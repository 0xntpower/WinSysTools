// ProcDump monitoring: process selection, the monitoring header, the samplers,
// and the trigger loops (immediate, timed, CPU, commit, hung, exception).
// See analysis/ProcDump.md for the sub_ map.
#pragma once

#include "DumpWriter.h"

namespace procdump
{
    constexpr int kHungTimeoutMs = 5000;

    [[nodiscard]] inline bool AllDigits(const std::wstring& s) noexcept
    {
        if (s.empty())
        {
            return false;
        }
        for (const wchar_t c : s)
        {
            if (c < L'0' || c > L'9')
            {
                return false;
            }
        }
        return true;
    }

    inline void PrintNotFoundHint()
    {
        std::fputs("Try elevating the command prompt or using PsExec to make one as SYSTEM.\n", stdout);
        std::fputs("\tpsexec.exe -s -d -i cmd.exe\n", stdout);
        std::fputs("\tprocdump.exe -accepteula ...\n", stdout);
        std::fputc('\n', stdout);
    }

    inline void FillImageInfo(DWORD pid, Target& out)
    {
        ScopedHandle proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!proc.Valid())
        {
            return;
        }
        wchar_t path[MAX_PATH] = {};
        DWORD size = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(proc.Get(), 0, path, &size) != 0)
        {
            out.imagePath = path;
            const std::wstring::size_type slash = out.imagePath.find_last_of(L"\\/");
            out.name = (slash == std::wstring::npos) ? out.imagePath : out.imagePath.substr(slash + 1);
        }
    }

    // Count name matches among running processes. A match is case-insensitive
    // against the image base name, with or without a trailing ".exe".
    [[nodiscard]] inline int FindByName(const std::wstring& wanted, DWORD& foundPid)
    {
        std::wstring withExe = wanted;
        const bool hasExt = wanted.size() >= 4 && _wcsicmp(wanted.c_str() + wanted.size() - 4, L".exe") == 0;
        if (!hasExt)
        {
            withExe += L".exe";
        }

        ScopedHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snap.Valid())
        {
            return 0;
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        int matches = 0;
        if (Process32FirstW(snap.Get(), &entry) != 0)
        {
            do
            {
                if (_wcsicmp(entry.szExeFile, wanted.c_str()) == 0 || _wcsicmp(entry.szExeFile, withExe.c_str()) == 0)
                {
                    ++matches;
                    foundPid = entry.th32ProcessID;
                }
            } while (Process32NextW(snap.Get(), &entry) != 0);
        }
        return matches;
    }

    // Resolve the target to a PID, applying the -w wait loop for a name. Prints
    // the original's not-found and multiple-match messages and returns false on
    // failure.
    [[nodiscard]] inline bool ResolveTarget(const Config& cfg, Target& out)
    {
        if (AllDigits(cfg.target))
        {
            const DWORD pid = static_cast<DWORD>(_wtoi(cfg.target.c_str()));
            ScopedHandle probe(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
            if (!probe.Valid())
            {
                std::fputs("No process matching the specified PID can be found.\n", stdout);
                PrintNotFoundHint();
                return false;
            }
            out.pid = pid;
            FillImageInfo(pid, out);
            if (out.name.empty())
            {
                out.name = cfg.target;
            }
            return true;
        }

        DWORD pid = 0;
        int matches = FindByName(cfg.target, pid);
        if (matches == 0 && cfg.wait)
        {
            std::fprintf(stdout, "Waiting for process named %s...\n", Narrow(cfg.target).c_str());
            while (matches == 0)
            {
                Sleep(1000);
                matches = FindByName(cfg.target, pid);
            }
        }
        if (matches == 0)
        {
            std::fputs("No process matching the specified name can be found.\n", stdout);
            PrintNotFoundHint();
            return false;
        }
        if (matches > 1)
        {
            std::fputs("Multiple processes match the specified name.\n", stdout);
            return false;
        }
        out.pid = pid;
        FillImageInfo(pid, out);
        if (out.name.empty())
        {
            out.name = cfg.target;
        }
        return true;
    }

    // Monitoring header, printed only in monitoring mode.
    inline void PrintHeader(const Config& cfg, const Target& target)
    {
        std::fprintf(stdout, "Process:               %s (%u)\n", Narrow(target.name).c_str(), target.pid);
        if (!target.imagePath.empty())
        {
            std::fprintf(stdout, "Process image:         %s\n", Narrow(target.imagePath).c_str());
        }

        if (cfg.cpuAbove || cfg.cpuBelow)
        {
            const char* op = cfg.cpuAbove ? ">=" : "<";
            const char* scale = cfg.singleCore ? "single core" : "system";
            std::fprintf(stdout, "CPU threshold:         %s %d%% of %s\n", op, cfg.cpuThreshold, scale);
        }
        else
        {
            std::fputs("CPU threshold:         n/a\n", stdout);
        }

        std::fputs("Performance counter:   n/a\n", stdout);

        if (cfg.memAbove || cfg.memBelow)
        {
            const char* op = cfg.memAbove ? ">=" : "<";
            std::fprintf(stdout, "Commit threshold:      %s %d MB\n", op, cfg.commitThresholdMb);
        }
        else
        {
            std::fputs("Commit threshold:      n/a\n", stdout);
        }

        std::fprintf(stdout, "Threshold seconds:     %d\n", cfg.seconds);
        std::fprintf(stdout, "Hung window check:     %s\n", cfg.hung ? "Enabled" : "Disabled");
        std::fputs("Log debug strings:     Disabled\n", stdout);

        if (cfg.onException)
        {
            const char* kind = cfg.firstChance ? "First Chance+Unhandled" : "Unhandled";
            const char* bp = cfg.breakpoints ? "+Breakpoints" : "";
            std::fprintf(stdout, "Exception monitor:     %s%s\n", kind, bp);
        }
        else
        {
            std::fputs("Exception monitor:     Disabled\n", stdout);
        }

        std::fputs("Exception filter:      [Includes]\n", stdout);
        std::fputs("                       *\n", stdout);
        std::fputs("                       [Excludes]\n", stdout);
        std::fprintf(stdout, "Terminate monitor:     %s\n", cfg.terminate ? "Enabled" : "Disabled");
        std::fputs("Cloning type:          Disabled\n", stdout);
        std::fputs("Concurrent limit:      n/a\n", stdout);
        std::fputs("Avoid outage:          n/a\n", stdout);
        std::fprintf(stdout, "Number of dumps:       %d\n", cfg.count);

        std::wstring folder;
        if (cfg.hasDumpArg && IsDirectoryArg(cfg.dumpArg))
        {
            folder = cfg.dumpArg;
        }
        else if (!cfg.hasDumpArg)
        {
            wchar_t cwd[MAX_PATH] = {};
            GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
            folder = cwd;
        }
        std::fprintf(stdout, "Dump folder:           %s\\\n", Narrow(folder).c_str());

        std::wstring mask = L"PROCESSNAME_YYMMDD_HHMMSS";
        if (cfg.hasDumpArg && !IsDirectoryArg(cfg.dumpArg))
        {
            mask = cfg.dumpArg;
        }
        std::fprintf(stdout, "Dump filename/mask:    %s\n", Narrow(mask).c_str());

        std::fputs("Queue to WER:          Disabled\n", stdout);
        std::fputs("Kill after dump:       Disabled\n", stdout);
        std::fputs("\n\nPress Ctrl-C to end monitoring without terminating the process.\n\n", stdout);
    }

    [[nodiscard]] inline bool ProcessAlive(HANDLE proc) noexcept
    {
        DWORD code = 0;
        return GetExitCodeProcess(proc, &code) != 0 && code == STILL_ACTIVE;
    }

    // CPU sampling. Percentage is the process busy time over the wall-clock
    // interval, divided across the cores unless -u scopes it to a single core.
    [[nodiscard]] inline unsigned long long FileTimeToU64(const FILETIME& ft) noexcept
    {
        ULARGE_INTEGER u{};
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u.QuadPart;
    }

    [[nodiscard]] inline int SampleCpu(HANDLE proc, unsigned long long& prevProc, unsigned long long& prevWall,
                                       bool singleCore)
    {
        FILETIME create{}, exit{}, kernel{}, user{};
        if (GetProcessTimes(proc, &create, &exit, &kernel, &user) == 0)
        {
            return 0;
        }
        FILETIME nowFt{};
        GetSystemTimeAsFileTime(&nowFt);
        const unsigned long long procNow = FileTimeToU64(kernel) + FileTimeToU64(user);
        const unsigned long long wallNow = FileTimeToU64(nowFt);
        if (prevWall == 0)
        {
            prevProc = procNow;
            prevWall = wallNow;
            return 0;
        }
        const unsigned long long procDelta = procNow - prevProc;
        const unsigned long long wallDelta = wallNow - prevWall;
        prevProc = procNow;
        prevWall = wallNow;
        if (wallDelta == 0)
        {
            return 0;
        }
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const unsigned long long cores = singleCore ? 1 : si.dwNumberOfProcessors;
        return static_cast<int>((procDelta * 100) / (wallDelta * cores));
    }

    [[nodiscard]] inline int SampleCommitMb(HANDLE proc)
    {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(proc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)) == 0)
        {
            return 0;
        }
        return static_cast<int>(pmc.PrivateUsage / (1024ULL * 1024ULL));
    }

    [[nodiscard]] inline bool HasHungWindow(DWORD pid)
    {
        struct Ctx
        {
            DWORD pid;
            bool hung;
        } ctx{pid, false};
        EnumWindows(
            [](HWND hwnd, LPARAM param) -> BOOL
            {
                Ctx* c = reinterpret_cast<Ctx*>(param);
                DWORD windowPid = 0;
                GetWindowThreadProcessId(hwnd, &windowPid);
                if (windowPid != c->pid || IsWindowVisible(hwnd) == 0)
                {
                    return TRUE;
                }
                DWORD_PTR result = 0;
                const LRESULT ok =
                    SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, kHungTimeoutMs, &result);
                if (ok == 0)
                {
                    c->hung = true;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&ctx));
        return ctx.hung;
    }

    [[nodiscard]] inline const char* ExceptionName(DWORD code) noexcept
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:
            return "ACCESS_VIOLATION";
        case EXCEPTION_BREAKPOINT:
            return "BREAKPOINT";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW:
            return "STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:
            return "IN_PAGE_ERROR";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "PRIV_INSTRUCTION";
        default:
            return "EXCEPTION";
        }
    }

    // Trigger loops. Each returns the number of dumps written.
    [[nodiscard]] inline int RunImmediate(const Config& cfg, const Target& target, HANDLE proc)
    {
        int written = 0;
        if (WriteOneDump(cfg, target, proc, 1, 0, nullptr))
        {
            written = 1;
        }
        std::fprintf(stdout, "%s Dump count reached.\n", TimeStamp().c_str());
        return written;
    }

    [[nodiscard]] inline int RunTimed(const Config& cfg, const Target& target, HANDLE proc)
    {
        int written = 0;
        for (int i = 1; i <= cfg.count; ++i)
        {
            Sleep(static_cast<DWORD>(cfg.seconds) * 1000);
            if (!ProcessAlive(proc))
            {
                std::fputs("The process has exited.\n", stdout);
                break;
            }
            std::fprintf(stdout, "%s Timed:\n", TimeStamp().c_str());
            if (WriteOneDump(cfg, target, proc, i, 0, nullptr))
            {
                ++written;
            }
            if (i == cfg.count)
            {
                std::fprintf(stdout, "%s Dump count reached.\n", TimeStamp().c_str());
            }
        }
        return written;
    }

    // CPU trigger. A status line prints each second the condition holds, showing
    // the consecutive-second count, and "(Trigger)" is appended on the second the
    // count reaches -s, at which point the dump is written. The first baseline
    // sample is taken before the loop so the first reading lands one second in,
    // matching the original's "CPU: 0% 1s" first line.
    [[nodiscard]] inline int RunCpuTrigger(const Config& cfg, const Target& target, HANDLE proc)
    {
        int written = 0;
        int held = 0;
        unsigned long long prevProc = 0;
        unsigned long long prevWall = 0;
        static_cast<void>(SampleCpu(proc, prevProc, prevWall, cfg.singleCore));
        while (written < cfg.count)
        {
            Sleep(1000);
            if (!ProcessAlive(proc))
            {
                std::fputs("The process has exited.\n", stdout);
                break;
            }
            const int cpu = SampleCpu(proc, prevProc, prevWall, cfg.singleCore);
            const bool condition = cfg.cpuAbove ? (cpu >= cfg.cpuThreshold) : (cpu <= cfg.cpuThreshold);
            held = condition ? held + 1 : 0;
            if (held < 1)
            {
                continue;
            }
            const char* reason = (held >= cfg.seconds) ? "(Trigger)" : "";
            std::fprintf(stdout, "%s CPU: %d%% %lds %s\n", TimeStamp().c_str(), cpu, static_cast<long>(held), reason);
            if (held >= cfg.seconds)
            {
                held = 0;
                if (WriteOneDump(cfg, target, proc, written + 1, 0, nullptr))
                {
                    ++written;
                }
                if (written == cfg.count)
                {
                    std::fprintf(stdout, "%s Dump count reached.\n", TimeStamp().c_str());
                }
            }
        }
        return written;
    }

    // Commit and hung-window triggers fire on the first evaluation that meets the
    // condition, printing the plain trigger label and writing the dump.
    [[nodiscard]] inline int RunLevelTrigger(const Config& cfg, const Target& target, HANDLE proc)
    {
        int written = 0;
        while (written < cfg.count)
        {
            if (!ProcessAlive(proc))
            {
                std::fputs("The process has exited.\n", stdout);
                break;
            }
            bool fired = false;
            std::string label;
            if (cfg.memAbove || cfg.memBelow)
            {
                const int mb = SampleCommitMb(proc);
                const bool condition = cfg.memAbove ? (mb >= cfg.commitThresholdMb) : (mb <= cfg.commitThresholdMb);
                if (condition)
                {
                    char buf[32] = {};
                    std::snprintf(buf, sizeof(buf), "Commit: % 7dMb", mb);
                    label = buf;
                    fired = true;
                }
            }
            else if (cfg.hung)
            {
                if (HasHungWindow(target.pid))
                {
                    label = "Hung Window:";
                    fired = true;
                }
            }

            if (fired)
            {
                std::fprintf(stdout, "%s %s\n", TimeStamp().c_str(), label.c_str());
                if (WriteOneDump(cfg, target, proc, written + 1, 0, nullptr))
                {
                    ++written;
                }
                if (written == cfg.count)
                {
                    std::fprintf(stdout, "%s Dump count reached.\n", TimeStamp().c_str());
                }
            }
            else
            {
                Sleep(1000);
            }
        }
        return written;
    }

    // Exception trigger: attach as a debugger and dump on an unhandled (or, with
    // -e 1, first-chance) exception. Managed-process interop is out of scope.
    [[nodiscard]] inline int RunExceptionTrigger(const Config& cfg, const Target& target, HANDLE proc)
    {
        if (DebugActiveProcess(target.pid) == 0)
        {
            std::fputs("Error debugging process:\n", stdout);
            return 0;
        }
        DebugSetProcessKillOnExit(FALSE);

        int written = 0;
        bool running = true;
        while (running && written < cfg.count)
        {
            DEBUG_EVENT ev{};
            if (WaitForDebugEvent(&ev, INFINITE) == 0)
            {
                break;
            }
            DWORD continueStatus = DBG_EXCEPTION_NOT_HANDLED;
            switch (ev.dwDebugEventCode)
            {
            case EXCEPTION_DEBUG_EVENT:
            {
                const EXCEPTION_RECORD& rec = ev.u.Exception.ExceptionRecord;
                const bool isBreakpoint = rec.ExceptionCode == EXCEPTION_BREAKPOINT;
                const bool wanted =
                    (ev.u.Exception.dwFirstChance == 0 || cfg.firstChance) && (!isBreakpoint || cfg.breakpoints);
                if (wanted)
                {
                    std::fprintf(stdout, "%s Exception: %08X.%s\n", TimeStamp().c_str(), rec.ExceptionCode,
                                 ExceptionName(rec.ExceptionCode));

                    EXCEPTION_POINTERS pointers{};
                    pointers.ExceptionRecord = const_cast<EXCEPTION_RECORD*>(&rec);
                    MINIDUMP_EXCEPTION_INFORMATION exInfo{};
                    exInfo.ThreadId = ev.dwThreadId;
                    exInfo.ExceptionPointers = &pointers;
                    exInfo.ClientPointers = FALSE;
                    if (WriteOneDump(cfg, target, proc, written + 1, rec.ExceptionCode, &exInfo))
                    {
                        ++written;
                    }
                    if (written == cfg.count)
                    {
                        std::fprintf(stdout, "%s Dump count reached.\n", TimeStamp().c_str());
                    }
                }
                break;
            }
            case EXIT_PROCESS_DEBUG_EVENT:
                std::fputs("The process has exited.\n", stdout);
                running = false;
                break;
            default:
                break;
            }
            if (running)
            {
                ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);
            }
        }
        DebugActiveProcessStop(target.pid);
        return written;
    }

    [[nodiscard]] inline bool HasConditionTrigger(const Config& cfg) noexcept
    {
        return cfg.cpuAbove || cfg.cpuBelow || cfg.memAbove || cfg.memBelow || cfg.onException || cfg.hung ||
               cfg.terminate;
    }
} // namespace procdump
