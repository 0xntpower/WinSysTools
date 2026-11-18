// MemSnapshot.h -- the core snapshot engine for the VMMap recreation.
//
// Captures one process's virtual address space by walking it with
// VirtualQueryEx, reads physical-memory (working set) attributes per page with
// QueryWorkingSetEx, classifies each region into the same categories the
// original uses (Image, Mapped File, Shareable, Heap, Managed Heap, Stack,
// Private Data, Page Table, Unusable, Free), and aggregates per-category
// totals. Thread stacks come from each thread's TEB, heaps from the PEB heap
// list. This is the snapshot path only. The original's launch-and-trace
// timeline feature is out of scope.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace vm
{
    enum class Category : int
    {
        Total = 0,
        Image,
        MappedFile,
        Shareable,
        Heap,
        ManagedHeap,
        Stack,
        PrivateData,
        PageTable,
        Unusable,
        Free,
        Count
    };

    constexpr int kCategoryCount = static_cast<int>(Category::Count);

    [[nodiscard]] inline const wchar_t* CategoryName(Category c)
    {
        switch (c)
        {
        case Category::Total:
            return L"Total";
        case Category::Image:
            return L"Image";
        case Category::MappedFile:
            return L"Mapped File";
        case Category::Shareable:
            return L"Shareable";
        case Category::Heap:
            return L"Heap";
        case Category::ManagedHeap:
            return L"Managed Heap";
        case Category::Stack:
            return L"Stack";
        case Category::PrivateData:
            return L"Private Data";
        case Category::PageTable:
            return L"Page Table";
        case Category::Unusable:
            return L"Unusable";
        case Category::Free:
            return L"Free";
        default:
            return L"";
        }
    }

    struct Region
    {
        uint64_t base = 0;
        uint64_t size = 0;
        uint64_t committed = 0;
        uint64_t privateBytes = 0;
        uint64_t totalWs = 0;
        uint64_t privateWs = 0;
        uint64_t shareableWs = 0;
        uint64_t sharedWs = 0;
        uint64_t lockedWs = 0;
        Category category = Category::Free;
        DWORD protect = 0;
        DWORD state = 0;
        DWORD type = 0;
        std::wstring details;
    };

    struct CategoryTotal
    {
        uint64_t size = 0;
        uint64_t committed = 0;
        uint64_t privateBytes = 0;
        uint64_t totalWs = 0;
        uint64_t privateWs = 0;
        uint64_t shareableWs = 0;
        uint64_t sharedWs = 0;
        uint64_t lockedWs = 0;
        uint64_t largest = 0;
        uint32_t blocks = 0;
    };

    struct Snapshot
    {
        DWORD pid = 0;
        std::wstring processName;
        uint64_t committedTotal = 0;
        uint64_t privateTotal = 0;
        uint64_t workingSetTotal = 0;
        std::vector<Region> regions;
        std::array<CategoryTotal, kCategoryCount> totals{};
        bool ok = false;
        std::wstring error;
    };

    // RAII handle wrapper.
    class Handle
    {
      public:
        Handle() = default;
        explicit Handle(HANDLE h)
            : h_(h)
        {
        }
        ~Handle()
        {
            close();
        }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& o) noexcept
            : h_(o.h_)
        {
            o.h_ = nullptr;
        }
        Handle& operator=(Handle&& o) noexcept
        {
            if (this != &o)
            {
                close();
                h_ = o.h_;
                o.h_ = nullptr;
            }
            return *this;
        }
        [[nodiscard]] HANDLE get() const
        {
            return h_;
        }
        [[nodiscard]] bool valid() const
        {
            return h_ != nullptr && h_ != INVALID_HANDLE_VALUE;
        }
        void reset(HANDLE h)
        {
            close();
            h_ = h;
        }

      private:
        void close()
        {
            if (h_ != nullptr && h_ != INVALID_HANDLE_VALUE)
            {
                ::CloseHandle(h_);
            }
            h_ = nullptr;
        }
        HANDLE h_ = nullptr;
    };

    namespace detail
    {
        using NtQIPFn = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
        using NtQITFn = NTSTATUS(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);

        struct ThreadBasicInformation
        {
            NTSTATUS ExitStatus;
            PVOID TebBaseAddress;
            CLIENT_ID ClientId;
            ULONG_PTR AffinityMask;
            LONG Priority;
            LONG BasePriority;
        };

        [[nodiscard]] inline NtQIPFn NtQIP()
        {
            static NtQIPFn fn = reinterpret_cast<NtQIPFn>(
                ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
            return fn;
        }
        [[nodiscard]] inline NtQITFn NtQIT()
        {
            static NtQITFn fn = reinterpret_cast<NtQITFn>(
                ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
            return fn;
        }

        // Best-effort SeDebugPrivilege so more processes are readable without
        // an elevation prompt. A plain failure is fine for same-user targets.
        inline void EnableDebugPrivilege()
        {
            HANDLE raw = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &raw))
            {
                return;
            }
            Handle token(raw);
            TOKEN_PRIVILEGES tp{};
            tp.PrivilegeCount = 1;
            if (::LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid))
            {
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                ::AdjustTokenPrivileges(token.get(), FALSE, &tp, 0, nullptr, nullptr);
            }
        }

        // Map a kernel device path (\Device\HarddiskVolumeN\...) to a drive
        // letter path for display.
        [[nodiscard]] inline std::wstring DeviceToDrive(const std::wstring& devicePath)
        {
            wchar_t drives[256] = {};
            DWORD len = ::GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)) - 1, drives);
            for (const wchar_t* d = drives; d < drives + len && *d; d += wcslen(d) + 1)
            {
                wchar_t letter[3] = {d[0], L':', 0};
                wchar_t target[MAX_PATH] = {};
                if (::QueryDosDeviceW(letter, target, static_cast<DWORD>(std::size(target))) != 0)
                {
                    const size_t tl = wcslen(target);
                    if (devicePath.size() > tl && _wcsnicmp(devicePath.c_str(), target, tl) == 0 &&
                        devicePath[tl] == L'\\')
                    {
                        return std::wstring(letter) + devicePath.substr(tl);
                    }
                }
            }
            return devicePath;
        }

        [[nodiscard]] inline std::wstring MappedName(HANDLE proc, LPVOID addr)
        {
            wchar_t buf[MAX_PATH] = {};
            if (::GetMappedFileNameW(proc, addr, buf, static_cast<DWORD>(std::size(buf))) != 0)
            {
                return DeviceToDrive(buf);
            }
            return std::wstring();
        }

        // Thread-stack allocation bases, keyed so a region's AllocationBase
        // marks the whole stack reservation (guard pages included) as Stack.
        // Thread Environment Block bases are collected too, to label them.
        inline void CollectStacks(HANDLE proc, DWORD pid, std::vector<uint64_t>& stackBases,
                                  std::vector<uint64_t>& tebBases)
        {
            NtQITFn q = NtQIT();
            if (!q)
            {
                return;
            }
            Handle snap(::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
            if (!snap.valid())
            {
                return;
            }
            THREADENTRY32 te{};
            te.dwSize = sizeof(te);
            for (BOOL more = ::Thread32First(snap.get(), &te); more; more = ::Thread32Next(snap.get(), &te))
            {
                if (te.th32OwnerProcessID != pid)
                {
                    continue;
                }
                Handle th(::OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID));
                if (!th.valid())
                {
                    continue;
                }
                ThreadBasicInformation tbi{};
                if (q(th.get(), 0 /*ThreadBasicInformation*/, &tbi, sizeof(tbi), nullptr) != 0 || !tbi.TebBaseAddress)
                {
                    continue;
                }
                tebBases.push_back(reinterpret_cast<uint64_t>(tbi.TebBaseAddress));
                // NT_TIB.StackBase sits at offset 8 of the TEB.
                uint64_t stackBase = 0;
                SIZE_T got = 0;
                if (::ReadProcessMemory(proc, static_cast<char*>(tbi.TebBaseAddress) + 8, &stackBase, sizeof(stackBase),
                                        &got) &&
                    got == sizeof(stackBase) && stackBase != 0)
                {
                    MEMORY_BASIC_INFORMATION mbi{};
                    if (::VirtualQueryEx(proc, reinterpret_cast<LPCVOID>(stackBase - 1), &mbi, sizeof(mbi)) ==
                        sizeof(mbi))
                    {
                        stackBases.push_back(reinterpret_cast<uint64_t>(mbi.AllocationBase));
                    }
                }
            }
            std::sort(stackBases.begin(), stackBases.end());
            stackBases.erase(std::unique(stackBases.begin(), stackBases.end()), stackBases.end());
            std::sort(tebBases.begin(), tebBases.end());
            tebBases.erase(std::unique(tebBases.begin(), tebBases.end()), tebBases.end());
        }

        // Heap base addresses from the PEB heap list. Regions whose
        // AllocationBase matches a heap base are classified as Heap. Additional
        // heap segments beyond the primary base are not followed here and show
        // as Private Data.
        inline void CollectHeaps(HANDLE proc, std::vector<uint64_t>& heapBases, uint64_t& pebBase)
        {
            NtQIPFn q = NtQIP();
            if (!q)
            {
                return;
            }
            PROCESS_BASIC_INFORMATION pbi{};
            if (q(proc, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr) != 0 || !pbi.PebBaseAddress)
            {
                return;
            }
            pebBase = reinterpret_cast<uint64_t>(pbi.PebBaseAddress);
            // 64-bit PEB: NumberOfHeaps at 0xE8, ProcessHeaps pointer at 0xF0.
            auto* peb = reinterpret_cast<char*>(pbi.PebBaseAddress);
            DWORD count = 0;
            uint64_t heapsPtr = 0;
            SIZE_T got = 0;
            if (!::ReadProcessMemory(proc, peb + 0xE8, &count, sizeof(count), &got) || got != sizeof(count))
            {
                return;
            }
            if (!::ReadProcessMemory(proc, peb + 0xF0, &heapsPtr, sizeof(heapsPtr), &got) || got != sizeof(heapsPtr))
            {
                return;
            }
            if (count == 0 || count > 4096 || heapsPtr == 0)
            {
                return;
            }
            std::vector<uint64_t> arr(count, 0);
            if (::ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(heapsPtr), arr.data(), count * sizeof(uint64_t),
                                    &got))
            {
                for (uint64_t b : arr)
                {
                    if (b != 0)
                    {
                        heapBases.push_back(b);
                    }
                }
            }
            std::sort(heapBases.begin(), heapBases.end());
            heapBases.erase(std::unique(heapBases.begin(), heapBases.end()), heapBases.end());
        }

        inline void QueryWorkingSet(HANDLE proc, Region& r, DWORD pageSize)
        {
            if (r.state != MEM_COMMIT || pageSize == 0)
            {
                return;
            }
            const uint64_t pages = r.size / pageSize;
            constexpr size_t kBatch = 4096;
            std::vector<PSAPI_WORKING_SET_EX_INFORMATION> info;
            for (uint64_t i = 0; i < pages; i += kBatch)
            {
                const size_t n = static_cast<size_t>(std::min<uint64_t>(kBatch, pages - i));
                info.assign(n, PSAPI_WORKING_SET_EX_INFORMATION{});
                for (size_t j = 0; j < n; ++j)
                {
                    info[j].VirtualAddress = reinterpret_cast<PVOID>(r.base + (i + j) * pageSize);
                }
                if (!::QueryWorkingSetEx(proc, info.data(), static_cast<DWORD>(n * sizeof(info[0]))))
                {
                    continue;
                }
                for (size_t j = 0; j < n; ++j)
                {
                    const auto& b = info[j].VirtualAttributes;
                    if (!b.Valid)
                    {
                        continue;
                    }
                    r.totalWs += pageSize;
                    if (b.Shared)
                    {
                        r.shareableWs += pageSize;
                        if (b.ShareCount > 1)
                        {
                            r.sharedWs += pageSize;
                        }
                    }
                    else
                    {
                        r.privateWs += pageSize;
                    }
                    if (b.Locked)
                    {
                        r.lockedWs += pageSize;
                    }
                }
            }
        }
    } // namespace detail

    [[nodiscard]] inline std::wstring ProtectionText(const Region& r)
    {
        if (r.state == MEM_FREE)
        {
            return L"Free";
        }
        if (r.state == MEM_RESERVE)
        {
            return L"Reserved";
        }
        const DWORD base = r.protect & 0xFF;
        std::wstring s;
        switch (base)
        {
        case PAGE_NOACCESS:
            s = L"No access";
            break;
        case PAGE_READONLY:
            s = L"Read";
            break;
        case PAGE_READWRITE:
            s = L"Read/Write";
            break;
        case PAGE_WRITECOPY:
            s = L"Copy on Write";
            break;
        case PAGE_EXECUTE:
            s = L"Execute";
            break;
        case PAGE_EXECUTE_READ:
            s = L"Execute/Read";
            break;
        case PAGE_EXECUTE_READWRITE:
            s = L"Execute/Read/Write";
            break;
        case PAGE_EXECUTE_WRITECOPY:
            s = L"Execute/Copy on Write";
            break;
        default:
            s = L"Read/Write";
            break;
        }
        if (r.protect & PAGE_GUARD)
        {
            s += L"/Guard";
        }
        if (r.protect & PAGE_NOCACHE)
        {
            s += L"/No cache";
        }
        if (r.protect & PAGE_WRITECOMBINE)
        {
            s += L"/Write combine";
        }
        return s;
    }

    // Capture a snapshot of the process's address space.
    [[nodiscard]] inline Snapshot Capture(DWORD pid, const std::wstring& name)
    {
        Snapshot snap;
        snap.pid = pid;
        snap.processName = name;

        detail::EnableDebugPrivilege();

        Handle proc(::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
        if (!proc.valid())
        {
            proc.reset(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        }
        if (!proc.valid())
        {
            snap.error = L"Unable to open the process. It may require higher privileges.";
            return snap;
        }

        SYSTEM_INFO si{};
        ::GetSystemInfo(&si);
        const DWORD pageSize = si.dwPageSize ? si.dwPageSize : 4096;
        const uint64_t granularity = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
        const uint64_t maxAddr = reinterpret_cast<uint64_t>(si.lpMaximumApplicationAddress);

        std::vector<uint64_t> stackBases;
        std::vector<uint64_t> tebBases;
        std::vector<uint64_t> heapBases;
        uint64_t pebBase = 0;
        detail::CollectStacks(proc.get(), pid, stackBases, tebBases);
        detail::CollectHeaps(proc.get(), heapBases, pebBase);

        auto isStack = [&](uint64_t allocBase)
        {
            return std::binary_search(stackBases.begin(), stackBases.end(), allocBase);
        };
        auto heapIndex = [&](uint64_t allocBase) -> int
        {
            auto it = std::lower_bound(heapBases.begin(), heapBases.end(), allocBase);
            if (it != heapBases.end() && *it == allocBase)
            {
                return static_cast<int>(it - heapBases.begin());
            }
            return -1;
        };

        uint64_t addr = 0;
        MEMORY_BASIC_INFORMATION mbi{};
        while (addr <= maxAddr &&
               ::VirtualQueryEx(proc.get(), reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi))
        {
            Region r;
            r.base = reinterpret_cast<uint64_t>(mbi.BaseAddress);
            r.size = mbi.RegionSize;
            r.state = mbi.State;
            r.protect = (mbi.State == MEM_COMMIT) ? mbi.Protect : mbi.AllocationProtect;
            r.type = mbi.Type;
            const uint64_t allocBase = reinterpret_cast<uint64_t>(mbi.AllocationBase);

            if (mbi.State == MEM_COMMIT)
            {
                r.committed = r.size;
            }

            if (mbi.State == MEM_FREE)
            {
                r.category = Category::Free;
            }
            else if (mbi.Type == MEM_IMAGE)
            {
                r.category = Category::Image;
                r.details = detail::MappedName(proc.get(), mbi.AllocationBase);
            }
            else if (mbi.Type == MEM_MAPPED)
            {
                r.category = Category::MappedFile;
                r.details = detail::MappedName(proc.get(), mbi.AllocationBase);
                if (r.details.empty())
                {
                    r.category = Category::Shareable;
                }
            }
            else // MEM_PRIVATE
            {
                const int heapIdx = heapIndex(allocBase);
                if (isStack(allocBase))
                {
                    r.category = Category::Stack;
                    r.details = L"Thread Stack";
                }
                else if (heapIdx >= 0)
                {
                    r.category = Category::Heap;
                    wchar_t buf[32] = {};
                    swprintf(buf, 32, L"Heap ID %d", heapIdx);
                    r.details = buf;
                }
                else
                {
                    r.category = Category::PrivateData;
                    if (pebBase && r.base == pebBase)
                    {
                        r.details = L"Process Environment Block";
                    }
                    else if (std::binary_search(tebBases.begin(), tebBases.end(), r.base))
                    {
                        r.details = L"Thread Environment Block";
                    }
                }
            }

            if (r.type == MEM_PRIVATE && r.state == MEM_COMMIT)
            {
                r.privateBytes = r.committed;
            }

            detail::QueryWorkingSet(proc.get(), r, pageSize);

            snap.regions.push_back(std::move(r));

            const uint64_t next = r.base + r.size;
            if (next <= addr)
            {
                break;
            }
            addr = next;
        }

        // Aggregate into per-category totals. Free address space is split into a
        // usable Free part and a granularity-locked Unusable part.
        auto add = [&](Category c, const CategoryTotal& v)
        {
            CategoryTotal& t = snap.totals[static_cast<int>(c)];
            t.size += v.size;
            t.committed += v.committed;
            t.privateBytes += v.privateBytes;
            t.totalWs += v.totalWs;
            t.privateWs += v.privateWs;
            t.shareableWs += v.shareableWs;
            t.sharedWs += v.sharedWs;
            t.lockedWs += v.lockedWs;
            t.largest = std::max(t.largest, v.largest);
            t.blocks += v.blocks;
        };

        for (const Region& r : snap.regions)
        {
            if (r.category == Category::Free)
            {
                const uint64_t aligned = (r.base + granularity - 1) & ~(granularity - 1);
                uint64_t unusable = 0;
                if (aligned >= r.base + r.size)
                {
                    unusable = r.size;
                }
                else
                {
                    unusable = aligned - r.base;
                }
                CategoryTotal fu{};
                fu.size = (r.size > unusable) ? (r.size - unusable) : 0;
                fu.blocks = fu.size ? 1 : 0;
                fu.largest = fu.size;
                add(Category::Free, fu);
                if (unusable)
                {
                    CategoryTotal uu{};
                    uu.size = unusable;
                    uu.blocks = 1;
                    uu.largest = unusable;
                    add(Category::Unusable, uu);
                }
                continue;
            }

            CategoryTotal v{};
            v.size = r.size;
            v.committed = r.committed;
            v.privateBytes = r.privateBytes;
            v.totalWs = r.totalWs;
            v.privateWs = r.privateWs;
            v.shareableWs = r.shareableWs;
            v.sharedWs = r.sharedWs;
            v.lockedWs = r.lockedWs;
            v.largest = r.size;
            v.blocks = 1;
            add(r.category, v);
        }

        // The Total row sums the memory-type categories, excluding Free and
        // Unusable address space.
        for (int c = static_cast<int>(Category::Image); c <= static_cast<int>(Category::PageTable); ++c)
        {
            add(Category::Total, snap.totals[c]);
        }

        // Header figures come from the process memory counters for accuracy.
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (::GetProcessMemoryInfo(proc.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        {
            snap.privateTotal = pmc.PrivateUsage;
            snap.workingSetTotal = pmc.WorkingSetSize;
        }
        else
        {
            snap.privateTotal = snap.totals[static_cast<int>(Category::Total)].privateBytes;
            snap.workingSetTotal = snap.totals[static_cast<int>(Category::Total)].totalWs;
        }
        snap.committedTotal = snap.totals[static_cast<int>(Category::Total)].committed;

        snap.ok = true;
        return snap;
    }
} // namespace vm
