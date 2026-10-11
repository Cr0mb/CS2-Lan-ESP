// NeacSafe64 driver interface for CS2 Free
// FltMgr minifilter communication via FilterConnectCommunicationPort
#pragma once
#include <Windows.h>
#include <cstdint>

namespace neacsafe {

bool ConnectDriver();
void DisconnectDriver();
bool IsConnected();

bool AttachToProcess(DWORD pid);
bool AttachToProcessName(const char* name);

DWORD    GetAttachedPid();
uint64_t GetProcessBase();

bool ReadMem(uint64_t addr, void* buf, size_t len);
bool WriteMem(uint64_t addr, const void* buf, size_t len);

const char* DriverLabel();

} // namespace neacsafe
