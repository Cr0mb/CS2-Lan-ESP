#pragma once
#include <iostream>
#include <Windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <TlHelp32.h>
#include "NeacSafe/Driver.h"   // NeacSafe64 FltMgr minifilter driver

// =============================================================================
//  Free MemoryMgr (V15) — NeacSafe64 with usermode fallback
//
//  Prefers NeacSafe64 kernel driver when available. Falls back to standard
//  ReadProcessMemory/WriteProcessMemory when driver is not connected.
// =============================================================================

class MemoryMgr
{
public:
    MemoryMgr();
    ~MemoryMgr();
    bool ConnectDriver(const LPCWSTR name);
    bool DisconnectDriver();
    bool Attach(const DWORD pid);
    bool AttachUsermode(const DWORD pid);  // Usermode fallback
    DWORD64 GetModuleBase(const wchar_t* moduleName);
    DWORD GetProcessID(const wchar_t* processName);
    bool BatchReadMemory(const std::vector<std::pair<DWORD64, SIZE_T>>& requests, void* output_buffer);

    template <typename ReadType>
    bool ReadMemory(DWORD64 address, ReadType& value, SIZE_T size = sizeof(ReadType))
    {
        if (address == 0 || address >= 0x7FFFFFFFFFFF || size == 0 || size > 0x1000)
            return false;
        if (address + size < address)
            return false;

        // Try NeacSafe64 driver first
        if (driverConnected && ProcessID != 0)
        {
            if (neacsafe::ReadMem(address, &value, size))
                return true;
        }

        // Fallback to usermode
        if (hProcess != nullptr && hProcess != INVALID_HANDLE_VALUE)
        {
            SIZE_T bytesRead = 0;
            if (ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), &value, size, &bytesRead))
                return bytesRead == size;
        }

        return false;
    }

    template <typename WriteType>
    bool WriteMemory(DWORD64 address, const WriteType& value, SIZE_T size = sizeof(WriteType))
    {
        if (address == 0 || address >= 0x7FFFFFFFFFFF || size == 0 || size > 0x1000)
            return false;
        if (address + size < address)
            return false;

        // Try NeacSafe64 driver first
        if (driverConnected && ProcessID != 0)
        {
            if (neacsafe::WriteMem(address, &value, size))
                return true;
        }

        // Fallback to usermode
        if (hProcess != nullptr && hProcess != INVALID_HANDLE_VALUE)
        {
            SIZE_T bytesWritten = 0;
            if (WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(address), &value, size, &bytesWritten))
                return bytesWritten == size;
        }

        return false;
    }

    void TryReconnectDriver()
    {
        if (!driverConnected) {
            driverConnected = neacsafe::ConnectDriver();
        }
    }

    // Public state predicates — used by the menu's "Driver Status" indicator.
    bool IsDriverReady() const { return driverConnected; }
    bool IsAttached()    const { return ProcessID != 0; }
    bool IsUsermodeAttached() const { return hProcess != nullptr && hProcess != INVALID_HANDLE_VALUE; }

    int consecutiveFailures = 0;

    template<typename T>
    bool BatchReadStructured(const std::vector<DWORD64>& addresses, std::vector<T>& results) {
        if (addresses.empty()) return false;
        std::vector<std::pair<DWORD64, SIZE_T>> requests;
        requests.reserve(addresses.size());
        for (DWORD64 addr : addresses) requests.emplace_back(addr, sizeof(T));
        results.resize(addresses.size());
        return BatchReadMemory(requests, results.data());
    }

private:
    DWORD  ProcessID      = 0;
    bool   driverConnected = false;
    HANDLE hProcess       = nullptr;  // Usermode handle fallback

    // Per-session module-base cache. Toolhelp snapshots are O(modules), and
    // cs2 carries 200+ DLLs — running CreateToolhelp32Snapshot every frame
    // from CheckCurrentMapName / GrenadeHelper was the dominant cost in
    // menu-open profiles. Bases are stable for the game session.
    std::unordered_map<std::wstring, DWORD64> moduleBaseCache;
    mutable std::shared_mutex                 moduleBaseCacheMtx;
};

inline MemoryMgr memoryManager;
