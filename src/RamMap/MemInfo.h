// MemInfo.h -- the physical-memory data engine for the RamMap recreation.
//
// RamMap categorises physical RAM by use type (the rows) against the kernel
// paging-list state (the columns). The original gets the detail from
// NtQuerySystemInformation with SystemSuperfetchInformation (class 79) and the
// per-page PFN database it walks, plus SystemMemoryListInformation (class 80)
// for the paging-list page counts including the per-priority standby lists.
// This engine reproduces the parts that NtQuerySystemInformation gives reliably
// in user mode with no kernel driver:
//
//   - the paging-list state totals (Standby by priority, Modified, Modified
//     No-Write, Zeroed, Free, Bad) from SystemMemoryListInformation,
//   - the physical totals from GlobalMemoryStatusEx,
//   - pool, driver and cache type rows from SystemPerformanceInformation,
//   - the physical memory ranges from the hardware resource map.
//
// The full per-type by per-state decomposition and the exact per-physical-page
// owner (the Physical Pages tab) need the Superfetch PFN-database enumeration
// and the RAMMap.sys helper driver respectively, which are out of scope here.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <array>

#pragma comment(lib, "advapi32.lib")

namespace ram
{
    // The sixteen use types, in the exact order the original's table uses
    // (off_14003EA60 in RAMMap64.exe).
    inline constexpr int kTypeCount = 16;

    [[nodiscard]] inline const wchar_t* TypeName(int i)
    {
        static const wchar_t* const names[kTypeCount] = {
            L"Process Private", L"Mapped File", L"Shareable", L"Page Table",
            L"Paged Pool", L"Nonpaged Pool", L"System PTE", L"Session Private",
            L"Metafile", L"AWE", L"Driver Locked", L"Kernel Stack",
            L"Unused", L"Large Page", L"Reserved1", L"Reserved2" };
        return (i >= 0 && i < kTypeCount) ? names[i] : L"";
    }

    // The nine paging-list state columns (col 0 is the type label, handled by
    // the view). Order matches the original's column table at 0x14003E940.
    enum State : int
    {
        kTotal = 0, kActive, kStandby, kModified, kModifiedNoWrite,
        kTransition, kZeroed, kFree, kBad, kStateCount
    };

    [[nodiscard]] inline const wchar_t* StateName(int i)
    {
        static const wchar_t* const names[kStateCount] = {
            L"Total", L"Active", L"Standby", L"Modified", L"Modified no write",
            L"Transition", L"Zeroed", L"Free", L"Bad" };
        return (i >= 0 && i < kStateCount) ? names[i] : L"";
    }

    struct UseRow
    {
        std::array<uint64_t, kStateCount> v{};   // bytes
    };

    struct Range
    {
        uint64_t start = 0;   // physical byte address
        uint64_t size = 0;    // bytes
    };

    struct Snapshot
    {
        uint64_t totalPhysBytes = 0;
        uint64_t availBytes = 0;
        uint32_t pageSize = 4096;

        std::array<UseRow, kTypeCount> types{};   // per-type matrix body
        UseRow total{};                           // the aggregate Total row

        std::array<uint64_t, 8> standbyByPrio{};      // bytes, per standby priority
        std::array<uint64_t, 8> repurposedByPrio{};   // bytes, per priority
        uint64_t standbyTotal = 0;
        uint64_t repurposedTotal = 0;

        std::vector<Range> ranges;
        uint64_t rangesTotal = 0;

        bool ok = false;
    };

    // ---- undocumented NT system-information plumbing ---------------------
    namespace detail
    {
        enum SYSINFOCLASS
        {
            SystemPerformanceInformation = 2,
            SystemMemoryListInformation = 0x50
        };

        // SystemMemoryListInformation (class 0x50) output.
        struct SYSTEM_MEMORY_LIST_INFORMATION
        {
            ULONG_PTR ZeroPageCount;
            ULONG_PTR FreePageCount;
            ULONG_PTR ModifiedPageCount;
            ULONG_PTR ModifiedNoWritePageCount;
            ULONG_PTR BadPageCount;
            ULONG_PTR PageCountByPriority[8];
            ULONG_PTR RepurposedPagesByPriority[8];
            ULONG_PTR ModifiedPageCountPageFile;
        };

        // SystemMemoryListInformation (class 0x50) input for the Empty commands.
        enum MEMORY_LIST_COMMAND : int
        {
            MemoryEmptyWorkingSets = 2,
            MemoryFlushModifiedList = 3,
            MemoryPurgeStandbyList = 4,
            MemoryPurgeLowPriorityStandbyList = 5
        };

        // SystemPerformanceInformation (class 2) output. Only the leading
        // fields, which are stable across Windows versions, are declared.
        struct SYSTEM_PERFORMANCE_INFORMATION
        {
            LARGE_INTEGER IdleProcessTime;
            LARGE_INTEGER IoReadTransferCount;
            LARGE_INTEGER IoWriteTransferCount;
            LARGE_INTEGER IoOtherTransferCount;
            ULONG IoReadOperationCount;
            ULONG IoWriteOperationCount;
            ULONG IoOtherOperationCount;
            ULONG AvailablePages;
            ULONG CommittedPages;
            ULONG CommitLimit;
            ULONG PeakCommitment;
            ULONG PageFaultCount;
            ULONG CopyOnWriteCount;
            ULONG TransitionCount;
            ULONG CacheTransitionCount;
            ULONG DemandZeroCount;
            ULONG PageReadCount;
            ULONG PageReadIoCount;
            ULONG CacheReadCount;
            ULONG CacheIoCount;
            ULONG DirtyPagesWriteCount;
            ULONG DirtyWriteIoCount;
            ULONG MappedPagesWriteCount;
            ULONG MappedWriteIoCount;
            ULONG PagedPoolPages;
            ULONG NonPagedPoolPages;
            ULONG PagedPoolAllocs;
            ULONG PagedPoolFrees;
            ULONG NonPagedPoolAllocs;
            ULONG NonPagedPoolFrees;
            ULONG FreeSystemPtes;
            ULONG ResidentSystemCodePage;
            ULONG TotalSystemDriverPages;
            ULONG TotalSystemCodePages;
            ULONG NonPagedPoolLookasideHits;
            ULONG PagedPoolLookasideHits;
            ULONG AvailablePagedPoolPages;
            ULONG ResidentSystemCachePage;
            ULONG ResidentPagedPoolPage;
            ULONG ResidentSystemDriverPage;
        };

        using NtQSI = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
        using NtSSI = LONG(NTAPI*)(ULONG, PVOID, ULONG);

        [[nodiscard]] inline NtQSI QuerySysInfo()
        {
            static NtQSI fn = reinterpret_cast<NtQSI>(::GetProcAddress(
                ::GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
            return fn;
        }
        [[nodiscard]] inline NtSSI SetSysInfo()
        {
            static NtSSI fn = reinterpret_cast<NtSSI>(::GetProcAddress(
                ::GetModuleHandleW(L"ntdll.dll"), "NtSetSystemInformation"));
            return fn;
        }

        inline bool EnablePrivilege(const wchar_t* name)
        {
            HANDLE raw = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(),
                                    TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw))
            {
                return false;
            }
            TOKEN_PRIVILEGES tp{};
            tp.PrivilegeCount = 1;
            bool ok = false;
            if (::LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid))
            {
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                ok = ::AdjustTokenPrivileges(raw, FALSE, &tp, 0, nullptr, nullptr) != 0
                     && ::GetLastError() == ERROR_SUCCESS;
            }
            ::CloseHandle(raw);
            return ok;
        }
    }

    // The three privileges the original raises before any memory query.
    inline bool EnablePrivileges()
    {
        const bool a = detail::EnablePrivilege(L"SeProfileSingleProcessPrivilege");
        const bool b = detail::EnablePrivilege(L"SeDebugPrivilege");
        const bool c = detail::EnablePrivilege(L"SeIncreaseQuotaPrivilege");
        return a && b && c;
    }

    // ---- physical memory ranges from the hardware resource map ----------
    namespace detail
    {
#pragma pack(push, 4)
        struct CM_PARTIAL_RESOURCE_DESCRIPTOR
        {
            BYTE Type;
            BYTE ShareDisposition;
            WORD Flags;
            // On x64 the union is sized to its largest member (the interrupt
            // affinity variant, 16 bytes), so the whole descriptor is 20 bytes.
            // The stride between descriptors must match that, not the 12-byte
            // Memory variant.
            union
            {
                struct { LARGE_INTEGER Start; DWORD Length; } Memory;
                BYTE raw[16];
            } u;
        };
        struct CM_PARTIAL_RESOURCE_LIST
        {
            WORD Version;
            WORD Revision;
            DWORD Count;
            CM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptors[1];
        };
        struct CM_FULL_RESOURCE_DESCRIPTOR
        {
            DWORD InterfaceType;
            DWORD BusNumber;
            CM_PARTIAL_RESOURCE_LIST PartialResourceList;
        };
        struct CM_RESOURCE_LIST
        {
            DWORD Count;
            CM_FULL_RESOURCE_DESCRIPTOR List[1];
        };
#pragma pack(pop)

        constexpr BYTE kCmResourceTypeMemory = 3;
        constexpr BYTE kCmResourceTypeMemoryLarge = 7;
        constexpr WORD kMemoryLarge40 = 0x0200;
        constexpr WORD kMemoryLarge48 = 0x0400;
        constexpr WORD kMemoryLarge64 = 0x0800;

        inline void ReadPhysicalRanges(Snapshot& s)
        {
            HKEY key = nullptr;
            if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\RESOURCEMAP\\System Resources\\Physical Memory",
                    0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
            {
                return;
            }
            DWORD cb = 0;
            DWORD type = 0;
            if (::RegQueryValueExW(key, L".Translated", nullptr, &type, nullptr, &cb) != ERROR_SUCCESS
                || cb < sizeof(CM_RESOURCE_LIST))
            {
                ::RegCloseKey(key);
                return;
            }
            std::vector<BYTE> buf(cb);
            if (::RegQueryValueExW(key, L".Translated", nullptr, &type, buf.data(), &cb) != ERROR_SUCCESS)
            {
                ::RegCloseKey(key);
                return;
            }
            ::RegCloseKey(key);

            const auto* list = reinterpret_cast<const CM_RESOURCE_LIST*>(buf.data());
            const BYTE* end = buf.data() + buf.size();
            const BYTE* p = reinterpret_cast<const BYTE*>(&list->List[0]);
            for (DWORD f = 0; f < list->Count && p + sizeof(CM_FULL_RESOURCE_DESCRIPTOR) <= end; ++f)
            {
                const auto* full = reinterpret_cast<const CM_FULL_RESOURCE_DESCRIPTOR*>(p);
                const DWORD count = full->PartialResourceList.Count;
                const auto* d = &full->PartialResourceList.PartialDescriptors[0];
                for (DWORD i = 0; i < count
                     && reinterpret_cast<const BYTE*>(d + 1) <= end; ++i, ++d)
                {
                    if (d->Type == kCmResourceTypeMemory || d->Type == kCmResourceTypeMemoryLarge)
                    {
                        uint64_t len = d->u.Memory.Length;
                        if (d->Type == kCmResourceTypeMemoryLarge)
                        {
                            if (d->Flags & kMemoryLarge40) { len <<= 8; }
                            else if (d->Flags & kMemoryLarge48) { len <<= 16; }
                            else if (d->Flags & kMemoryLarge64) { len <<= 32; }
                        }
                        Range r;
                        r.start = static_cast<uint64_t>(d->u.Memory.Start.QuadPart);
                        r.size = len;
                        s.rangesTotal += len;
                        s.ranges.push_back(r);
                    }
                }
                p = reinterpret_cast<const BYTE*>(d);
            }
        }
    }

    // Capture a system physical-memory snapshot.
    [[nodiscard]] inline Snapshot Collect()
    {
        Snapshot s;

        SYSTEM_INFO si{};
        ::GetSystemInfo(&si);
        s.pageSize = si.dwPageSize ? si.dwPageSize : 4096;
        const uint64_t ps = s.pageSize;

        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (::GlobalMemoryStatusEx(&ms))
        {
            s.totalPhysBytes = ms.ullTotalPhys;
            s.availBytes = ms.ullAvailPhys;
        }

        detail::NtQSI q = detail::QuerySysInfo();

        detail::SYSTEM_MEMORY_LIST_INFORMATION ml{};
        bool haveMl = false;
        if (q)
        {
            ULONG ret = 0;
            if (q(detail::SystemMemoryListInformation, &ml, sizeof(ml), &ret) >= 0)
            {
                haveMl = true;
            }
        }

        detail::SYSTEM_PERFORMANCE_INFORMATION perf{};
        bool havePerf = false;
        if (q)
        {
            // Over-allocate: the OS struct is longer than the declared prefix.
            std::vector<BYTE> pbuf(0x600, 0);
            ULONG ret = 0;
            if (q(detail::SystemPerformanceInformation, pbuf.data(),
                  static_cast<ULONG>(pbuf.size()), &ret) >= 0
                && ret >= sizeof(perf))
            {
                memcpy(&perf, pbuf.data(), sizeof(perf));
                havePerf = true;
            }
        }

        const uint64_t totalPages = ps ? (s.totalPhysBytes / ps) : 0;

        uint64_t standby = 0;
        if (haveMl)
        {
            for (int p = 0; p < 8; ++p)
            {
                const uint64_t sp = static_cast<uint64_t>(ml.PageCountByPriority[p]);
                const uint64_t rp = static_cast<uint64_t>(ml.RepurposedPagesByPriority[p]);
                s.standbyByPrio[p] = sp * ps;
                s.repurposedByPrio[p] = rp * ps;
                s.standbyTotal += s.standbyByPrio[p];
                s.repurposedTotal += s.repurposedByPrio[p];
                standby += sp;
            }
        }

        const uint64_t modified = haveMl ? static_cast<uint64_t>(ml.ModifiedPageCount) : 0;
        const uint64_t modNoWrite = haveMl ? static_cast<uint64_t>(ml.ModifiedNoWritePageCount) : 0;
        const uint64_t zeroed = haveMl ? static_cast<uint64_t>(ml.ZeroPageCount) : 0;
        const uint64_t freePages = haveMl ? static_cast<uint64_t>(ml.FreePageCount) : 0;
        const uint64_t bad = haveMl ? static_cast<uint64_t>(ml.BadPageCount) : 0;
        const uint64_t transition = 0;   // needs the Superfetch PFN walk

        uint64_t accounted = standby + modified + modNoWrite + zeroed + freePages + bad + transition;
        const uint64_t active = (totalPages > accounted) ? (totalPages - accounted) : 0;

        // The aggregate Total row, in bytes.
        s.total.v[kTotal] = totalPages * ps;
        s.total.v[kActive] = active * ps;
        s.total.v[kStandby] = standby * ps;
        s.total.v[kModified] = modified * ps;
        s.total.v[kModifiedNoWrite] = modNoWrite * ps;
        s.total.v[kTransition] = transition * ps;
        s.total.v[kZeroed] = zeroed * ps;
        s.total.v[kFree] = freePages * ps;
        s.total.v[kBad] = bad * ps;

        // Per-type rows that SystemPerformanceInformation exposes directly.
        // These land in the Active column, their natural residence. The rows
        // that need the Superfetch PFN enumeration stay at zero.
        auto setActive = [&](int typeIdx, uint64_t pages)
        {
            s.types[typeIdx].v[kActive] = pages * ps;
            s.types[typeIdx].v[kTotal] = pages * ps;
        };
        if (havePerf)
        {
            setActive(4, perf.PagedPoolPages);          // Paged Pool
            setActive(5, perf.NonPagedPoolPages);       // Nonpaged Pool
            setActive(10, perf.TotalSystemDriverPages); // Driver Locked (approx)
            setActive(8, perf.ResidentSystemCachePage); // Metafile (approx)
        }

        detail::ReadPhysicalRanges(s);

        s.ok = haveMl || s.totalPhysBytes != 0;
        return s;
    }

    // ---- Empty operations (NtSetSystemInformation) ----------------------
    inline bool EmptyMemoryList(int command)
    {
        detail::NtSSI set = detail::SetSysInfo();
        if (!set)
        {
            return false;
        }
        int cmd = command;
        return set(detail::SystemMemoryListInformation, &cmd, sizeof(cmd)) >= 0;
    }

    // Empty System Working Set: trim the system file cache working set, which
    // the original does through SetSystemFileCacheSize(-1, -1, 0).
    inline bool EmptySystemWorkingSet()
    {
        return ::SetSystemFileCacheSize(static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0) != 0;
    }
}
