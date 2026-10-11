// =============================================================================
//  controller.cpp — driver bridge (ASTRA64 direct, v6.0+)
//
//  Talks straight to ASTRA64.sys (Astra32 / TVicHW) via the single
//  MmMapIoSpaceEx IOCTL it exposes (0x80002008). No intermediate driver, no
//  shared section, no GHaxMapper. The mapping primitive is physical memory;
//  arbitrary process-VA R/W is composed on top of that via a 4-level page
//  table walk under the target process's DirBase (read out of EPROCESS+0x28
//  after walking ActiveProcessLinks from PsInitialSystemProcess).
//
//  HVCI/VBS-safe by construction: writes go through \Device\PhysicalMemory
//  via the driver's mapping IOCTL, not through any kernel-VA write primitive.
//  Reads bounce through ReadProcessMemory so VTL 1 EPT faults are caught
//  rather than BSOD-ing.
// =============================================================================

#include "driver_controller.h"
#include "driver_log.h"
#include "drv_defs.h"

#include <Windows.h>
#include <Psapi.h>
#include <winternl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <atomic>
#include <vector>
#include <mutex>
#include <unordered_map>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "psapi.lib")

namespace {

using namespace astra64;

// ── State ──────────────────────────────────────────────────────────────────
HANDLE  g_Device       = INVALID_HANDLE_VALUE;
uint64_t g_SystemCr3   = 0;
uint64_t g_KernelBase  = 0;
uint64_t g_SystemEproc = 0;

// Target cache — per External run, target PID is stable.
DWORD   g_TargetPid    = 0;
uint64_t g_TargetEproc = 0;
uint64_t g_TargetCr3   = 0;

// Build-dependent EPROCESS offsets
struct EprocOffsets {
    uint64_t pid;
    uint64_t links;
    uint64_t token;
    uint64_t image_name;
    uint64_t dirbase;       // _KPROCESS.DirectoryTableBase — kernel CR3 under KVAS
    uint64_t user_dirbase;  // _KPROCESS.UserDirectoryTableBase — real user CR3
};
EprocOffsets g_EpOffs{};

std::atomic<uint64_t> g_ReadOK{0};
std::atomic<uint64_t> g_ReadFail{0};

// VirtualQuery scan hint for upper 32 bits of mapped VA
uint64_t g_HintHigh = 0;

// ── VA → PA translation cache ─────────────────────────────────────────────
// A 4-level page-table walk costs 4 IOCTLs (one per level). Caching the
// per-page result means subsequent reads to the same target VA skip the walk
// and go straight to ReadPhys — collapsing 5 IOCTLs/read down to 1. This is
// the lossless optimization: we only cache *results*, never hold any kernel
// mappings, so the driver's MmMapIoSpaceEx pool can't leak.
//
// Direct-mapped open-addressing table with atomic slots. Invalidation by
// epoch bump on target CR3 change — old slots stay in place but fail the
// epoch check on next lookup and get overwritten naturally.
// =============================================================================
constexpr size_t kVtopSlotCount = 16384;  // power of 2 — ~256 KiB for the table

struct VtopSlot {
    std::atomic<uint64_t> va_page;   // 0 == empty
    std::atomic<uint64_t> pa_page;
    std::atomic<uint64_t> cr3_tag;   // MUST match — different CR3s can map
                                      // the same VA to different PAs (e.g.
                                      // FindSystemCr3 tries many candidates
                                      // against KUSER_SHARED_DATA).
    std::atomic<uint32_t> epoch;
};
VtopSlot g_VtopSlots[kVtopSlotCount];
std::atomic<uint32_t> g_VtopEpoch{1};

static inline size_t VtopHash(uint64_t va_page) {
    // 64-bit MurmurHash3 finalizer — well-distributed for page-aligned VAs.
    uint64_t k = va_page;
    k ^= k >> 33; k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return (size_t)(k & (kVtopSlotCount - 1));
}

static void ClearVtopCache() {
    g_VtopEpoch.fetch_add(1, std::memory_order_relaxed);
}

// Forward decl — MapPhysRaw is defined below but MapPhysCached needs it.
static uintptr_t MapPhysRaw(uint64_t phys, uint32_t size);

// ── PA → user-VA mapping cache ────────────────────────────────────────────
// Bounded LRU under a mutex. Caches the user-VA that IOCTL_MAP_PHYS returned
// for a given phys page so subsequent reads to the same page skip the IOCTL
// entirely. Hard cap on entries — every eviction calls UnmapViewOfFile so
// the driver's MmMapIoSpaceEx pool can't leak. Cap is intentionally small
// (256 × 4 KiB = 1 MiB pinned) — well under what the driver can sustain,
// and large enough to cover ESP's hot working set (typically <100 pages).
constexpr size_t kPhysCacheCap = 256;
struct PhysEntry { uintptr_t va; uint64_t lru; };
std::mutex g_PhysMtx;
std::unordered_map<uint64_t, PhysEntry> g_PhysCache;
uint64_t g_LruClock = 0;

static void ClearPhysCache() {
    std::lock_guard<std::mutex> lk(g_PhysMtx);
    for (auto& kv : g_PhysCache) UnmapViewOfFile((LPCVOID)kv.second.va);
    g_PhysCache.clear();
    g_LruClock = 0;
}

// Returns (va, cached). Caller MUST call ReleaseMap on the result — for
// uncached entries that's an UnmapViewOfFile; for cached entries it's a
// no-op (the cache keeps the mapping alive).
struct MapHandle { uintptr_t va; bool cached; };

static MapHandle MapPhysCached(uint64_t phys_page) {
    {
        std::lock_guard<std::mutex> lk(g_PhysMtx);
        auto it = g_PhysCache.find(phys_page);
        if (it != g_PhysCache.end()) {
            it->second.lru = ++g_LruClock;
            return { it->second.va, true };
        }
    }
    uintptr_t va = MapPhysRaw(phys_page, 0x1000);
    if (!va) return { 0, false };

    std::lock_guard<std::mutex> lk(g_PhysMtx);
    auto it = g_PhysCache.find(phys_page);
    if (it != g_PhysCache.end()) {
        // Lost the race — keep the existing mapping, drop our duplicate.
        UnmapViewOfFile((LPCVOID)va);
        it->second.lru = ++g_LruClock;
        return { it->second.va, true };
    }
    if (g_PhysCache.size() >= kPhysCacheCap) {
        // Evict LRU. Linear scan over 256 entries is ~50 ns — cheaper than
        // maintaining a separate eviction list for a cache this small.
        uint64_t oldest = UINT64_MAX;
        uint64_t victim_pa = 0;
        uintptr_t victim_va = 0;
        for (auto& kv : g_PhysCache) {
            if (kv.second.lru < oldest) {
                oldest = kv.second.lru;
                victim_pa = kv.first;
                victim_va = kv.second.va;
            }
        }
        if (victim_pa) {
            g_PhysCache.erase(victim_pa);
            UnmapViewOfFile((LPCVOID)victim_va);
        }
    }
    g_PhysCache[phys_page] = { va, ++g_LruClock };
    return { va, true };
}

static inline void ReleaseMap(MapHandle h) {
    if (!h.cached && h.va) UnmapViewOfFile((LPCVOID)h.va);
}

// ── Physical mapping ───────────────────────────────────────────────────────
static uintptr_t MapPhysRaw(uint64_t phys, uint32_t size) {
    if (g_Device == INVALID_HANDLE_VALUE) return 0;

    MapPhysInput io{};
    io.interface_type = 0;
    io.bus_number     = 0;
    io.physical_addr  = phys;
    io.address_space  = 0;
    io.size           = size;

    DWORD ret = 0;
    if (!DeviceIoControl(g_Device, IOCTL_MAP_PHYS,
                         &io, sizeof(io), &io, sizeof(io),
                         &ret, nullptr)) {
        return 0;
    }
    uint64_t low = io.interface_type;
    if (!low) return 0;

    auto try_va = [&](uint64_t hi) -> uintptr_t {
        uintptr_t cand = (uintptr_t)((hi << 32) | low);
        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T n = VirtualQuery((LPCVOID)cand, &mbi, sizeof(mbi));
        if (n > 0 && mbi.State == MEM_COMMIT) return cand;
        return 0;
    };

    if (uintptr_t va = try_va(g_HintHigh)) return va;
    for (uint64_t hi = 0; hi < 0x8000ULL; ++hi) {
        if (hi == g_HintHigh) continue;
        if (uintptr_t va = try_va(hi)) { g_HintHigh = hi; return va; }
    }
    return 0;
}

// HVCI-safe read: bounce through ReadProcessMemory(self) so VTL 1 EPT faults
// surface as STATUS_PARTIAL_COPY rather than crashing us. Slow (real syscall
// per call) but works on every config including HVCI-on machines.
static bool SafeCopyFrom(const void* src, void* dst, size_t len) {
    SIZE_T got = 0;
    BOOL ok = ReadProcessMemory(GetCurrentProcess(), src, dst, len, &got);
    return ok && got == len;
}

static bool ReadPhys(uint64_t addr, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    size_t pos = 0;
    uint64_t cur = addr;
    while (pos < len) {
        uint64_t page = cur & ~0xFFFULL;
        size_t   off  = (size_t)(cur & 0xFFF);
        size_t   chunk = (len - pos) < (0x1000 - off) ? (len - pos) : (0x1000 - off);

        MapHandle h = MapPhysCached(page);
        if (!h.va) return false;
        bool ok = SafeCopyFrom((const void*)(h.va + off), out + pos, chunk);
        ReleaseMap(h);
        if (!ok) return false;

        pos += chunk;
        cur += chunk;
    }
    return true;
}

static bool WritePhys(uint64_t addr, const void* buf, size_t len) {
    const uint8_t* in = (const uint8_t*)buf;
    size_t pos = 0;
    uint64_t cur = addr;
    while (pos < len) {
        uint64_t page = cur & ~0xFFFULL;
        size_t   off  = (size_t)(cur & 0xFFF);
        size_t   chunk = (len - pos) < (0x1000 - off) ? (len - pos) : (0x1000 - off);

        MapHandle h = MapPhysCached(page);
        if (!h.va) return false;
        bool ok = true;
        __try {
            memcpy((void*)(h.va + off), in + pos, chunk);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
        ReleaseMap(h);
        if (!ok) return false;

        pos += chunk;
        cur += chunk;
    }
    return true;
}

static bool ReadPhysU32(uint64_t pa, uint32_t* out) {
    return ReadPhys(pa, out, 4);
}
static bool ReadPhysU64(uint64_t pa, uint64_t* out) {
    return ReadPhys(pa, out, 8);
}

// ── 4-level page table walk (cached at page granularity) ───────────────────
static bool VirtToPhysRaw(uint64_t cr3, uint64_t va, uint64_t* out_pa) {
    uint64_t pml4_idx = (va >> 39) & 0x1FF;
    uint64_t pdpt_idx = (va >> 30) & 0x1FF;
    uint64_t pd_idx   = (va >> 21) & 0x1FF;
    uint64_t pt_idx   = (va >> 12) & 0x1FF;

    uint64_t pml4e = 0;
    if (!ReadPhysU64((cr3 & PAGE_MASK_4K) + pml4_idx * 8, &pml4e)) return false;
    if (!(pml4e & 1)) return false;

    uint64_t pdpte = 0;
    if (!ReadPhysU64((pml4e & PAGE_MASK_4K) + pdpt_idx * 8, &pdpte)) return false;
    if (!(pdpte & 1)) return false;
    if (pdpte & 0x80) {
        *out_pa = (pdpte & PAGE_MASK_1G) | (va & 0x3FFFFFFFULL);
        return true;
    }

    uint64_t pde = 0;
    if (!ReadPhysU64((pdpte & PAGE_MASK_4K) + pd_idx * 8, &pde)) return false;
    if (!(pde & 1)) return false;
    if (pde & 0x80) {
        *out_pa = (pde & PAGE_MASK_2M) | (va & 0x1FFFFFULL);
        return true;
    }

    uint64_t pte = 0;
    if (!ReadPhysU64((pde & PAGE_MASK_4K) + pt_idx * 8, &pte)) return false;
    if (!(pte & 1)) return false;

    *out_pa = (pte & PAGE_MASK_4K) | (va & 0xFFFULL);
    return true;
}

static bool VirtToPhys(uint64_t cr3, uint64_t va, uint64_t* out_pa) {
    const uint64_t va_page = va & ~0xFFFULL;
    const uint32_t epoch = g_VtopEpoch.load(std::memory_order_relaxed);
    // Hash both VA and CR3 so different CR3s land in different home slots.
    const size_t h = VtopHash(va_page ^ (cr3 >> 12));

    // Lookup: linear-probe 8 slots. Must match va_page AND cr3 AND epoch.
    // The va_page-last store ordering in the install path guarantees that
    // if we read a non-empty va_page, the pa_page and cr3_tag for that slot
    // are already visible to us.
    for (size_t probe = 0; probe < 8; ++probe) {
        size_t i = (h + probe) & (kVtopSlotCount - 1);
        uint64_t slot_va = g_VtopSlots[i].va_page.load(std::memory_order_acquire);
        if (slot_va == va_page &&
            g_VtopSlots[i].cr3_tag.load(std::memory_order_acquire) == cr3 &&
            g_VtopSlots[i].epoch.load(std::memory_order_acquire) == epoch) {
            *out_pa = g_VtopSlots[i].pa_page.load(std::memory_order_acquire) |
                      (va & 0xFFFULL);
            return true;
        }
        if (slot_va == 0) break;  // empty slot — definitively a miss
    }

    // Miss — do the actual walk.
    uint64_t pa = 0;
    if (!VirtToPhysRaw(cr3, va, &pa)) return false;
    *out_pa = pa;

    // Best-effort install. Prefer empty or stale-epoch slots; if all 8 probes
    // are taken by live entries, skip the install — the next call will redo
    // the walk, but we don't want to evict hot entries for cold ones.
    for (size_t probe = 0; probe < 8; ++probe) {
        size_t i = (h + probe) & (kVtopSlotCount - 1);
        uint64_t slot_va = g_VtopSlots[i].va_page.load(std::memory_order_relaxed);
        uint32_t slot_epoch = g_VtopSlots[i].epoch.load(std::memory_order_relaxed);
        if (slot_va == 0 || slot_epoch != epoch) {
            // Order: write pa/cr3/epoch FIRST, then va_page LAST. A concurrent
            // reader that observes the new va_page is guaranteed (via acquire)
            // to see the matching pa/cr3/epoch.
            g_VtopSlots[i].pa_page.store(pa & ~0xFFFULL, std::memory_order_release);
            g_VtopSlots[i].cr3_tag.store(cr3, std::memory_order_release);
            g_VtopSlots[i].epoch.store(epoch, std::memory_order_release);
            g_VtopSlots[i].va_page.store(va_page, std::memory_order_release);
            break;
        }
    }
    return true;
}

static bool VRead(uint64_t cr3, uint64_t va, void* buf, size_t len) {
    uint8_t* out = (uint8_t*)buf;
    size_t pos = 0;
    uint64_t cur = va;
    while (pos < len) {
        size_t off   = (size_t)(cur & 0xFFF);
        size_t chunk = (len - pos) < (0x1000 - off) ? (len - pos) : (0x1000 - off);
        uint64_t pa = 0;
        if (!VirtToPhys(cr3, cur, &pa)) return false;
        if (!ReadPhys(pa, out + pos, chunk)) return false;
        pos += chunk;
        cur += chunk;
    }
    return true;
}

static bool VWrite(uint64_t cr3, uint64_t va, const void* buf, size_t len) {
    const uint8_t* in = (const uint8_t*)buf;
    size_t pos = 0;
    uint64_t cur = va;
    while (pos < len) {
        size_t off   = (size_t)(cur & 0xFFF);
        size_t chunk = (len - pos) < (0x1000 - off) ? (len - pos) : (0x1000 - off);
        uint64_t pa = 0;
        if (!VirtToPhys(cr3, cur, &pa)) return false;
        if (!WritePhys(pa, in + pos, chunk)) return false;
        pos += chunk;
        cur += chunk;
    }
    return true;
}

static bool VReadU32(uint64_t cr3, uint64_t va, uint32_t* out) { return VRead(cr3, va, out, 4); }
static bool VReadU64(uint64_t cr3, uint64_t va, uint64_t* out) { return VRead(cr3, va, out, 8); }

// ── CR3 discovery (System) ─────────────────────────────────────────────────
static bool FindSystemCr3(uint64_t* out) {
    const uint64_t kusd_pml4_idx = (KUSD_VA >> 39) & 0x1FF;

    std::vector<uint64_t> candidates;
    candidates.reserve(64);

    // Phase 1 — collect PML4 candidates from low 64 MiB
    for (uint64_t page = 0; page < 0x4000000ULL; page += 0x1000) {
        uint64_t pml4e = 0;
        if (!ReadPhysU64(page + kusd_pml4_idx * 8, &pml4e)) continue;
        if (!(pml4e & 1)) continue;
        uint64_t next_pa = pml4e & PAGE_MASK_4K;
        if (next_pa > 0x800000000ULL) continue;
        candidates.push_back(page);
        if (candidates.size() >= 128) break;
    }

    // Phase 2 — verify by translating KUSD and checking NtMajorVersion
    for (uint64_t cr3 : candidates) {
        uint64_t kusd_pa = 0;
        if (!VirtToPhys(cr3, KUSD_VA, &kusd_pa)) continue;
        uint32_t mver = 0;
        if (!ReadPhysU32(kusd_pa + 0x26C, &mver)) continue;
        if (mver == 10) { *out = cr3; return true; }
    }
    return false;
}

// ── Read IA32_LSTAR via driver ─────────────────────────────────────────────
static bool ReadMsrLSTAR(uint64_t* out) {
    uint8_t io[8] = {};
    uint32_t idx = IA32_LSTAR;
    memcpy(io, &idx, 4);
    DWORD ret = 0;
    if (!DeviceIoControl(g_Device, IOCTL_READ_MSR,
                         io, 4, io, 8, &ret, nullptr)) return false;
    memcpy(out, io, 8);
    return true;
}

// ── ntoskrnl base from LSTAR walkback ──────────────────────────────────────
static bool FindKernelBase(uint64_t cr3, uint64_t lstar, uint64_t* out) {
    uint64_t start = lstar & ~0xFFFULL;
    for (uint64_t i = 0; i < 0x4000ULL; ++i) {
        uint64_t va = start - i * 0x1000ULL;
        if (!IsKernelPtr(va)) break;
        uint64_t pa = 0;
        if (!VirtToPhys(cr3, va, &pa)) continue;
        uint8_t hdr[0x200];
        if (!ReadPhys(pa, hdr, sizeof(hdr))) continue;
        if (hdr[0] != 'M' || hdr[1] != 'Z') continue;
        uint32_t lfn = *(uint32_t*)(hdr + 0x3C);
        if (lfn + 0x54 > sizeof(hdr)) continue;
        if (memcmp(hdr + lfn, "PE\0\0", 4) != 0) continue;
        if (*(uint16_t*)(hdr + lfn + 4) != 0x8664) continue;
        if (*(uint16_t*)(hdr + lfn + 24) != 0x20B) continue;
        uint32_t sz = *(uint32_t*)(hdr + lfn + 0x50);
        if (sz < 0x100000) continue;
        if (va + sz <= lstar) continue;
        *out = va;
        return true;
    }
    return false;
}

// ── Resolve PsInitialSystemProcess RVA from on-disk ntoskrnl ───────────────
static bool ResolveKernelExportRva(const char* name, uint64_t* out_rva) {
    HMODULE h = LoadLibraryExA("ntoskrnl.exe", nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!h) h = LoadLibraryExA("ntkrnlmp.exe", nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!h) return false;
    FARPROC p = GetProcAddress(h, name);
    if (!p) { FreeLibrary(h); return false; }
    *out_rva = (uint64_t)p - (uint64_t)h;
    FreeLibrary(h);
    return true;
}

// ── Build-version EPROCESS offsets ─────────────────────────────────────────
static bool DetectEprocessOffsets(EprocOffsets* o) {
    OSVERSIONINFOEXW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    using RtlGetVersion_t = NTSTATUS(NTAPI*)(PRTL_OSVERSIONINFOEXW);
    auto fn = (RtlGetVersion_t)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (!fn || fn(&info) != 0) return false;
    DWORD b = info.dwBuildNumber;

    // _KPROCESS embeds at offset 0 of EPROCESS. DirectoryTableBase is at 0x28
    // on every x64 NT build; that's the *kernel-mode* CR3 when KVA-shadowing
    // (Meltdown mitigation) is on. The user-mode CR3, which actually maps the
    // process's user VA space, is at UserDirectoryTableBase. Offset depends
    // on build — but it's always somewhere in the 0x280..0x3C0 range and is
    // page-aligned, so we fall back to scanning if the hard-coded slot is 0.
    o->dirbase = 0x28;
    if (b >= 26100) {
        // Win11 24H2 / Server 2025
        o->pid = 0x1D0; o->links = 0x1D8; o->token = 0x248; o->image_name = 0x338;
        o->user_dirbase = 0x388;
    } else if (b >= 22000) {
        // Win11 21H2 / 22H2 / 23H2
        o->pid = 0x440; o->links = 0x448; o->token = 0x4B8; o->image_name = 0x5A8;
        o->user_dirbase = 0x388;
    } else if (b >= 19041) {
        // Win10 2004+ / 21H1 / 21H2 / 22H2
        o->pid = 0x440; o->links = 0x448; o->token = 0x4B8; o->image_name = 0x5A8;
        o->user_dirbase = 0x388;
    } else {
        return false;
    }
    return true;
}

// Verify a candidate CR3 by translating KUSER_SHARED_DATA (mapped at the
// same VA in every user-mode process) and checking NtMajorVersion == 10.
static bool ValidateUserCr3(uint64_t cr3) {
    if (cr3 == 0 || (cr3 & 0xFFF)) return false;
    uint64_t pa = 0;
    if (!VirtToPhys(cr3, KUSD_VA, &pa)) return false;
    uint32_t ver = 0;
    if (!ReadPhysU32(pa + 0x26C, &ver)) return false;
    return ver == 10;
}

// Pick the right CR3 for translating target-process user VAs. On Win10/11
// with KVA-shadowing on (the default), the kernel CR3 at EPROCESS+0x28 maps
// only a thin syscall trampoline of user memory; the real user CR3 lives at
// _KPROCESS.UserDirectoryTableBase. That field's offset shifts across builds
// (0x280..0x3C0 historically), so instead of trusting a hard-coded value we
// scan candidate offsets and validate each by translating KUSER_SHARED_DATA.
// Falls back to the kernel CR3 at 0x28 if no valid user CR3 found.
static uint64_t PickTargetCr3(uint64_t eproc) {
    // First try kernel CR3 — on systems without KVA shadow this IS the user CR3.
    uint64_t kdb = 0;
    if (VReadU64(g_SystemCr3, eproc + g_EpOffs.dirbase, &kdb)) {
        kdb &= ~0xFFFULL;
        if (ValidateUserCr3(kdb)) return kdb;
    }
    // Then try the hint we set per-build (most common: 0x388).
    if (g_EpOffs.user_dirbase) {
        uint64_t udb = 0;
        if (VReadU64(g_SystemCr3, eproc + g_EpOffs.user_dirbase, &udb)) {
            udb &= ~0xFFFULL;
            if (ValidateUserCr3(udb)) return udb;
        }
    }
    // Brute-force scan of plausible offsets within KPROCESS for a CR3 that
    // translates KUSER_SHARED_DATA cleanly. KPROCESS layout doesn't change
    // much between patch levels, so the right slot is always in this range.
    for (uint64_t off = 0x280; off <= 0x3F0; off += 8) {
        uint64_t cand = 0;
        if (!VReadU64(g_SystemCr3, eproc + off, &cand)) continue;
        cand &= ~0xFFFULL;
        if (ValidateUserCr3(cand)) return cand;
    }
    // Last resort: return kernel CR3 even if it didn't validate. Caller will
    // still try reads; some pages (syscall trampoline) translate either way.
    return kdb;
}

// ── EPROCESS walk to find target PID ───────────────────────────────────────
static bool FindEprocessByPid(DWORD pid, uint64_t* out_eproc, uint64_t* out_cr3) {
    uint64_t psisp_rva = 0;
    if (!ResolveKernelExportRva("PsInitialSystemProcess", &psisp_rva)) return false;
    uint64_t psisp_va = g_KernelBase + psisp_rva;

    uint64_t sys_va = 0;
    if (!VReadU64(g_SystemCr3, psisp_va, &sys_va)) return false;
    if (!IsKernelPtr(sys_va)) return false;
    g_SystemEproc = sys_va;

    uint64_t sys_pid = 0;
    if (!VReadU64(g_SystemCr3, sys_va + g_EpOffs.pid, &sys_pid)) return false;
    if (sys_pid != 4) return false;

    if (pid == 4) {
        *out_eproc = sys_va;
        *out_cr3   = PickTargetCr3(sys_va);
        return true;
    }

    // PsActiveProcessHead is referenced from System.ActiveProcessLinks.Blink
    uint64_t head_va = 0;
    if (!VReadU64(g_SystemCr3, sys_va + g_EpOffs.links + 8, &head_va)) return false;
    if (!IsKernelPtr(head_va)) return false;

    uint64_t head_flink = 0;
    if (!VReadU64(g_SystemCr3, head_va, &head_flink)) return false;

    uint64_t cur = head_flink;
    for (int i = 0; i < 4096; ++i) {
        if (cur == head_va || !IsKernelPtr(cur)) break;
        uint64_t ep = cur - g_EpOffs.links;
        uint64_t cur_pid = 0;
        if (VReadU64(g_SystemCr3, ep + g_EpOffs.pid, &cur_pid)) {
            if ((DWORD)cur_pid == pid) {
                uint64_t cr3 = PickTargetCr3(ep);
                if (cr3 == 0) return false;
                *out_eproc = ep;
                *out_cr3   = cr3;
                return true;
            }
        }
        uint64_t nxt = 0;
        if (!VReadU64(g_SystemCr3, cur, &nxt)) break;
        if (!IsKernelPtr(nxt)) break;
        cur = nxt;
    }
    return false;
}

} // anon

// =============================================================================
//  Public API
// =============================================================================

HANDLE connect_driver() {
    if (g_Device != INVALID_HANDLE_VALUE) return g_Device;

    g_Device = CreateFileW(DEVICE_PATH,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_Device == INVALID_HANDLE_VALUE) {
        DWORD le = GetLastError();
        LOG_ERROR(TAG_DRV,
            "connect_driver: open \\\\.\\Astra32Device0 failed err=%lu — "
            "ASTRA64.sys not loaded?", le);
        return INVALID_HANDLE_VALUE;
    }
    LOG_INFO(TAG_DRV, "connect_driver: ASTRA64 device open");

    // Bootstrap: System CR3, kernel base, EPROCESS offsets.
    if (!DetectEprocessOffsets(&g_EpOffs)) {
        LOG_ERROR(TAG_DRV, "connect_driver: unsupported Windows build");
        CloseHandle(g_Device); g_Device = INVALID_HANDLE_VALUE;
        return INVALID_HANDLE_VALUE;
    }

    if (!FindSystemCr3(&g_SystemCr3)) {
        LOG_ERROR(TAG_DRV, "connect_driver: System CR3 scan failed");
        CloseHandle(g_Device); g_Device = INVALID_HANDLE_VALUE;
        return INVALID_HANDLE_VALUE;
    }
    LOG_INFO(TAG_DRV, "connect_driver: System CR3 = 0x%llX",
             (unsigned long long)g_SystemCr3);

    uint64_t lstar = 0;
    if (!ReadMsrLSTAR(&lstar)) {
        LOG_ERROR(TAG_DRV, "connect_driver: MSR LSTAR read failed");
        CloseHandle(g_Device); g_Device = INVALID_HANDLE_VALUE;
        return INVALID_HANDLE_VALUE;
    }
    if (!FindKernelBase(g_SystemCr3, lstar, &g_KernelBase)) {
        LOG_ERROR(TAG_DRV, "connect_driver: ntoskrnl base walkback failed (LSTAR=0x%llX)",
                  (unsigned long long)lstar);
        CloseHandle(g_Device); g_Device = INVALID_HANDLE_VALUE;
        return INVALID_HANDLE_VALUE;
    }
    LOG_INFO(TAG_DRV, "connect_driver: ntoskrnl base = 0x%llX",
             (unsigned long long)g_KernelBase);
    LOG_INFO(TAG_DRV, "connect_driver: ASTRA64 channel ready");
    return g_Device;
}

void disconnect_driver() {
    if (g_Device != INVALID_HANDLE_VALUE) {
        CloseHandle(g_Device);
        g_Device = INVALID_HANDLE_VALUE;
    }
    g_TargetPid = 0;
    g_TargetEproc = 0;
    g_TargetCr3 = 0;
    ClearVtopCache();
    ClearPhysCache();
}

const char* get_driver_label() {
    return (g_Device != INVALID_HANDLE_VALUE) ? "ASTRA64" : "-";
}

PVOID get_proc_base(HANDLE hDriver, DWORD Pid) {
    UNREFERENCED_PARAMETER(Pid);
    UNREFERENCED_PARAMETER(hDriver);

    // Fast path: pull the image base out of _EPROCESS.SectionBaseAddress.
    // That field lives somewhere in [0x500..0x700] across Windows versions:
    //   - Win10 19041-22000: ~0x520
    //   - Win11 22000-26099: ~0x520-0x5B0
    //   - Win11 26100+ (24H2/25H2): ~0x5C0-0x650
    // Instead of hard-coding a per-build offset, scan the full window for a
    // QWORD that's (a) page-aligned, (b) in user-VA range, (c) translates
    // under target CR3, (d) starts with the MZ magic.
    if (g_TargetEproc && g_TargetCr3) {
        for (uint64_t off = 0x500; off <= 0x700; off += 8) {
            uint64_t cand = 0;
            if (!VReadU64(g_SystemCr3, g_TargetEproc + off, &cand)) continue;
            if (cand == 0) continue;
            if (cand & 0xFFF) continue;                            // not page-aligned
            if (cand < 0x10000 || cand >= 0x800000000000ULL) continue; // not user VA
            uint16_t mz = 0;
            if (!VRead(g_TargetCr3, cand, &mz, sizeof(mz))) continue;
            if (mz != 0x5A4D) continue;
            LOG_INFO(TAG_DRV,
                "get_proc_base: SectionBaseAddress @ EPROC+0x%llX = 0x%llX",
                (unsigned long long)off, (unsigned long long)cand);
            return (PVOID)(uintptr_t)cand;
        }
    }

    // Fallback: brute scan a wide user-VA range looking for MZ. Step by 64 KiB
    // (Windows allocation granularity) to catch any image base. CS2 typically
    // loads around 0x7FF6xxxxx000 - 0x7FF7xxxxx000.
    for (uintptr_t addr = 0x7FF400000000ULL; addr < 0x7FF800000000ULL; addr += 0x10000) {
        uint16_t mz = 0;
        if (read_proc_memory(hDriver, (uintptr_t)g_TargetPid,
                             (PVOID)addr, sizeof(mz), &mz) && mz == 0x5A4D)
            return (PVOID)addr;
    }
    return nullptr;
}

uintptr_t open_process_via_driver(HANDLE /*hDriver*/, DWORD Pid) {
    if (g_Device == INVALID_HANDLE_VALUE) return 0;

    if (g_TargetPid == Pid && g_TargetCr3 != 0) return (uintptr_t)Pid;

    LOG_INFO(TAG_DRV, "open_process_via_driver: locating EPROCESS for PID %lu...", Pid);

    uint64_t eproc = 0, cr3 = 0;
    if (!FindEprocessByPid(Pid, &eproc, &cr3) || cr3 == 0) {
        LOG_ERROR(TAG_DRV,
            "open_process_via_driver: PID %lu not found via EPROCESS walk", Pid);
        return 0;
    }

    // New target → drop the VA→PA cache (entries from the previous CR3 are
    // poisonous). Physical-mapping cache stays — PAs stable across targets.
    ClearVtopCache();

    g_TargetPid   = Pid;
    g_TargetEproc = eproc;
    g_TargetCr3   = cr3;

    // Read the kernel CR3 too so log shows whether KVA shadow was active
    // (kernel != user → shadow on; kernel == user → shadow off).
    uint64_t kcr3 = 0;
    VReadU64(g_SystemCr3, eproc + g_EpOffs.dirbase, &kcr3);
    kcr3 &= ~0xFFFULL;
    LOG_INFO(TAG_DRV,
        "open_process_via_driver: PID %lu  EPROC=0x%llX  user_CR3=0x%llX  kernel_CR3=0x%llX",
        Pid, (unsigned long long)eproc,
        (unsigned long long)cr3, (unsigned long long)kcr3);
    return (uintptr_t)Pid;
}

DWORD read_proc_memory(HANDLE /*hDriver*/, uintptr_t TargetHandle,
                       PVOID Addr, DWORD Size, PVOID Out) {
    if (g_Device == INVALID_HANDLE_VALUE || !Addr || !Size || !Out || !TargetHandle) return 0;
    if (g_TargetCr3 == 0 || (DWORD)TargetHandle != g_TargetPid) return 0;

    if (!VRead(g_TargetCr3, (uint64_t)(uintptr_t)Addr, Out, Size)) {
        ++g_ReadFail;
        uint64_t nfail = g_ReadFail.load();
        if (nfail == 1 || (nfail % 1024) == 0) {
            LOG_ERROR(TAG_DRV,
                "read_proc_memory: VRead failed va=0x%llX size=%u "
                "(totals: ok=%llu fail=%llu)",
                (unsigned long long)(uintptr_t)Addr, Size,
                (unsigned long long)g_ReadOK.load(),
                (unsigned long long)nfail);
        }
        return 0;
    }
    ++g_ReadOK;
    return Size;
}

DWORD write_proc_memory(HANDLE /*hDriver*/, uintptr_t TargetHandle,
                        PVOID Addr, DWORD Size, PVOID In) {
    if (g_Device == INVALID_HANDLE_VALUE || !Addr || !Size || !In || !TargetHandle) return 0;
    if (g_TargetCr3 == 0 || (DWORD)TargetHandle != g_TargetPid) return 0;

    if (!VWrite(g_TargetCr3, (uint64_t)(uintptr_t)Addr, In, Size)) return 0;
    return Size;
}
