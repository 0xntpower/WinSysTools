// PsList output: the recovered column layouts, the thread-state and wait-reason
// tables, the per-mode renderers, the tree builder, and the task-manager loop.
// Column format strings and the field mapping are in analysis/PsList.md.
#pragma once

#include "CommandLine.h"

#include <conio.h>
#include <cstdio>
#include <string>
#include <vector>

namespace pslist
{
    // Recovered verbatim from the binary string arrays. Index 5 of the states is the wait prefix.
    // See analysis/PsList.md for the source addresses.
    inline constexpr const char* kThreadStates[] = {
        "Initialized", "Ready", "Running", "Standby", "Terminated", "Wait:", "Transition", "Unknown",
    };

    inline constexpr const char* kWaitReasons[] = {
        "Executive",      "FreePage",     "PageIn",      "PoolAlloc",      "DelayExec",  "Suspended",
        "UserReq",        "Executive",    "FreePage",    "PageIn",         "PoolAlloc",  "DelayExec",
        "Suspended",      "UserReq",      "EventPair",   "Queue",          "LpcReceive", "LpcReply",
        "VirtualMem",     "PageOut",      "Rendezvous,", "KeyedEvent",     "Terminated", "ProcessInSwap",
        "CpuRateControl", "CalloutStack", "Kernel",      "Resource",       "PushLock",   "Mutex",
        "QuantumEnd",     "DispatchInt",  "Preempted",   "YieldExecution", "FastMutex",  "GuardedMutex",
        "Rundown",
    };

    inline constexpr ULONGLONG kKilo = 1024;

    [[nodiscard]] inline LONGLONG CurrentFileTime()
    {
        FILETIME ft = {};
        ::GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER u = {};
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return static_cast<LONGLONG>(u.QuadPart);
    }

    // Format a 100-nanosecond tick count as the original's H:MM:SS.mmm field.
    [[nodiscard]] inline std::string FormatTime(LONGLONG ticks)
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
        std::snprintf(buffer, std::size(buffer), "%2d:%02d:%02d.%03d", hours, minutes, seconds, millis);
        return buffer;
    }

    [[nodiscard]] inline std::string ThreadStateText(ULONG state, ULONG waitReason)
    {
        if (state == 5)
        {
            const char* reason = waitReason < std::size(kWaitReasons) ? kWaitReasons[waitReason] : "Unknown";
            return std::string("Wait:") + reason;
        }
        if (state < std::size(kThreadStates))
        {
            return kThreadStates[state];
        }
        return "Unknown";
    }

    inline void PrintThreadDetail(const Process& p, LONGLONG now)
    {
        std::printf("%4s %3s %9s %16s %13s %13s %14s\n", "Tid", "Pri", "Cswtch", "State", "User Time", "Kernel Time",
                    "Elapsed Time");
        for (ULONG i = 0; i < p.threadCount; ++i)
        {
            const SystemThreadInfo& t = p.threads[i];
            const DWORD tid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(t.Cid.UniqueThread));
            const std::string state = ThreadStateText(t.ThreadState, t.WaitReason);
            const std::string userTime = FormatTime(t.UserTime.QuadPart);
            const std::string kernelTime = FormatTime(t.KernelTime.QuadPart);
            const std::string elapsed = FormatTime(now - t.CreateTime.QuadPart);
            std::printf("%4d %3d %9I64d %16s %12s %13s %14s\n", tid, t.Priority,
                        static_cast<LONGLONG>(t.ContextSwitches), state.c_str(), userTime.c_str(), kernelTime.c_str(),
                        elapsed.c_str());
        }
    }

    inline void PrintDefaultHeader()
    {
        std::printf("%-17s %5s %3s %3s %4s %6s %15s %15s\n", "Name", "Pid", "Pri", "Thd", "Hnd", "Priv", "CPU Time",
                    "Elapsed Time");
    }

    inline void PrintDefaultRow(const Process& p, LONGLONG now)
    {
        const std::string cpu = FormatTime(p.kernelTime + p.userTime);
        const std::string elapsed = FormatTime(now - p.createTime);
        std::printf("%-17s %5d %3d %3d %4d %6d %15s %15s\n", p.name.c_str(), p.pid, p.basePriority, p.threadCount,
                    p.handleCount, static_cast<int>(p.pagefile / kKilo), cpu.c_str(), elapsed.c_str());
    }

    inline void PrintMemoryHeader()
    {
        std::printf("%-17s %5s %7s %7s %7s %7s %8s %6s %4s\n", "Name", "Pid", "VM", "WS", "Priv", "Priv Pk", "Faults",
                    "NonP", "Page");
    }

    inline void PrintMemoryRow(const Process& p)
    {
        std::printf("%-17s %5d %7llu %7I64d %7I64d %7I64d %8I64d %6I64d %4I64d\n", p.name.c_str(), p.pid,
                    static_cast<unsigned long long>(p.virtualSize / kKilo), static_cast<LONGLONG>(p.workingSet / kKilo),
                    static_cast<LONGLONG>(p.pagefile / kKilo), static_cast<LONGLONG>(p.peakPagefile / kKilo),
                    static_cast<LONGLONG>(p.pageFaults), static_cast<LONGLONG>(p.nonPagedPool / kKilo),
                    static_cast<LONGLONG>(p.pagedPool / kKilo));
    }

    inline void PrintTreeHeader()
    {
        std::printf("%-30s %5s %3s %3s %4s %7s %7s %7s\n", "Name", "Pid", "Pri", "Thd", "Hnd", "VM", "WS", "Priv");
    }

    inline void PrintTreeRow(const Process& p, int depth)
    {
        const std::string indented = std::string(static_cast<size_t>(depth) * 2, ' ') + p.name;
        std::printf("%-30s %5d %3d %3d %4d %7llu %7d %7d\n", indented.c_str(), p.pid, p.basePriority, p.threadCount,
                    p.handleCount, static_cast<unsigned long long>(p.virtualSize / kKilo),
                    static_cast<int>(p.workingSet / kKilo), static_cast<int>(p.pagefile / kKilo));
    }

    inline void PrintUsage(const char* exePath)
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

    inline void PrintTreeChildren(const std::vector<Process>& all, const std::vector<std::vector<size_t>>& children,
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

    inline void PrintTree(const std::vector<Process>& all)
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

    inline void PrintNotFound(const Options& o, const char* computer)
    {
        if (o.filterIsPid)
        {
            std::printf("process %lu was not found on %s\n\n", static_cast<unsigned long>(o.filterPid), computer);
        }
        else
        {
            // The name path ends with the original's trailing line-clear space.
            std::printf("process %s was not found on %s\n\n ", o.filterName.c_str(), computer);
        }
    }

    // Render one full listing for the given computer. Returns the matched-process count for the task loop.
    inline size_t Render(const std::vector<Process>& all, const Options& o, const char* computer)
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

    inline void ClearConsole()
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
    // ponytail: the _kbhit poll stands in for the original's ReadConsoleInputW, identical for a console
    // tool. Upgrade to the input queue only if key chords ever need distinguishing.
    inline void RunTaskMode(const Options& o, const char* computer)
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
} // namespace pslist
