#pragma once
#include <Windows.h>
#include <cstdint>

// =============================================================================
//  drv_defs.h — ASTRA64.sys (Astra32 / TVicHW) driver definitions.
//
//  v6.0 replaces the v5.9 ghax-rw section-channel architecture. The External
//  now talks directly to ASTRA64.sys via its single physical-memory mapping
//  IOCTL — no intermediate driver, no GHaxMapper, no shared section.
//
//  ASTRA64 exposes \Device\Astra32Device0 (DOS path \\.\Astra32Device0) and
//  IOCTL 0x80002008, which calls MmMapIoSpaceEx() on the requested physical
//  page and returns the mapped VA truncated to its low 32 bits via the
//  `interface_type` output field (driver bug — the upper half is recovered
//  user-side with VirtualQuery).
//
//  Arbitrary process-VA R/W is composed on top of physical R/W:
//    1. Open \\.\Astra32Device0
//    2. Brute-force System CR3 by scanning the low 64 MiB of phys memory for
//       a PML4 that cleanly translates KUSER_SHARED_DATA (NtMajorVersion==10).
//    3. Read IA32_LSTAR via IOCTL 0x800020EC, walk back to ntoskrnl MZ.
//    4. Resolve PsInitialSystemProcess, walk ActiveProcessLinks to the target
//       PID, read its DirBase (EPROCESS+0x28).
//    5. For each read/write: walk the target's PML4 → PT to translate VA→PA,
//       map+memcpy the page, unmap.
//
//  HVCI / VBS bypass: writes go via \Device\PhysicalMemory rather than virtual
//  address space, so KMCI page protections don't apply. Reads use a bounce
//  through ReadProcessMemory to survive any VTL 1 EPT faults.
// =============================================================================

namespace astra64 {

constexpr wchar_t DEVICE_PATH[]   = L"\\\\.\\Astra32Device0";
constexpr wchar_t SERVICE_NAME[]  = L"ASTRA64";
constexpr wchar_t DRIVER_FILENAME[] = L"ASTRA64.sys";

constexpr unsigned long IOCTL_MAP_PHYS = 0x80002008;
constexpr unsigned long IOCTL_READ_MSR = 0x800020EC;

constexpr unsigned int IA32_LSTAR = 0xC0000082u;

constexpr unsigned __int64 KUSD_VA          = 0xFFFFF78000000000ULL;
constexpr unsigned __int64 EX_FAST_REF_MASK = 0xFULL;

constexpr unsigned __int64 PAGE_MASK_4K  = 0x000FFFFFFFFFF000ULL;
constexpr unsigned __int64 PAGE_MASK_2M  = 0x000FFFFFFFE00000ULL;
constexpr unsigned __int64 PAGE_MASK_1G  = 0x000FFFFC00000000ULL;

#pragma pack(push, 1)
struct MapPhysInput {
    uint32_t interface_type;  // OUT: low 32 bits of mapped VA (driver quirk)
    uint32_t bus_number;
    uint64_t physical_addr;
    uint32_t address_space;
    uint32_t size;
};
#pragma pack(pop)

inline bool IsKernelPtr(unsigned __int64 v) {
    return v > 0xFFFF800000000000ULL && v < 0xFFFFFFFFFFFFFFF0ULL;
}

} // namespace astra64
