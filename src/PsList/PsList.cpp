// PsList -- lists running processes with CPU time, elapsed time, priority,
// thread and handle counts, and memory, plus per-thread detail and a process
// tree. Source-accurate C++20 console reconstruction of the Sysinternals
// PsList64.exe v1.41.
//
// Behaviour recovered with IDA (idalib) from PsList64.exe. All of the program
// logic lives in one large main (sub_140004410). The reconstruction keeps the
// exact column layout, titles, abbreviation key, thread-state and wait-reason
// tables, and message strings recovered from the binary.
//
// Recovered format strings (column layout, byte for byte):
//   default header  %-17s %5s %3s %3s %4s %6s %15s %15s
//   default row     %-17s %5d %3d %3d %4d %6d %15s %15s
//   -m header       %-17s %5s %7s %7s %7s %7s %8s %6s %4s
//   -m row          %-17s %5d %7llu %7I64d %7I64d %7I64d %8I64d %6I64d %4I64d
//   -t header       %-30s %5s %3s %3s %4s %7s %7s %7s
//   -t row          %-30s %5d %3d %3d %4d %7llu %7d %7d
//   thread header   %4s %3s %9s %16s %13s %13s %14s
//   thread row      %4d %3d %9I64d %16s %12s %13s %14s
//   time field      %2d:%02d:%02d.%03d
//
// Data source. The original reads the registry performance counters
// (HKEY_PERFORMANCE_DATA, local or remote through RegConnectRegistryA), mapping
// counter names such as "ID Process", "% User Time", "Elapsed Time",
// "Page File Bytes", "Pool Nonpaged Bytes", "Creating Process ID" to their
// perf-library indices. This reconstruction reads the same field set from
// NtQuerySystemInformation(SystemProcessInformation), which the task permits and
// which yields identical columns and field semantics for ordinary processes.
// The two differences that fall out of that choice are documented in
// analysis/PsList.md (the "Memory Compression" instance name and the perf-only
// elapsed values on the Idle and System pseudo-threads). Remote targets, which
// the original serves through the remote registry, are out of scope here and
// report a clear message.
//
// The shared PsTools front end (banner, EULA gate, error text, local identity)
// lives in ../common/PsTools.h.

#include "../common/PsTools.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Version-resource fields of PsList64.exe v1.41, used for the banner and the
    // EULA registry key. The original reads these from its own version resource
    // at run time. Fixed here, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsList",
        "1.41",
        "Process information lister",
        "Copyright (C) 2000-2023 Mark Russinovich",
        "Sysinternals - www.sysinternals.com",
    };

    constexpr char kApp[] = "PsList";

    // --- NtQuerySystemInformation layout ----------------------------------

    using NtStatus = LONG;
    constexpr NtStatus kStatusInfoLengthMismatch = static_cast<NtStatus>(0xC0000004);
    constexpr ULONG kSystemProcessInformation = 5;

    struct UnicodeStr
    {
        USHORT Length = 0;
        USHORT MaximumLength = 0;
        PWSTR Buffer = nullptr;
    };

    struct ClientId
    {
        HANDLE UniqueProcess = nullptr;
        HANDLE UniqueThread = nullptr;
    };

    struct SystemThreadInfo
    {
        LARGE_INTEGER KernelTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER CreateTime;
        ULONG WaitTime;
        PVOID StartAddress;
        ClientId Cid;
        LONG Priority;
        LONG BasePriority;
        ULONG ContextSwitches;
        ULONG ThreadState;
        ULONG WaitReason;
    };

    struct SystemProcessInfo
    {
        ULONG NextEntryOffset;
        ULONG NumberOfThreads;
        LARGE_INTEGER WorkingSetPrivateSize;
        ULONG HardFaultCount;
        ULONG NumberOfThreadsHighWatermark;
        ULONGLONG CycleTime;
        LARGE_INTEGER CreateTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER KernelTime;
        UnicodeStr ImageName;
        LONG BasePriority;
        HANDLE UniqueProcessId;
        HANDLE InheritedFromUniqueProcessId;
        ULONG HandleCount;
        ULONG SessionId;
        ULONG_PTR UniqueProcessKey;
        SIZE_T PeakVirtualSize;
        SIZE_T VirtualSize;
        ULONG PageFaultCount;
        SIZE_T PeakWorkingSetSize;
        SIZE_T WorkingSetSize;
        SIZE_T QuotaPeakPagedPoolUsage;
        SIZE_T QuotaPagedPoolUsage;
        SIZE_T QuotaPeakNonPagedPoolUsage;
        SIZE_T QuotaNonPagedPoolUsage;
        SIZE_T PagefileUsage;
        SIZE_T PeakPagefileUsage;
        SIZE_T PrivatePageCount;
        LARGE_INTEGER ReadOperationCount;
        LARGE_INTEGER WriteOperationCount;
        LARGE_INTEGER OtherOperationCount;
        LARGE_INTEGER ReadTransferCount;
        LARGE_INTEGER WriteTransferCount;
        LARGE_INTEGER OtherTransferCount;
    };

    using NtQuerySystemInformationFn =
        NtStatus(NTAPI*)(ULONG systemInformationClass, PVOID systemInformation,
                         ULONG systemInformationLength, PULONG returnLength);

    // --- Thread-state and wait-reason tables ------------------------------
    // Recovered verbatim from the original's string arrays (base 0x14003ace0
    // states, 0x14003a3a0 wait reasons). Index 5 of the states is the prefix the
    // original pairs with a wait reason for a waiting thread.

    constexpr const char* kThreadStates[] = {
        "Initialized", "Ready", "Running", "Standby",
        "Terminated", "Wait:", "Transition", "Unknown",
    };

    constexpr const char* kWaitReasons[] = {
        "Executive", "FreePage", "PageIn", "PoolAlloc", "DelayExec", "Suspended",
        "UserReq", "Executive", "FreePage", "PageIn", "PoolAlloc", "DelayExec",
        "Suspended", "UserReq", "EventPair", "Queue", "LpcReceive", "LpcReply",
        "VirtualMem", "PageOut", "Rendezvous,", "KeyedEvent", "Terminated",
        "ProcessInSwap", "CpuRateControl", "CalloutStack", "Kernel", "Resource",
        "PushLock", "Mutex", "QuantumEnd", "DispatchInt", "Preempted",
        "YieldExecution", "FastMutex", "GuardedMutex", "Rundown",
    };

    // --- Collected per-process record -------------------------------------

    struct Process
    {
        DWORD pid = 0;
        DWORD parentPid = 0;
        std::string name;
        LONG basePriority = 0;
        ULONG threadCount = 0;
        ULONG handleCount = 0;
        LONGLONG kernelTime = 0;
        LONGLONG userTime = 0;
        LONGLONG createTime = 0;
        ULONGLONG virtualSize = 0;
        ULONGLONG workingSet = 0;
        ULONGLONG pagefile = 0;
        ULONGLONG peakPagefile = 0;
        ULONGLONG nonPagedPool = 0;
        ULONGLONG pagedPool = 0;
        ULONG pageFaults = 0;
        const SystemThreadInfo* threads = nullptr;
    };

    // --- Helpers -----------------------------------------------------------

    [[nodiscard]] LONGLONG CurrentFileTime()
    {
        FILETIME ft = {};
        ::GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER u = {};
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return static_cast<LONGLONG>(u.QuadPart);
    }

    // Format a 100-nanosecond tick count as the original's H:MM:SS.mmm field.
    [[nodiscard]] std::string FormatTime(LONGLONG ticks)
    {
        if (ticks < 0)
        {
            ticks = 0;
        }
        const LONGLONG totalMs = ticks / 10000;
        const int millis = static_cast<int>(totalMs % 1000);
        const LONGLONG totalSec = totalMs / 1000;
        const int seconds = static_cast<int>(totalSec % 60);
        const LONGLONG totalMin = totalSec / 60;
        const int minutes = static_cast<int>(totalMin % 60);
        const int hours = static_cast<int>(totalMin / 60);
        char buffer[64] = {};
        std::snprintf(buffer, std::size(buffer), "%2d:%02d:%02d.%03d",
                      hours, minutes, seconds, millis);
        return buffer;
    }

    [[nodiscard]] std::string ThreadStateText(ULONG state, ULONG waitReason)
    {
        if (state == 5)
        {
            const char* reason = waitReason < std::size(kWaitReasons)
                                     ? kWaitReasons[waitReason]
                                     : "Unknown";
            return std::string("Wait:") + reason;
        }
        if (state < std::size(kThreadStates))
        {
            return kThreadStates[state];
        }
        return "Unknown";
    }

    // Convert the image name to the base name the original shows: ANSI, without
    // the ".exe" extension. The idle process carries no image name.
    [[nodiscard]] std::string ProcessName(const SystemProcessInfo* info)
    {
        const DWORD pid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(info->UniqueProcessId));
        if (pid == 0)
        {
            return "Idle";
        }
        if (info->ImageName.Buffer == nullptr || info->ImageName.Length == 0)
        {
            return {};
        }
        const int wideChars = info->ImageName.Length / static_cast<int>(sizeof(WCHAR));
        const int needed = ::WideCharToMultiByte(CP_ACP, 0, info->ImageName.Buffer, wideChars,
                                                 nullptr, 0, nullptr, nullptr);
        std::string name(static_cast<size_t>(needed), '\0');
        ::WideCharToMultiByte(CP_ACP, 0, info->ImageName.Buffer, wideChars,
                              name.data(), needed, nullptr, nullptr);
        if (name.size() >= 4 && ::_stricmp(name.c_str() + name.size() - 4, ".exe") == 0)
        {
            name.resize(name.size() - 4);
        }
        return name;
    }

    [[nodiscard]] std::vector<BYTE> QueryProcesses()
    {
        const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            return {};
        }
        const auto query = reinterpret_cast<NtQuerySystemInformationFn>(
            ::GetProcAddress(ntdll, "NtQuerySystemInformation"));
        if (query == nullptr)
        {
            return {};
        }
        ULONG size = 0x20000;
        std::vector<BYTE> buffer;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            buffer.resize(size);
            ULONG needed = 0;
            const NtStatus status = query(kSystemProcessInformation, buffer.data(), size, &needed);
            if (status == kStatusInfoLengthMismatch)
            {
                size = needed != 0 ? needed + 0x10000 : size * 2;
                continue;
            }
            if (status < 0)
            {
                return {};
            }
            buffer.resize(needed);
            return buffer;
        }
        return {};
    }

    [[nodiscard]] std::vector<Process> CollectProcesses(const std::vector<BYTE>& raw)
    {
        std::vector<Process> result;
        size_t offset = 0;
        while (offset + sizeof(SystemProcessInfo) <= raw.size())
        {
            const auto* info = reinterpret_cast<const SystemProcessInfo*>(raw.data() + offset);
            Process p;
            p.pid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(info->UniqueProcessId));
            p.parentPid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(info->InheritedFromUniqueProcessId));
            p.name = ProcessName(info);
            p.basePriority = info->BasePriority;
            p.threadCount = info->NumberOfThreads;
            p.handleCount = info->HandleCount;
            p.kernelTime = info->KernelTime.QuadPart;
            p.userTime = info->UserTime.QuadPart;
            p.createTime = info->CreateTime.QuadPart;
            p.virtualSize = info->VirtualSize;
            p.workingSet = info->WorkingSetSize;
            p.pagefile = info->PagefileUsage;
            p.peakPagefile = info->PeakPagefileUsage;
            p.nonPagedPool = info->QuotaNonPagedPoolUsage;
            p.pagedPool = info->QuotaPagedPoolUsage;
            p.pageFaults = info->PageFaultCount;
            p.threads = reinterpret_cast<const SystemThreadInfo*>(
                reinterpret_cast<const BYTE*>(info) + sizeof(SystemProcessInfo));
            result.push_back(std::move(p));
            if (info->NextEntryOffset == 0)
            {
                break;
            }
            offset += info->NextEntryOffset;
        }
        return result;
    }

    // --- Output ------------------------------------------------------------

    constexpr ULONGLONG kKilo = 1024;

    void PrintThreadDetail(const Process& p, LONGLONG now)
    {
        std::printf("%4s %3s %9s %16s %13s %13s %14s\n",
                    "Tid", "Pri", "Cswtch", "State", "User Time", "Kernel Time", "Elapsed Time");
        for (ULONG i = 0; i < p.threadCount; ++i)
        {
            const SystemThreadInfo& t = p.threads[i];
            const DWORD tid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(t.Cid.UniqueThread));
            const std::string state = ThreadStateText(t.ThreadState, t.WaitReason);
            const std::string userTime = FormatTime(t.UserTime.QuadPart);
            const std::string kernelTime = FormatTime(t.KernelTime.QuadPart);
            const std::string elapsed = FormatTime(now - t.CreateTime.QuadPart);
            std::printf("%4d %3d %9I64d %16s %12s %13s %14s\n",
                        tid, t.Priority, static_cast<LONGLONG>(t.ContextSwitches),
                        state.c_str(), userTime.c_str(), kernelTime.c_str(), elapsed.c_str());
        }
    }

    void PrintDefaultHeader()
    {
        std::printf("%-17s %5s %3s %3s %4s %6s %15s %15s\n",
                    "Name", "Pid", "Pri", "Thd", "Hnd", "Priv", "CPU Time", "Elapsed Time");
    }

    void PrintDefaultRow(const Process& p, LONGLONG now)
    {
        const std::string cpu = FormatTime(p.kernelTime + p.userTime);
        const std::string elapsed = FormatTime(now - p.createTime);
        std::printf("%-17s %5d %3d %3d %4d %6d %15s %15s\n",
                    p.name.c_str(), p.pid, p.basePriority, p.threadCount, p.handleCount,
                    static_cast<int>(p.pagefile / kKilo), cpu.c_str(), elapsed.c_str());
    }

    void PrintMemoryHeader()
    {
        std::printf("%-17s %5s %7s %7s %7s %7s %8s %6s %4s\n",
                    "Name", "Pid", "VM", "WS", "Priv", "Priv Pk", "Faults", "NonP", "Page");
    }

    void PrintMemoryRow(const Process& p)
    {
        std::printf("%-17s %5d %7llu %7I64d %7I64d %7I64d %8I64d %6I64d %4I64d\n",
                    p.name.c_str(), p.pid,
                    static_cast<unsigned long long>(p.virtualSize / kKilo),
                    static_cast<LONGLONG>(p.workingSet / kKilo),
                    static_cast<LONGLONG>(p.pagefile / kKilo),
                    static_cast<LONGLONG>(p.peakPagefile / kKilo),
                    static_cast<LONGLONG>(p.pageFaults),
                    static_cast<LONGLONG>(p.nonPagedPool / kKilo),
                    static_cast<LONGLONG>(p.pagedPool / kKilo));
    }

    void PrintTreeHeader()
    {
        std::printf("%-30s %5s %3s %3s %4s %7s %7s %7s\n",
                    "Name", "Pid", "Pri", "Thd", "Hnd", "VM", "WS", "Priv");
    }

    void PrintTreeRow(const Process& p, int depth)
    {
        const std::string indented = std::string(static_cast<size_t>(depth) * 2, ' ') + p.name;
        std::printf("%-30s %5d %3d %3d %4d %7llu %7d %7d\n",
                    indented.c_str(), p.pid, p.basePriority, p.threadCount, p.handleCount,
                    static_cast<unsigned long long>(p.virtualSize / kKilo),
                    static_cast<int>(p.workingSet / kKilo),
                    static_cast<int>(p.pagefile / kKilo));
    }

    // --- Filtering ---------------------------------------------------------

    [[nodiscard]] bool IsAllDigits(const std::string& s)
    {
        if (s.empty())
        {
            return false;
        }
        for (const char c : s)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool NameMatches(const std::string& name, const std::string& filter, bool exact)
    {
        if (exact)
        {
            return ::_stricmp(name.c_str(), filter.c_str()) == 0;
        }
        return ::_strnicmp(name.c_str(), filter.c_str(), filter.size()) == 0;
    }

    struct Options
    {
        bool threadDetail = false;   // -d
        bool memoryDetail = false;   // -m
        bool everything = false;     // -x
        bool tree = false;           // -t
        bool taskMode = false;       // -s
        bool exact = false;          // -e
        int taskSeconds = 0;         // -s n
        int refreshRate = 1;         // -r n
        bool hasFilter = false;
        bool filterIsPid = false;
        DWORD filterPid = 0;
        std::string filterName;
        std::string computer;        // without the leading backslashes
        std::string user;
        std::string password;
        bool haveUser = false;
        bool havePassword = false;
    };

    void PrintUsage(const char* exePath)
    {
        std::printf("Usage: %s [-d][-m][-x][-t][-s [n] [-r n] [\\\\computer [-u username][-p password][name|pid]\n",
                    exePath);
        std::printf("   -d          Show thread detail.\n");
        std::printf("   -m          Show memory detail.\n");
        std::printf("   -x          Show processes, memory information and threads.\n");
        std::printf("   -t          Show process tree.\n");
        std::printf("   -s [n]      Run in task-manager mode, for optional seconds specified.\n");
        std::printf("               Press Escape to abort.\n");
        std::printf("   -r n        Task-manager mode refresh rate in seconds (default is 1).\n");
        std::printf("   \\\\computer  Specifies remote computer.\n");
        std::printf("   -u          Optional user name for remote login.\n");
        std::printf("   -p          Optional password for remote login. If you don't present\n");
        std::printf("               on the command line pslist will prompt you for it if necessary.\n");
        std::printf("   name        Show information about processes that begin with the name\n");
        std::printf("               specified.\n");
        std::printf("   -e          Exact match the process name.\n");
        std::printf("   -nobanner   Do not display the startup banner and copyright message.\n");
        std::printf("   pid         Show information about specified process.\n");
        std::printf("\n");
        std::printf("All memory values are displayed in KB.\n");
        std::printf("Abbreviation key:\n");
        std::printf("   Pri         Priority\n");
        std::printf("   Thd         Number of Threads\n");
        std::printf("   Hnd         Number of Handles\n");
        std::printf("   VM          Virtual Memory\n");
        std::printf("   WS          Working Set\n");
        std::printf("   Priv        Private Virtual Memory\n");
        std::printf("   Priv Pk     Private Virtual Memory Peak\n");
        std::printf("   Faults      Page Faults\n");
        std::printf("   NonP        Non-Paged Pool\n");
        std::printf("   Page        Paged Pool\n");
        std::printf("   Cswtch      Context Switches\n");
        std::printf("\n");
    }

    // Parse PsList's own switches. Returns false when the caller should print
    // usage. The order follows the original: switches start with '-' or '/', a
    // '\\' prefix names a computer, a trailing token is the name or pid filter.
    [[nodiscard]] bool ParseArgs(int argc, char** argv, Options& o)
    {
        for (int i = 1; i < argc; ++i)
        {
            const char* a = argv[i];
            if ((a[0] == '-' || a[0] == '/') && a[1] != '\0')
            {
                const char c = static_cast<char>(::tolower(static_cast<unsigned char>(a[1])));
                if (a[2] != '\0' && c != 'u' && c != 'p' && c != 'r' && c != 's')
                {
                    return false;
                }
                switch (c)
                {
                case 'd': o.threadDetail = true; break;
                case 'm': o.memoryDetail = true; break;
                case 'x': o.everything = true; break;
                case 't': o.tree = true; break;
                case 'e': o.exact = true; break;
                case 's':
                    o.taskMode = true;
                    if (i + 1 < argc && IsAllDigits(argv[i + 1]))
                    {
                        o.taskSeconds = std::atoi(argv[++i]);
                    }
                    break;
                case 'r':
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    o.refreshRate = std::atoi(argv[++i]);
                    break;
                case 'u':
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    o.user = argv[++i];
                    o.haveUser = true;
                    break;
                case 'p':
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    o.password = argv[++i];
                    o.havePassword = true;
                    break;
                default:
                    return false;
                }
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                o.computer = a + 2;
            }
            else
            {
                if (o.hasFilter)
                {
                    return false;
                }
                o.hasFilter = true;
                o.filterName = a;
                if (IsAllDigits(o.filterName))
                {
                    o.filterIsPid = true;
                    o.filterPid = static_cast<DWORD>(std::strtoul(o.filterName.c_str(), nullptr, 10));
                }
            }
        }
        if (o.everything)
        {
            o.memoryDetail = true;
            o.threadDetail = true;
        }
        return true;
    }

    [[nodiscard]] bool Keep(const Process& p, const Options& o)
    {
        if (!o.hasFilter)
        {
            return true;
        }
        if (o.filterIsPid)
        {
            return p.pid == o.filterPid;
        }
        return NameMatches(p.name, o.filterName, o.exact);
    }

    void PrintTreeChildren(const std::vector<Process>& all,
                           const std::vector<std::vector<size_t>>& children,
                           size_t index, int depth, std::vector<bool>& shown)
    {
        if (shown[index])
        {
            return;
        }
        shown[index] = true;
        PrintTreeRow(all[index], depth);
        for (const size_t child : children[index])
        {
            PrintTreeChildren(all, children, child, depth + 1, shown);
        }
    }

    void PrintTree(const std::vector<Process>& all)
    {
        const size_t count = all.size();
        std::vector<std::vector<size_t>> children(count);
        std::vector<bool> hasParent(count, false);
        for (size_t i = 0; i < count; ++i)
        {
            for (size_t j = 0; j < count; ++j)
            {
                if (j != i && all[j].pid == all[i].parentPid && all[i].pid != all[i].parentPid)
                {
                    children[j].push_back(i);
                    hasParent[i] = true;
                    break;
                }
            }
        }
        std::vector<bool> shown(count, false);
        for (size_t i = 0; i < count; ++i)
        {
            if (!hasParent[i])
            {
                PrintTreeChildren(all, children, i, 0, shown);
            }
        }
        for (size_t i = 0; i < count; ++i)
        {
            PrintTreeChildren(all, children, i, 0, shown);
        }
    }

    void PrintNotFound(const Options& o, const char* computer)
    {
        if (o.filterIsPid)
        {
            std::printf("process %lu was not found on %s\n\n",
                        static_cast<unsigned long>(o.filterPid), computer);
        }
        else
        {
            // The name path ends with the original's trailing line-clear space.
            std::printf("process %s was not found on %s\n\n ", o.filterName.c_str(), computer);
        }
    }

    // Render one full listing for the given computer name. Returns the number of
    // matched processes so the task-mode path can react.
    size_t Render(const std::vector<Process>& all, const Options& o, const char* computer)
    {
        const LONGLONG now = CurrentFileTime();

        if (o.tree)
        {
            std::printf("Process information for %s:\n\n", computer);
            PrintTreeHeader();
            PrintTree(all);
            return all.size();
        }

        std::vector<const Process*> matched;
        for (const Process& p : all)
        {
            if (Keep(p, o))
            {
                matched.push_back(&p);
            }
        }

        if (o.everything)
        {
            std::printf("Process and thread information for %s:\n", computer);
            if (o.hasFilter && matched.empty())
            {
                std::printf("\n");
                PrintNotFound(o, computer);
                return 0;
            }
            for (const Process* p : matched)
            {
                std::printf("\n");
                PrintMemoryHeader();
                PrintMemoryRow(*p);
                PrintThreadDetail(*p, now);
            }
            return matched.size();
        }

        if (o.threadDetail)
        {
            std::printf("Thread detail for %s:\n\n", computer);
            if (o.hasFilter && matched.empty())
            {
                PrintNotFound(o, computer);
                return 0;
            }
            for (const Process* p : matched)
            {
                std::printf("\n%s %d:\n", p->name.c_str(), p->pid);
                PrintThreadDetail(*p, now);
            }
            return matched.size();
        }

        if (o.memoryDetail)
        {
            std::printf("Process memory detail for %s:\n\n", computer);
            if (o.hasFilter && matched.empty())
            {
                PrintNotFound(o, computer);
                return 0;
            }
            PrintMemoryHeader();
            for (const Process* p : matched)
            {
                PrintMemoryRow(*p);
            }
            return matched.size();
        }

        std::printf("Process information for %s:\n\n", computer);
        if (o.hasFilter && matched.empty())
        {
            PrintNotFound(o, computer);
            return 0;
        }
        PrintDefaultHeader();
        for (const Process* p : matched)
        {
            PrintDefaultRow(*p, now);
        }
        return matched.size();
    }

    void ClearConsole()
    {
        const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info = {};
        if (!::GetConsoleScreenBufferInfo(out, &info))
        {
            return;
        }
        const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
        const COORD home = {0, 0};
        DWORD written = 0;
        ::FillConsoleOutputCharacterA(out, ' ', cells, home, &written);
        ::SetConsoleCursorPosition(out, home);
    }

    // Task-manager mode: redraw until the time budget elapses or Escape is hit.
    //
    // ponytail: the original drives this with ReadConsoleInputW and the console
    // cursor APIs. This keeps the same clear-and-redraw behaviour with a simpler
    // _kbhit poll, which a console tool reads identically. Upgrade to the input
    // queue only if key chords ever need distinguishing.
    void RunTaskMode(const Options& o, const char* computer)
    {
        const DWORD start = ::GetTickCount();
        for (;;)
        {
            const std::vector<BYTE> raw = QueryProcesses();
            const std::vector<Process> all = CollectProcesses(raw);
            ClearConsole();
            Render(all, o, computer);
            std::fflush(stdout);

            if (o.taskSeconds != 0)
            {
                const DWORD elapsedSec = (::GetTickCount() - start) / 1000;
                if (elapsedSec >= static_cast<DWORD>(o.taskSeconds))
                {
                    break;
                }
            }
            const int refresh = o.refreshRate > 0 ? o.refreshRate : 1;
            bool abort = false;
            for (int slice = 0; slice < refresh * 10 && !abort; ++slice)
            {
                if (::_kbhit() && ::_getch() == 27)
                {
                    abort = true;
                }
                ::Sleep(100);
            }
            if (abort)
            {
                break;
            }
        }
    }
}

int main(int argc, char** argv)
{
    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    // The original shows the banner before usage even under -nobanner, and before
    // the listing only when -nobanner was not given.
    Options o;
    if (!ParseArgs(argc, argv, o))
    {
        pstools::PrintBanner(kBanner, false);
        PrintUsage(argv[0]);
        return -1;
    }
    pstools::PrintBanner(kBanner, noBanner);

    if (o.taskSeconds != 0 && o.refreshRate > o.taskSeconds)
    {
        std::fprintf(stderr, "The refresh rate must be greater than the seconds to run.\n\n");
        return -1;
    }

    const std::string local = pstools::LocalComputerName();

    // Remote targets are served by the original through the remote registry.
    // This local-focused reconstruction handles only the local machine.
    if (!o.computer.empty() && ::_stricmp(o.computer.c_str(), local.c_str()) != 0)
    {
        if (o.haveUser && !o.havePassword)
        {
            o.password = pstools::PromptHiddenPassword();
        }
        std::fprintf(stderr, "Cannot connect to remote registry on %s:\n", o.computer.c_str());
        std::fprintf(stderr, "This build queries the local machine only.\n");
        return -1;
    }

    const char* computer = local.c_str();

    if (o.taskMode)
    {
        RunTaskMode(o, computer);
        return 0;
    }

    const std::vector<BYTE> raw = QueryProcesses();
    if (raw.empty())
    {
        std::fprintf(stderr, "Failed to take process snapshot on %s.\n", computer);
        return -1;
    }
    const std::vector<Process> all = CollectProcesses(raw);
    Render(all, o, computer);
    return 0;
}
