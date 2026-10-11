#pragma once
#include <Windows.h>
#include <cstdint>

// Driver bridge — ASTRA64 direct (matches DayZ-External / GHaxLoaderClient).
// No user-mode read/write paths. All R/W is composed on top of the driver's
// physical-memory mapping IOCTL via a per-process page-table walk.

HANDLE connect_driver();
void   disconnect_driver();

const char* get_driver_label();

PVOID get_proc_base(HANDLE hDriver, DWORD Pid);

uintptr_t open_process_via_driver(HANDLE hDriver, DWORD Pid);

DWORD read_proc_memory (HANDLE hDriver, uintptr_t TargetHandle, PVOID Addr, DWORD Size, PVOID Out);
DWORD write_proc_memory(HANDLE hDriver, uintptr_t TargetHandle, PVOID Addr, DWORD Size, PVOID In);

// ─── Legacy adapters for cs2/Free call sites ────────────────────────────────
// The existing cs2/Free code calls `read_proc_memory(hPort, DWORD pid, addr, size, out)`
// and `write_proc_memory` with the same DWORD-pid signature. The ASTRA64 API
// already accepts the PID as the TargetHandle, so these inline adapters are
// just type-converting trampolines — no extra allocation, no extra IOCTL.
inline DWORD read_proc_memory(HANDLE hDriver, DWORD Pid, PVOID Addr, DWORD Size, PVOID Out)
{ return read_proc_memory(hDriver, (uintptr_t)Pid, Addr, Size, Out); }

inline DWORD write_proc_memory(HANDLE hDriver, DWORD Pid, PVOID Addr, DWORD Size, PVOID In)
{ return write_proc_memory(hDriver, (uintptr_t)Pid, Addr, Size, In); }

// Loader is responsible for installing/starting ASTRA64 *before* this process
// launches. These are kept as no-op stubs purely so legacy call sites that
// still reference `start_driver()` / `stop_driver()` link cleanly without
// recompiling every caller.
inline int  start_driver() { return connect_driver() != INVALID_HANDLE_VALUE ? 1 : 0; }
inline int  stop_driver()  { disconnect_driver(); return 1; }

// Legacy SSDT helpers used to live in NeacController. The ASTRA64 driver does
// not expose SSDT enumeration — features that relied on it are dead. Return
// FALSE so any straggling caller fails closed instead of corrupting memory.
inline BOOL kernel_read_data(HANDLE, PVOID, PVOID, DWORD) { return FALSE; }
inline BOOL get_ssdt_items(HANDLE, PVOID, DWORD)          { return FALSE; }
