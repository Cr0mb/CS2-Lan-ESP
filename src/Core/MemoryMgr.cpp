#include "MemoryMgr.h"
#include <cstdio>

// =============================================================================
//  Free MemoryMgr (V15) — NeacSafe64 with usermode fallback
//
//  Prefers NeacSafe64 kernel driver when available. Falls back to standard
//  ReadProcessMemory/WriteProcessMemory when driver is not connected.
// =============================================================================

MemoryMgr::MemoryMgr()
{
    ProcessID       = 0;
    driverConnected = false;
    hProcess        = nullptr;
}

MemoryMgr::~MemoryMgr()
{
    DisconnectDriver();
    if (hProcess != nullptr && hProcess != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hProcess);
        hProcess = nullptr;
    }
}

bool MemoryMgr::ConnectDriver(const LPCWSTR /*name*/)
{
    printf("[MEMORY] Connecting to NeacSafe64 driver...\n");
    driverConnected = neacsafe::ConnectDriver();
    if (driverConnected) {
        printf("[MEMORY] NeacSafe64 driver connected!\n");
    } else {
        printf("[MEMORY] NeacSafe64 driver connection FAILED\n");
        printf("[MEMORY] Make sure Driver.sys is present and run as Administrator\n");
    }
    return driverConnected;
}

bool MemoryMgr::DisconnectDriver()
{
    if (driverConnected)
    {
        neacsafe::DisconnectDriver();
        driverConnected = false;
    }
    ProcessID = 0;
    return true;
}

bool MemoryMgr::Attach(const DWORD pid)
{
    if (pid == 0) return false;

    // NeacSafe64 driver required - no usermode fallback
    if (!driverConnected)
    {
        printf("[MEMORY] ERROR: NeacSafe64 driver not connected!\n");
        return false;
    }

    printf("[MEMORY] Attaching to PID %lu via NeacSafe64...\n", pid);
    if (neacsafe::AttachToProcess(pid))
    {
        printf("[MEMORY] Attached via NeacSafe64\n");
        if (ProcessID != pid) {
            std::unique_lock<std::shared_mutex> wlock(moduleBaseCacheMtx);
            moduleBaseCache.clear();
        }
        ProcessID = pid;
        return true;
    }
    printf("[MEMORY] NeacSafe64 attach failed\n");
    return false;
}

bool MemoryMgr::AttachUsermode(const DWORD pid)
{
    if (pid == 0) return false;

    // Close previous handle if any
    if (hProcess != nullptr && hProcess != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hProcess);
        hProcess = nullptr;
    }

    // Open with read/write access
    hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == nullptr || hProcess == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        printf("[MEMORY] OpenProcess failed for PID %lu, error=%lu\n", pid, err);
        printf("[MEMORY] Error 5 = Access Denied (need admin or driver)\n");
        hProcess = nullptr;
        return false;
    }

    printf("[MEMORY] Successfully attached via usermode (OpenProcess)\n");
    if (ProcessID != pid) {
        std::unique_lock<std::shared_mutex> wlock(moduleBaseCacheMtx);
        moduleBaseCache.clear();
    }
    ProcessID = pid;
    return true;
}

DWORD MemoryMgr::GetProcessID(const wchar_t* processName)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe32 = { sizeof(PROCESSENTRY32W) };
    DWORD found = 0;
    if (Process32FirstW(hSnapshot, &pe32))
    {
        do {
            if (_wcsicmp(pe32.szExeFile, processName) == 0)
            { found = pe32.th32ProcessID; break; }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
    return found;
}

DWORD64 MemoryMgr::GetModuleBase(const wchar_t* moduleName)
{
    if (ProcessID == 0 || !moduleName) return 0;

    // Memoized lookup
    {
        std::shared_lock<std::shared_mutex> rlock(moduleBaseCacheMtx);
        auto it = moduleBaseCache.find(moduleName);
        if (it != moduleBaseCache.end() && it->second != 0)
            return it->second;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, ProcessID);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    DWORD64 found = 0;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0) {
                found = (DWORD64)(uintptr_t)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);

    if (found != 0) {
        std::unique_lock<std::shared_mutex> wlock(moduleBaseCacheMtx);
        moduleBaseCache[moduleName] = found;
    }
    return found;
}

bool MemoryMgr::BatchReadMemory(const std::vector<std::pair<DWORD64, SIZE_T>>& requests,
                                void* output_buffer)
{
    if (requests.empty() || !output_buffer)
        return false;

    BYTE* dst   = static_cast<BYTE*>(output_buffer);
    SIZE_T offs = 0;

    // Try NeacSafe64 driver first
    if (driverConnected && ProcessID != 0)
    {
        bool allGood = true;
        for (const auto& r : requests)
        {
            if (!neacsafe::ReadMem(r.first, dst + offs, r.second)) {
                allGood = false;
                break;
            }
            offs += r.second;
        }
        if (allGood) return true;
    }

    // Fallback to usermode
    if (hProcess != nullptr && hProcess != INVALID_HANDLE_VALUE)
    {
        offs = 0;
        for (const auto& r : requests)
        {
            SIZE_T bytesRead = 0;
            if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(r.first), dst + offs, r.second, &bytesRead))
                return false;
            if (bytesRead != r.second) return false;
            offs += r.second;
        }
        return true;
    }

    return false;
}
