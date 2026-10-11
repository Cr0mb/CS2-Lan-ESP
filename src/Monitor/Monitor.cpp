// USB-Lan-ESP Monitor Process
// Handles driver lifecycle: install, load, unload, cleanup
// Monitors main process and cleans up on exit/crash

#include <Windows.h>
#include <TlHelp32.h>
#include <fltUser.h>
#include <winternl.h>
#include <shellapi.h>
#include <string>
#include <filesystem>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "FltLib.lib")
#pragma comment(lib, "shell32.lib")

extern "C" NTSTATUS NTAPI NtLoadDriver(PUNICODE_STRING DriverServiceName);
extern "C" NTSTATUS NTAPI NtUnloadDriver(PUNICODE_STRING DriverServiceName);

namespace fs = std::filesystem;

// Check if running as admin
BOOL IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

// Driver constants
constexpr const char* SERVICE_NAME = "NeacSafe64";
constexpr const wchar_t* PORT_NAME = L"\\OWNeacSafePort";

// Global state
HANDLE g_mainProcess = NULL;
DWORD g_mainPID = 0;
bool g_driverLoaded = false;
bool g_serviceCreated = false;
std::string g_driverPath;
std::string g_exeDir;
std::string g_tempDir;  // Temp directory to clean up on exit

std::string GetExeDir() {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    auto p = s.find_last_of("\\/");
    return (p == std::string::npos) ? "." : s.substr(0, p);
}

std::string RegPath() {
    return std::string("System\\CurrentControlSet\\Services\\") + SERVICE_NAME;
}

std::wstring NtRegPath() {
    std::wstring w = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    for (const char* s = SERVICE_NAME; *s; ++s) w.push_back((wchar_t)*s);
    return w;
}

bool EnableLoadDriverPriv() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok))
        return false;
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

bool WriteServiceKey(const std::string& sysPath) {
    HKEY k = nullptr;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, RegPath().c_str(), 0, nullptr, 0,
                        KEY_ALL_ACCESS, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;

    std::string img = "\\??\\" + sysPath;
    RegSetValueExA(k, "ImagePath", 0, REG_EXPAND_SZ, (const BYTE*)img.c_str(), (DWORD)img.size() + 1);
    DWORD type = 2, err = 1, start = 3;
    RegSetValueExA(k, "Type", 0, REG_DWORD, (BYTE*)&type, sizeof(type));
    RegSetValueExA(k, "ErrorControl", 0, REG_DWORD, (BYTE*)&err, sizeof(err));
    RegSetValueExA(k, "Start", 0, REG_DWORD, (BYTE*)&start, sizeof(start));
    const char* grp = "FSFilter Activity Monitor";
    RegSetValueExA(k, "Group", 0, REG_SZ, (const BYTE*)grp, (DWORD)strlen(grp) + 1);
    DWORD sup = 3;
    RegSetValueExA(k, "SupportedFeatures", 0, REG_DWORD, (BYTE*)&sup, sizeof(sup));
    static const char deps[] = "FltMgr\0";
    RegSetValueExA(k, "DependOnService", 0, REG_MULTI_SZ, (const BYTE*)deps, sizeof(deps));

    // Create Instances subkey
    HKEY inst = nullptr;
    std::string ipath = RegPath() + "\\Instances";
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, ipath.c_str(), 0, nullptr, 0,
                        KEY_ALL_ACCESS, nullptr, &inst, nullptr) == ERROR_SUCCESS) {
        const char* def = "NeacSafe64 Instance";
        RegSetValueExA(inst, "DefaultInstance", 0, REG_SZ, (const BYTE*)def, (DWORD)strlen(def) + 1);
        RegCloseKey(inst);

        HKEY ient = nullptr;
        std::string iep = ipath + "\\NeacSafe64 Instance";
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, iep.c_str(), 0, nullptr, 0,
                            KEY_ALL_ACCESS, nullptr, &ient, nullptr) == ERROR_SUCCESS) {
            const char* alt = "370020";
            RegSetValueExA(ient, "Altitude", 0, REG_SZ, (const BYTE*)alt, (DWORD)strlen(alt) + 1);
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
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ipath.c_str(), 0, KEY_ALL_ACCESS, &inst) == ERROR_SUCCESS) {
        RegDeleteKeyA(inst, "NeacSafe64 Instance");
        RegCloseKey(inst);
    }
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, ipath.c_str());
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, RegPath().c_str());
}

bool IsDriverLoaded() {
    HANDLE port = INVALID_HANDLE_VALUE;
    HRESULT hr = FilterConnectCommunicationPort(PORT_NAME, 0, nullptr, 0, nullptr, &port);
    if (hr == S_OK && port != INVALID_HANDLE_VALUE) {
        CloseHandle(port);
        return true;
    }
    return false;
}

bool LoadDriver() {
    if (IsDriverLoaded()) {
        g_driverLoaded = true;
        return true;
    }

    if (!EnableLoadDriverPriv())
        return false;

    g_driverPath = g_exeDir + "\\Driver.sys";
    if (!fs::exists(g_driverPath))
        return false;

    if (!WriteServiceKey(g_driverPath))
        return false;
    g_serviceCreated = true;

    std::wstring ntPath = NtRegPath();
    UNICODE_STRING us{};
    us.Length = (USHORT)(ntPath.size() * sizeof(wchar_t));
    us.MaximumLength = us.Length + sizeof(wchar_t);
    us.Buffer = const_cast<PWSTR>(ntPath.c_str());

    NTSTATUS status = NtLoadDriver(&us);
    if (status != 0 && status != (NTSTATUS)0xC000010E && status != (NTSTATUS)0xC0000035) {
        DeleteServiceKey();
        g_serviceCreated = false;
        return false;
    }

    g_driverLoaded = true;
    return true;
}

void UnloadDriver() {
    if (!g_driverLoaded && !g_serviceCreated)
        return;

    EnableLoadDriverPriv();

    if (g_driverLoaded) {
        std::wstring ntPath = NtRegPath();
        UNICODE_STRING us{};
        us.Length = (USHORT)(ntPath.size() * sizeof(wchar_t));
        us.MaximumLength = us.Length + sizeof(wchar_t);
        us.Buffer = const_cast<PWSTR>(ntPath.c_str());
        NtUnloadDriver(&us);
        g_driverLoaded = false;
    }

    if (g_serviceCreated) {
        DeleteServiceKey();
        g_serviceCreated = false;
    }
}

bool IsProcessRunning(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == NULL) return false;

    DWORD exitCode = 0;
    BOOL result = GetExitCodeProcess(h, &exitCode);
    CloseHandle(h);

    return result && exitCode == STILL_ACTIVE;
}

DWORD LaunchMainProcess() {
    std::string mainExe = g_exeDir + "\\USB-Lan-ESP.exe";
    if (!fs::exists(mainExe))
        return 0;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    std::string cmdLine = "\"" + mainExe + "\" --monitored";

    if (!CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()),
                        nullptr, nullptr, FALSE, 0, nullptr,
                        g_exeDir.c_str(), &si, &pi)) {
        return 0;
    }

    CloseHandle(pi.hThread);
    g_mainProcess = pi.hProcess;
    g_mainPID = pi.dwProcessId;
    return g_mainPID;
}

void Cleanup() {
    if (g_mainProcess && IsProcessRunning(g_mainPID)) {
        TerminateProcess(g_mainProcess, 0);
        WaitForSingleObject(g_mainProcess, 3000);
    }
    if (g_mainProcess) {
        CloseHandle(g_mainProcess);
        g_mainProcess = NULL;
    }

    UnloadDriver();

    std::string tempDriver = std::string(getenv("LOCALAPPDATA")) + "\\GHax Labs\\Driver.sys";
    if (fs::exists(tempDriver)) {
        try { fs::remove(tempDriver); } catch (...) {}
    }

    if (!g_tempDir.empty() && fs::exists(g_tempDir)) {
        std::string cmd = "cmd /c ping 127.0.0.1 -n 2 >nul & rd /s /q \"" + g_tempDir + "\"";
        STARTUPINFOA si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread) CloseHandle(pi.hThread);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_exeDir = GetExeDir();

    // Parse --tempdir argument for cleanup
    std::string cmdLine(lpCmdLine);
    size_t pos = cmdLine.find("--tempdir=");
    if (pos != std::string::npos) {
        g_tempDir = cmdLine.substr(pos + 10);
        size_t space = g_tempDir.find(' ');
        if (space != std::string::npos) g_tempDir = g_tempDir.substr(0, space);
    }

    if (!IsRunningAsAdmin()) {
        return 1;
    }

    atexit(Cleanup);

    if (!LoadDriver()) {
        return 1;
    }

    if (!LaunchMainProcess()) {
        Cleanup();
        return 1;
    }

    // Monitor loop - silent
    while (true) {
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            break;
        }
        if (!IsProcessRunning(g_mainPID)) {
            break;
        }
        Sleep(500);
    }

    Cleanup();
    return 0;
}
