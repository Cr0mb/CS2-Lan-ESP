// NeacSafe64 client (FltMgr minifilter) for CS2 Free
// Adapted from DayZ Dumper's driver.cpp
#include "Driver.h"
#include "DriverResource.h"

#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <fltUser.h>
#include <ShlObj.h>
#include <emmintrin.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "FltLib.lib")

extern "C" NTSTATUS NTAPI NtLoadDriver  (PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING DriverServiceName);

namespace neacsafe {
namespace {

constexpr const char*    kServiceName = "NeacSafe64";
constexpr const char*    kDriverFile  = "Driver.sys";
constexpr const wchar_t* kPortName    = L"\\OWNeacSafePort";

constexpr DWORD       kMagic   = 0x4655434B;   // 'FUCK' (matches shipped driver)
constexpr DWORD       kVersion = 8;
constexpr const char* kKey     = "FuckKeenFuckKeenFuckKeenFuckKeen";

#pragma pack(push, 1)
struct NEAC_FILTER_CONNECT { DWORD Magic; DWORD Version; BYTE EncKey[32]; };
struct READ_PACKET   { BYTE Op; DWORD Pid; PVOID Addr; DWORD Size; };
struct WRITE_PACKET  { BYTE Op; DWORD Pid; PVOID Addr; DWORD Size; };
struct BASE_PACKET   { BYTE Op; DWORD Pid; };
#pragma pack(pop)

HANDLE   g_port         = INVALID_HANDLE_VALUE;
DWORD    g_target_pid   = 0;
uint64_t g_target_base  = 0;
bool     g_service_made = false;
BYTE     g_key[32]      = {};

std::string SelfDir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    auto p = s.find_last_of("\\/");
    return (p == std::string::npos) ? std::string(".") : s.substr(0, p);
}

std::string LocalAppDataDir() {
    wchar_t* p = nullptr;
    if (!SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)) || !p)
        return {};
    std::wstring w = p;
    CoTaskMemFree(p);
    w += L"\\GHax Labs";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string u(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u.data(), n, nullptr, nullptr);
    CreateDirectoryA(u.c_str(), nullptr);
    return u;
}

std::string ResolveOrExtractDriverPath() {
    // 1. %LOCALAPPDATA%\GHax Labs\Driver.sys
    std::string lad = LocalAppDataDir();
    if (!lad.empty()) {
        std::string p = lad + "\\" + kDriverFile;
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            printf("[DRIVER] using %s\n", p.c_str());
            return p;
        }
    }
    // 2. Sibling-of-EXE
    std::string sib = SelfDir() + "\\" + kDriverFile;
    if (GetFileAttributesA(sib.c_str()) != INVALID_FILE_ATTRIBUTES) {
        printf("[DRIVER] using %s\n", sib.c_str());
        return sib;
    }
    // 3. Extract from embedded resource
    HRSRC info = FindResourceA(nullptr, MAKEINTRESOURCEA(IDR_NEACSAFE_DRIVER), "RCDATA");
    if (!info) {
        printf("[DRIVER] no embedded NeacSafe64 resource and no Driver.sys found.\n");
        return {};
    }
    DWORD   sz  = SizeofResource(nullptr, info);
    HGLOBAL g   = LoadResource(nullptr, info);
    if (!g || !sz) {
        printf("[DRIVER] LoadResource failed err=%lu\n", GetLastError());
        return {};
    }
    const void* ptr = LockResource(g);
    if (!ptr) {
        printf("[DRIVER] LockResource failed err=%lu\n", GetLastError());
        return {};
    }
    if (lad.empty()) {
        printf("[DRIVER] no %%LOCALAPPDATA%%; can't extract embedded driver.\n");
        return {};
    }
    std::string dst = lad + "\\" + kDriverFile;
    HANDLE f = CreateFileA(dst.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        printf("[DRIVER] open %s failed err=%lu\n", dst.c_str(), GetLastError());
        return {};
    }
    DWORD wr = 0;
    BOOL ok = WriteFile(f, ptr, sz, &wr, nullptr) && wr == sz;
    CloseHandle(f);
    if (!ok) {
        printf("[DRIVER] write %s failed (%lu/%lu)\n", dst.c_str(), wr, sz);
        return {};
    }
    printf("[DRIVER] extracted embedded NeacSafe64 driver -> %s (%lu bytes)\n",
           dst.c_str(), sz);
    return dst;
}

bool EnableLoadDriverPriv() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    LUID luid{};
    bool ok = LookupPrivilegeValueA(nullptr, "SeLoadDriverPrivilege", &luid) != 0;
    if (ok) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ok = AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr) != 0
             && GetLastError() == ERROR_SUCCESS;
    }
    CloseHandle(tok);
    return ok;
}

std::string RegPath() { return std::string("System\\CurrentControlSet\\Services\\") + kServiceName; }
std::wstring NtRegPath() {
    std::wstring w = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    for (const char* s = kServiceName; *s; ++s) w.push_back((wchar_t)*s);
    return w;
}

bool WriteServiceKey(const std::string& sys_path) {
    HKEY k = nullptr;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, RegPath().c_str(), 0, nullptr, 0,
                        KEY_ALL_ACCESS, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    std::string img = std::string("\\??\\") + sys_path;
    RegSetValueExA(k, "ImagePath", 0, REG_EXPAND_SZ,
                   (const BYTE*)img.c_str(), (DWORD)img.size() + 1);
    DWORD type = 2, err = 1, start = 3;
    RegSetValueExA(k, "Type",         0, REG_DWORD, (BYTE*)&type,  sizeof(type));
    RegSetValueExA(k, "ErrorControl", 0, REG_DWORD, (BYTE*)&err,   sizeof(err));
    RegSetValueExA(k, "Start",        0, REG_DWORD, (BYTE*)&start, sizeof(start));
    const char* lo = "FSFilter Activity Monitor";
    RegSetValueExA(k, "Group", 0, REG_SZ, (const BYTE*)lo, (DWORD)std::strlen(lo) + 1);
    DWORD sup = 3;
    RegSetValueExA(k, "SupportedFeatures", 0, REG_DWORD, (BYTE*)&sup, sizeof(sup));
    static const char deps[] = "FltMgr\0";
    RegSetValueExA(k, "DependOnService", 0, REG_MULTI_SZ,
                   (const BYTE*)deps, sizeof(deps));

    HKEY inst = nullptr;
    std::string ipath = RegPath() + "\\Instances";
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, ipath.c_str(), 0, nullptr, 0,
                        KEY_ALL_ACCESS, nullptr, &inst, nullptr) == ERROR_SUCCESS) {
        const char* def = "NeacSafe64 Instance";
        RegSetValueExA(inst, "DefaultInstance", 0, REG_SZ,
                       (const BYTE*)def, (DWORD)std::strlen(def) + 1);
        RegCloseKey(inst);

        HKEY ient = nullptr;
        std::string iep = ipath + "\\NeacSafe64 Instance";
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, iep.c_str(), 0, nullptr, 0,
                            KEY_ALL_ACCESS, nullptr, &ient, nullptr) == ERROR_SUCCESS) {
            const char* alt = "370020";
            RegSetValueExA(ient, "Altitude", 0, REG_SZ,
                           (const BYTE*)alt, (DWORD)std::strlen(alt) + 1);
            DWORD flags = 0;
            RegSetValueExA(ient, "Flags", 0, REG_DWORD, (BYTE*)&flags, sizeof(flags));
            RegCloseKey(ient);
        }
    }
    RegCloseKey(k);
    return true;
}

void DeleteServiceKey() {
    HKEY inst = nullptr;
    std::string ipath = RegPath() + "\\Instances";
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ipath.c_str(), 0,
                      KEY_ALL_ACCESS, &inst) == ERROR_SUCCESS) {
        RegDeleteKeyA(inst, "NeacSafe64 Instance");
        RegCloseKey(inst);
    }
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, ipath.c_str());
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, RegPath().c_str());
}

NTSTATUS LoadDriverNt(const std::wstring& nt_path) {
    UNICODE_STRING us{};
    us.Length        = (USHORT)(nt_path.size() * sizeof(wchar_t));
    us.MaximumLength = us.Length + sizeof(wchar_t);
    us.Buffer        = const_cast<PWSTR>(nt_path.c_str());
    return NtLoadDriver(&us);
}

NTSTATUS UnloadDriverNt(const std::wstring& nt_path) {
    UNICODE_STRING us{};
    us.Length        = (USHORT)(nt_path.size() * sizeof(wchar_t));
    us.MaximumLength = us.Length + sizeof(wchar_t);
    us.Buffer        = const_cast<PWSTR>(nt_path.c_str());
    return NtUnloadDriver(&us);
}

void encrypt_block(unsigned int* buffer, unsigned int idx) {
    static const unsigned char enc_imm[16] = {
        0x7A, 0x54, 0xE5, 0x41, 0x8B, 0xDB, 0xB0, 0x55,
        0x7A, 0xBD, 0x01, 0xBD, 0x1A, 0x7F, 0x9E, 0x17
    };
    __m128i imm = _mm_loadu_si128((__m128i*)enc_imm);
    __m128i v2  = _mm_cvtsi32_si128(idx);
    __m128i v8  = _mm_xor_si128(
        _mm_shuffle_epi32(_mm_shufflelo_epi16(_mm_unpacklo_epi8(v2, v2), 0), 0),
        imm);
    unsigned int* result = &v8.m128i_u32[3];
    __m128i v5 = _mm_cvtsi32_si128(0x4070E1Fu);
    for (int v4 = 0; v4 < 4; v4++) {
        __m128i v6 = _mm_shufflelo_epi16(
            _mm_unpacklo_epi8(
                _mm_or_si128(_mm_cvtsi32_si128(*result), v5),
                _mm_setzero_si128()),
            27);
        v6 = _mm_packus_epi16(v6, v6);
        *buffer = (*buffer ^ ~idx) ^ v6.m128i_u32[0] ^ idx;
        buffer++;
        result = (unsigned int*)((char*)result - 1);
    }
}

void encode_payload(BYTE* buffer, size_t size) {
    for (size_t i = 0; i < size; i++) buffer[i] ^= g_key[i & 31];
    unsigned int* ptr = (unsigned int*)buffer;
    unsigned int v12 = 0;
    do {
        encrypt_block(ptr, v12++);
        ptr += 4;
    } while (v12 < size >> 4);
}

template <typename T>
size_t PaddedSize() { return ((sizeof(T) / 16) + 1) * 16; }

} // anon

bool ConnectDriver() {
    if (g_port != INVALID_HANDLE_VALUE) return true;

    std::memcpy(g_key, kKey, 32);
    NEAC_FILTER_CONNECT ctx{};
    ctx.Magic   = kMagic;
    ctx.Version = kVersion;
    std::memcpy(ctx.EncKey, g_key, 32);

    // Try connecting first — if already loaded, reuse it
    HRESULT hr = FilterConnectCommunicationPort(
        kPortName, 0, &ctx, sizeof(ctx), nullptr, &g_port);
    if (hr == S_OK && g_port != INVALID_HANDLE_VALUE) {
        printf("[DRIVER] NeacSafe64 already loaded — connected without re-load.\n");
        return true;
    }
    g_port = INVALID_HANDLE_VALUE;

    // Driver not loaded; load it ourselves
    std::string sys = ResolveOrExtractDriverPath();
    if (sys.empty()) return false;

    if (!EnableLoadDriverPriv()) {
        printf("[DRIVER] SeLoadDriverPrivilege denied. Run as Administrator.\n");
        return false;
    }
    if (!WriteServiceKey(sys)) {
        printf("[DRIVER] WriteServiceKey failed err=%lu\n", GetLastError());
        return false;
    }
    g_service_made = true;

    NTSTATUS s = LoadDriverNt(NtRegPath());
    if (s != 0 && s != (NTSTATUS)0xC000010E /* STATUS_IMAGE_ALREADY_LOADED */) {
        printf("[DRIVER] NtLoadDriver failed: 0x%08lX\n", (unsigned long)s);
        DeleteServiceKey();
        g_service_made = false;
        return false;
    }
    printf("[DRIVER] NeacSafe64 loaded via NtLoadDriver.\n");

    hr = FilterConnectCommunicationPort(
        kPortName, 0, &ctx, sizeof(ctx), nullptr, &g_port);
    if (hr != S_OK || g_port == INVALID_HANDLE_VALUE) {
        printf("[DRIVER] FilterConnectCommunicationPort failed hr=0x%08lX\n",
               (unsigned long)hr);
        UnloadDriverNt(NtRegPath());
        DeleteServiceKey();
        g_service_made = false;
        g_port = INVALID_HANDLE_VALUE;
        return false;
    }
    return true;
}

void DisconnectDriver() {
    if (g_port != INVALID_HANDLE_VALUE) {
        CloseHandle(g_port);
        g_port = INVALID_HANDLE_VALUE;
    }
    if (g_service_made) {
        EnableLoadDriverPriv();
        UnloadDriverNt(NtRegPath());
        DeleteServiceKey();
        g_service_made = false;
    }
    g_target_pid  = 0;
    g_target_base = 0;
}

bool IsConnected() {
    return g_port != INVALID_HANDLE_VALUE;
}

uint64_t QueryBaseFromDriver(DWORD pid) {
    if (g_port == INVALID_HANDLE_VALUE) return 0;
    size_t bs = PaddedSize<BASE_PACKET>();
    std::vector<BYTE> pkt(bs, 0);
    auto* p = (BASE_PACKET*)pkt.data();
    p->Op = 32; p->Pid = pid;
    encode_payload(pkt.data(), bs);
    BYTE res[16] = {};
    DWORD out = 0;
    if (FilterSendMessage(g_port, pkt.data(), (DWORD)bs, res, 16, &out) != S_OK)
        return 0;
    return *(uint64_t*)res;
}

bool AttachToProcess(DWORD pid) {
    if (g_port == INVALID_HANDLE_VALUE) return false;
    if (g_target_pid == pid && g_target_base) return true;
    g_target_pid  = pid;
    g_target_base = QueryBaseFromDriver(pid);
    if (!g_target_base) {
        printf("[DRIVER] QueryBaseFromDriver(pid=%lu) returned 0 — target may not be running.\n",
               pid);
        return false;
    }
    printf("[DRIVER] attached pid=%lu base=0x%llX\n",
           pid, (unsigned long long)g_target_base);
    return true;
}

bool AttachToProcessName(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD found = 0;
    // Convert name to wide string for comparison
    wchar_t wname[260] = {};
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 260);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, wname) == 0) { found = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!found) {
        printf("[DRIVER] process '%s' not running\n", name);
        return false;
    }
    return AttachToProcess(found);
}

DWORD    GetAttachedPid()  { return g_target_pid; }
uint64_t GetProcessBase()  { return g_target_base; }

bool ReadMem(uint64_t addr, void* buf, size_t len) {
    if (g_port == INVALID_HANDLE_VALUE || !g_target_pid || !addr || !buf || !len)
        return false;
    size_t bs = PaddedSize<READ_PACKET>();
    BYTE pkt[64] = {};
    auto* p = (READ_PACKET*)pkt;
    p->Op = 9; p->Pid = g_target_pid;
    p->Addr = (PVOID)(uintptr_t)addr; p->Size = (DWORD)len;
    encode_payload(pkt, bs);
    DWORD out = 0;
    HRESULT hr = FilterSendMessage(g_port, pkt, (DWORD)bs, buf, (DWORD)len, &out);
    return hr == S_OK;
}

bool WriteMem(uint64_t addr, const void* buf, size_t len) {
    if (g_port == INVALID_HANDLE_VALUE || !g_target_pid || !addr || !buf || !len)
        return false;
    size_t bs = PaddedSize<WRITE_PACKET>();
    std::vector<BYTE> pkt(bs, 0);
    auto* p = (WRITE_PACKET*)pkt.data();
    p->Op = 61; p->Pid = g_target_pid;
    p->Addr = (PVOID)(uintptr_t)addr; p->Size = (DWORD)len;
    encode_payload(pkt.data(), bs);
    DWORD out = 0;
    return FilterSendMessage(g_port, pkt.data(), (DWORD)bs,
                             (PVOID)buf, (DWORD)len, &out) == S_OK;
}

const char* DriverLabel() {
    return (g_port != INVALID_HANDLE_VALUE) ? "NeacSafe64" : "-";
}

} // namespace neacsafe
