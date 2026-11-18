// ProcDump - console recreation of the Sysinternals ProcDump utility (v11.0).
//
// Monitors a target process and writes a user-mode crash dump when a trigger
// fires. Reverse engineered from targets/sysinternals/procdump64.exe. The dump
// writer (sub_140013D00 / sub_140014060 / sub_140014620) calls a dynamically
// loaded MiniDumpWriteDump, the dump-type flag words come from sub_140006A50,
// and the status lines, header block, filename scheme and EULA gate are matched
// against the original's captured output.
//
// Scope: the core is reproduced faithfully - argument parsing, process
// selection by PID or name, the MiniDumpWriteDump dump writer with the per-type
// flags, and the trigger set (immediate -n count, CPU -c, memory -m, exception
// -e, hung window -h). The clone (-r), WER (-wer), AeDebug install (-i/-u),
// launch (-x), performance counter (-p), debug-logging (-l) and filter
// (-f/-fx) paths are out of scope and are noted in analysis/ProcDump.md.

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    // --- Banner and version resource strings (InternalName ProcDump, 11.0) ---
    constexpr char kBanner[] =
        "\nProcDump v11.0 - Sysinternals process dump utility\n"
        "Independent source-accurate recreation of ProcDump by Sysinternals, through reverse engineering and rewritten in modern C++20.\n"
        "Author: 0xntpower\n";

    // --- MINIDUMP_TYPE flag words, exactly as the original computes them for a
    // modern dbghelp (ImagehlpApiVersion major 6). sub_140006A50 selects these
    // from the detected version. Mini is the metadata-rich base without full
    // memory, Full adds MiniDumpWithFullMemory, Triage is the filtered set. ---
    constexpr DWORD kFlagsMini = 0x00469925;
    constexpr DWORD kFlagsFull = 0x00469927;
    constexpr DWORD kFlagsTriage = 0x001285A0;
    constexpr DWORD kFlagsMiniPlus = 0x00469925;
    constexpr DWORD kFlagsCallback = 0x00469925;

    // The original retries with MiniDumpWithIptTrace cleared when the first
    // MiniDumpWriteDump fails with E_INVALIDARG on a host whose dbghelp predates
    // that flag (sub_140014620).
    constexpr DWORD kMiniDumpWithIptTrace = 0x00400000;

    constexpr int kDefaultSeconds = 10;
    constexpr int kHungTimeoutMs = 5000;
    constexpr int kExitError = 127;
    constexpr unsigned long long kBytesPerMb = 1024000ULL;

    enum class DumpType
    {
        Mini = 0,
        Full = 1,
        Triage = 2,
        MiniPlus = 3,
        Custom = 4,
        Callback = 5
    };

    using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
        PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
        PMINIDUMP_CALLBACK_INFORMATION);

    // RAII wrapper around a Win32 handle.
    class ScopedHandle
    {
    public:
        ScopedHandle() noexcept = default;
        explicit ScopedHandle(HANDLE handle) noexcept : handle_(handle) {}
        ~ScopedHandle()
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle_);
            }
        }
        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;
        ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }
        ScopedHandle& operator=(ScopedHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset(other.handle_);
                other.handle_ = nullptr;
            }
            return *this;
        }
        void Reset(HANDLE handle) noexcept
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle_);
            }
            handle_ = handle;
        }
        [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
        [[nodiscard]] bool Valid() const noexcept
        {
            return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE handle_ = nullptr;
    };

    struct Config
    {
        DumpType type = DumpType::Mini;
        bool typeChosen = false;
        DWORD customMask = 0;
        std::wstring callbackDll;

        int count = 1;                 // -n
        int seconds = kDefaultSeconds; // -s

        bool cpuAbove = false;         // -c
        bool cpuBelow = false;         // -cl
        int cpuThreshold = 0;
        bool singleCore = false;       // -u

        bool memAbove = false;         // -m
        bool memBelow = false;         // -ml
        int commitThresholdMb = 0;

        bool onException = false;      // -e
        bool firstChance = false;      // -e 1
        bool breakpoints = false;      // -b

        bool hung = false;             // -h
        bool terminate = false;        // -t

        bool overwrite = false;        // -o
        bool wait = false;             // -w

        std::wstring target;
        std::wstring dumpArg;
        bool hasDumpArg = false;
    };

    struct Target
    {
        DWORD pid = 0;
        std::wstring name;       // base name, on-disk case preserved
        std::wstring imagePath;  // full image path
    };

    // --- Narrow conversion for byte-faithful console output. The original is a
    // Unicode app but writes narrow bytes to a redirected stdout, which for the
    // ASCII names and paths here is identical. ---
    [[nodiscard]] std::string Narrow(const std::wstring& wide)
    {
        if (wide.empty())
        {
            return {};
        }
        const int needed = WideCharToMultiByte(CP_ACP, 0, wide.c_str(),
            static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_ACP, 0, wide.c_str(), static_cast<int>(wide.size()),
            out.data(), needed, nullptr, nullptr);
        return out;
    }

    [[nodiscard]] bool ArgEquals(const wchar_t* arg, const wchar_t* dash) noexcept
    {
        if (_wcsicmp(arg, dash) == 0)
        {
            return true;
        }
        // The original accepts both the '-' and '/' spellings of every switch.
        if (arg[0] == L'/' && dash[0] == L'-')
        {
            return _wcsicmp(arg + 1, dash + 1) == 0;
        }
        return false;
    }

    // Remove the first argv entry matching the switch, shifting the rest down.
    [[nodiscard]] bool StripSwitch(int& argc, wchar_t** argv, const wchar_t* dash)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (ArgEquals(argv[i], dash))
            {
                for (int j = i; j < argc - 1; ++j)
                {
                    argv[j] = argv[j + 1];
                }
                --argc;
                return true;
            }
        }
        return false;
    }

    // --- EULA gate. HKCU\Software\Sysinternals\ProcDump\EulaAccepted mirrors the
    // standalone Sysinternals pattern (the suite-wide flag is also honoured). ---
    [[nodiscard]] bool EulaFlagSet(HKEY root, const wchar_t* sub)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key)
            != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0;
        DWORD cb = sizeof(data);
        const LSTATUS st = RegQueryValueExW(key, L"EulaAccepted", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(&data), &cb);
        RegCloseKey(key);
        return st == ERROR_SUCCESS && data != 0;
    }

    void WriteEulaAccepted()
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Sysinternals\\ProcDump",
            0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            RegSetValueExW(key, L"EulaAccepted", 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&one), sizeof(one));
            RegCloseKey(key);
        }
    }

    [[nodiscard]] bool EnsureEula(bool acceptFlag)
    {
        if (acceptFlag
            || EulaFlagSet(HKEY_LOCAL_MACHINE, L"Software\\Sysinternals")
            || EulaFlagSet(HKEY_CURRENT_USER, L"Software\\Sysinternals")
            || EulaFlagSet(HKEY_CURRENT_USER, L"Software\\Sysinternals\\ProcDump"))
        {
            WriteEulaAccepted();
            return true;
        }
        std::fputs("\nProcDump License Agreement\n", stderr);
        std::fputs("This is the first run of this program. "
            "You must accept EULA to continue.\n", stderr);
        std::fputs("Use -accepteula to accept EULA.\n\n", stderr);
        return false;
    }

    void PrintUsage()
    {
        static const char* const lines[] = {
            "Monitors a process and writes a dump file when the process exceeds the\n",
            "specified criteria or has an exception.\n",
            "\n",
            "Capture Usage: \n",
            "   procdump.exe [-mm] [-ma] [-mt] [-mp] [-mc <Mask>] [-md <Callback_DLL>] [-mk]\n",
            "                [-n <Count>]\n",
            "                [-s <Seconds>]\n",
            "                [-c|-cl <CPU_Usage> [-u]]\n",
            "                [-m|-ml <Commit_Usage>]\n",
            "                [-p|-pl <Counter> <Threshold>]\n",
            "                [-h]\n",
            "                [-e [1] [-g] [-b] [-ld] [-ud] [-ct] [-et]]\n",
            "                [-l]\n",
            "                [-t]\n",
            "                [-f  <Include_Filter>, ...]\n",
            "                [-fx <Exclude_Filter>, ...]\n",
            "                [-dc <Comment>]\n",
            "                [-o]\n",
            "                [-r [1..5] [-a]]\n",
            "                [-at <Timeout>]\n",
            "                [-wer]\n",
            "                [-64]\n",
            "                {\n",
            "                 {{[-w] <Process_Name> | <Service_Name> | <PID>} [<Dump_File> | <Dump_Folder>]}\n",
            "                |\n",
            "                 {-x <Dump_Folder> <Image_File> [Argument, ...]}\n",
            "                }\n",
            "Install Usage: \n",
            "   procdump.exe -i [Dump_Folder]\n",
            "                [-mm] [-ma] [-mt] [-mp] [-mc <Mask>] [-md <Callback_DLL>] [-mk]\n",
            "                [-r]\n",
            "                [-at <Timeout>]\n",
            "                [-k]\n",
            "                [-wer]\n",
            "Uninstall Usage: \n",
            "   procdump.exe -u\n",
            "\n",
            "Dump Types:\n",
            "   -mm     Write a 'Mini' dump file. (default)\n",
            "           - Includes directly and indirectly referenced memory (stacks and what they reference).\n",
            "           - Includes all metadata (Process, Thread, Module, Handle, Address Space, etc.).\n",
            "   -ma     Write a 'Full' dump file.\n",
            "           - Includes all memory (Image, Mapped and Private).\n",
            "           - Includes all metadata (Process, Thread, Module, Handle, Address Space, etc.).\n",
            "   -mt     Write a 'Triage' dump file.\n",
            "           - Includes directly referenced memory (stacks).\n",
            "           - Includes limited metadata (Process, Thread, Module and Handle).\n",
            "           - Removal of sensitive information is attempted but not guaranteed.\n",
            "   -mp     Write a 'MiniPlus' dump file.\n",
            "           - Includes all Private memory and all Read/Write Image or Mapped memory.\n",
            "           - Includes all metadata (Process, Thread, Module, Handle, Address Space, etc.).\n",
            "           - To minimize size, the largest Private memory area over 512MB is excluded.\n",
            "             A memory area is defined as the sum of same-sized memory allocations.\n",
            "             The dump is as detailed as a Full dump but 10%-75% the size.\n",
            "           - Note: CLR processes are dumped as Full (-ma) due to debugging limitations.\n",
            "   -mc     Write a 'Custom' dump file.\n",
            "           - Includes the memory and metadata defined by the specified MINIDUMP_TYPE mask (Hex).\n",
            "   -md     Write a 'Callback' dump file.\n",
            "           - Includes the memory defined by the MiniDumpWriteDump callback routine\n",
            "             named MiniDumpCallbackRoutine of the specified DLL.\n",
            "           - Includes all metadata (Process, Thread, Module, Handle, Address Space, etc.).\n",
            "   -mk     Also write a 'Kernel' dump file.\n",
            "           - Includes the kernel stacks of the threads in the process.\n",
            "           - OS doesn't support a kernel dump (-mk) when using a clone (-r).\n",
            "           - When using multiple dump sizes, a kernel dump is taken for each dump size.\n",
            "\n",
            "Conditions:\n",
            "   -a      Avoid outage. Requires -r. If the trigger will cause the target\n",
            "           to suspend for a prolonged time due to an exceeded concurrent\n",
            "           dump limit, the trigger will be skipped.\n",
            "   -at     Avoid outage at Timeout. Cancel the trigger's collection at N seconds.\n",
            "   -b      Treat debug breakpoints as exceptions (otherwise ignore them).\n",
            "   -c      CPU threshold above which to create a dump of the process.\n",
            "   -cl     CPU threshold below which to create a dump of the process.\n",
            "   -dc     Add the specified string to the generated Dump Comment.\n",
            "   -e      Write a dump when the process encounters an unhandled exception.\n",
            "           Include the 1 to create dump on first chance exceptions.\n",
            "           Add -ld to create a dump when a DLL (module) is loaded (filtering applies).\n",
            "           Add -ud to create a dump when a DLL (module) is unloaded (filtering applies).\n",
            "           Add -ct to create a dump when a thread is created.\n",
            "           Add -et to create a dump when a thread exits.\n",
            "   -f      Filter (include) on the content of exceptions, debug logging and filename at DLL load/unload.\n",
            "           Wildcards (*) are supported.\n",
            "   -fx     Filter (exclude) on the content of exceptions, debug logging and filename at DLL load/unload.\n",
            "           Wildcards (*) are supported.\n",
            "   -g      Run as a native debugger in a managed process (no interop).\n",
            "   -h      Write dump if process has a hung window (does not respond to\n",
            "           window messages for at least 5 seconds).\n",
            "   -k      Kill the process after cloning (-r), or at end of dump collection.\n",
            "   -l      Display the debug logging of the process.\n",
            "   -m      Memory commit threshold in MB at which to create a dump.\n",
            "   -ml     Trigger when memory commit drops below specified MB value.\n",
            "   -n      Number of dumps to write before exiting.\n",
            "   -o      Overwrite an existing dump file.\n",
            "   -p      Trigger when the Performance Counter is at, or exceeds, the specified Threshold.\n",
            "           Some Counters and/or Instance Names can be case-sensitive.\n",
            "   -pl     Trigger when the Performance Counter falls below the specified Threshold.\n",
            "   -r      Dump using a clone. Concurrent limit is optional (default 1, max 5).\n",
            "           OS doesn't support a kernel dump (-mk) when using a clone (-r).\n",
            "           CAUTION: a high concurrency value may impact system performance.\n",
            "           - Windows 7   : Uses Reflection. OS doesn't support -e.\n",
            "           - Windows 8.0 : Uses Reflection. OS doesn't support -e.\n",
            "           - Windows 8.1+: Uses PSS. All trigger types are supported.\n",
            "   -s      Consecutive seconds before dump is written (default is 10).\n",
            "   -t      Write a dump when the process terminates.\n",
            "   -u      Treat CPU usage relative to a single core (used with -c).\n",
            "   -w      Wait for the specified process to launch if it's not running.\n",
            "   -wer    Queue the (largest) dump to Windows Error Reporting.\n",
            "   -x      Launch the specified image with optional arguments. \n",
            "           If it is a Store Application or Package, ProcDump will start\n",
            "           on the next activation (only).\n",
            "   -64     By default ProcDump will capture a 32-bit dump of a 32-bit process\n",
            "           when running on 64-bit Windows. This option overrides to create a\n",
            "           64-bit dump. Only use for WOW64 subsystem debugging.\n",
            "\n",
            "Install/Uninstall:\n",
            "   -i      Install ProcDump as the AeDebug postmortem debugger.\n",
            "           Only -mm, -ma, -mt, -mp, -mc, -md and -r are supported as additional options.\n",
            "           Uninstall (-u only) restores the previous configuration.\n",
            "   -u      As the only option, Uninstalls ProcDump as the AeDebug postmortem debugger.\n",
            "\n",
            "License Agreement:\n",
            "   Use the -accepteula command line option to automatically accept the\n",
            "   Sysinternals license agreement.\n",
            "\n",
            "Automated Termination:\n",
            "   -cancel <Target Process PID>\n",
            "           Using this option or setting an event with the name \"ProcDump-<PID>\"\n",
            "           is the same as typing Ctrl+C to gracefully terminate ProcDump.\n",
            "           Graceful termination ensures the process is resumed if a capture is active.\n",
            "           The cancellation applies to ALL ProcDump instances monitoring the process.\n",
            "\n",
            "Filename:\n",
            "   Default dump filename: PROCESSNAME_YYMMDD_HHMMSS.dmp\n",
            "   The following substitutions are supported:\n",
            "           PROCESSNAME   Process Name\n",
            "           PID           Process ID\n",
            "           EXCEPTIONCODE Exception Code\n",
            "           YYMMDD        Year/Month/Day\n",
            "           HHMMSS        Hour/Minute/Second\n",
            "\n",
            "Examples:\n",
            "   Use -? -e to see example command lines.\n",
            "\n",
            nullptr
        };
        for (int i = 0; lines[i] != nullptr; ++i)
        {
            std::fputs(lines[i], stdout);
        }
    }

    [[nodiscard]] DWORD DumpFlags(const Config& cfg)
    {
        switch (cfg.type)
        {
        case DumpType::Mini:     return kFlagsMini;
        case DumpType::Full:     return kFlagsFull;
        case DumpType::Triage:   return kFlagsTriage;
        case DumpType::MiniPlus: return kFlagsMiniPlus;
        case DumpType::Callback: return kFlagsCallback;
        case DumpType::Custom:   return cfg.customMask;
        }
        return kFlagsMini;
    }

    // The estimated-size "writing" status line is shown only for the Full (-ma)
    // dump. MiniPlus, Triage, Mini, and a Custom mask (even one with the full
    // memory bit) do not print it in the original.
    [[nodiscard]] bool ShowsEstimate(const Config& cfg) noexcept
    {
        return cfg.type == DumpType::Full;
    }

    [[nodiscard]] std::string TimeStamp()
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char buf[16] = {};
        std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d]", st.wHour, st.wMinute, st.wSecond);
        return buf;
    }

    // --- Process selection ---
    [[nodiscard]] bool AllDigits(const std::wstring& s) noexcept
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

    void PrintNotFoundHint()
    {
        std::fputs("Try elevating the command prompt or using PsExec to make one as SYSTEM.\n", stdout);
        std::fputs("\tpsexec.exe -s -d -i cmd.exe\n", stdout);
        std::fputs("\tprocdump.exe -accepteula ...\n", stdout);
        std::fputc('\n', stdout);
    }

    void FillImageInfo(DWORD pid, Target& out)
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
            out.name = (slash == std::wstring::npos)
                ? out.imagePath
                : out.imagePath.substr(slash + 1);
        }
    }

    // Count name matches among running processes. A match is case-insensitive
    // against the image base name, with or without a trailing ".exe".
    [[nodiscard]] int FindByName(const std::wstring& wanted, DWORD& foundPid)
    {
        std::wstring withExe = wanted;
        const bool hasExt = wanted.size() >= 4
            && _wcsicmp(wanted.c_str() + wanted.size() - 4, L".exe") == 0;
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
                if (_wcsicmp(entry.szExeFile, wanted.c_str()) == 0
                    || _wcsicmp(entry.szExeFile, withExe.c_str()) == 0)
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
    [[nodiscard]] bool ResolveTarget(const Config& cfg, Target& out)
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

    // --- Dump file naming ---
    [[nodiscard]] std::wstring ExpandMask(const std::wstring& mask, const Target& target,
        DWORD exceptionCode)
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t ymd[8] = {};
        wchar_t hms[8] = {};
        std::swprintf(ymd, std::size(ymd), L"%02d%02d%02d", st.wYear % 100, st.wMonth, st.wDay);
        std::swprintf(hms, std::size(hms), L"%02d%02d%02d", st.wHour, st.wMinute, st.wSecond);
        wchar_t pid[16] = {};
        std::swprintf(pid, std::size(pid), L"%u", target.pid);
        wchar_t code[16] = {};
        std::swprintf(code, std::size(code), L"%08X", exceptionCode);

        struct Sub { const wchar_t* token; const wchar_t* value; };
        const Sub subs[] = {
            { L"PROCESSNAME", target.name.c_str() },
            { L"EXCEPTIONCODE", code },
            { L"PID", pid },
            { L"YYMMDD", ymd },
            { L"HHMMSS", hms },
        };

        std::wstring out;
        for (std::wstring::size_type i = 0; i < mask.size();)
        {
            bool replaced = false;
            for (const Sub& s : subs)
            {
                const size_t len = std::wcslen(s.token);
                if (mask.compare(i, len, s.token) == 0)
                {
                    out += s.value;
                    i += len;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
            {
                out += mask[i];
                ++i;
            }
        }
        return out;
    }

    [[nodiscard]] bool IsDirectoryArg(const std::wstring& arg)
    {
        if (arg.empty())
        {
            return false;
        }
        const wchar_t last = arg.back();
        if (last == L'\\' || last == L'/')
        {
            return true;
        }
        const DWORD attrs = GetFileAttributesW(arg.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    // Build the dump path. A folder argument is concatenated verbatim with a
    // backslash and the expanded default mask, which reproduces the original's
    // "folder/\name" when the folder already ends in a separator. A file
    // argument is treated as a name or mask. Without an argument the current
    // directory is used.
    [[nodiscard]] std::wstring BuildDumpPath(const Config& cfg, const Target& target,
        DWORD exceptionCode)
    {
        const std::wstring defaultMask = L"PROCESSNAME_YYMMDD_HHMMSS";
        std::wstring base;
        if (cfg.hasDumpArg && !IsDirectoryArg(cfg.dumpArg))
        {
            base = ExpandMask(cfg.dumpArg, target, exceptionCode);
            if (base.size() < 4 || _wcsicmp(base.c_str() + base.size() - 4, L".dmp") != 0)
            {
                base += L".dmp";
            }
        }
        else
        {
            std::wstring folder;
            if (cfg.hasDumpArg)
            {
                folder = cfg.dumpArg;
            }
            else
            {
                wchar_t cwd[MAX_PATH] = {};
                GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
                folder = cwd;
            }
            base = folder + L"\\" + ExpandMask(defaultMask, target, exceptionCode) + L".dmp";
        }

        if (cfg.overwrite)
        {
            return base;
        }

        // Without -o the original appends -1, -2, ... before the extension on a
        // collision.
        if (GetFileAttributesW(base.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return base;
        }
        const std::wstring::size_type dot = base.find_last_of(L'.');
        const std::wstring stem = (dot == std::wstring::npos) ? base : base.substr(0, dot);
        const std::wstring ext = (dot == std::wstring::npos) ? std::wstring() : base.substr(dot);
        for (int n = 1; n < 1000; ++n)
        {
            std::wstring candidate = stem + L"-" + std::to_wstring(n) + ext;
            if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                return candidate;
            }
        }
        return base;
    }

    // Estimate the full-dump size from the target's committed regions. Used for
    // the "writing: Estimated dump file size is N MB." line on full-memory types.
    [[nodiscard]] unsigned long long EstimateCommitted(HANDLE proc)
    {
        unsigned long long total = 0;
        MEMORY_BASIC_INFORMATION mbi{};
        unsigned char* address = nullptr;
        while (VirtualQueryEx(proc, address, &mbi, sizeof(mbi)) == sizeof(mbi))
        {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_GUARD) == 0
                && mbi.Protect != PAGE_NOACCESS)
            {
                total += mbi.RegionSize;
            }
            unsigned char* next = static_cast<unsigned char*>(mbi.BaseAddress) + mbi.RegionSize;
            if (next <= address)
            {
                break;
            }
            address = next;
        }
        return total;
    }

    // --- Dump writer. Loads dbghelp, resolves MiniDumpWriteDump, writes one
    // dump and prints the initiated / writing / complete status lines. ---
    [[nodiscard]] bool WriteOneDump(const Config& cfg, const Target& target, HANDLE proc,
        int dumpNumber, DWORD exceptionCode, PMINIDUMP_EXCEPTION_INFORMATION exInfo)
    {
        static MiniDumpWriteDumpFn miniDump = nullptr;
        if (miniDump == nullptr)
        {
            const HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr,
                LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (dbghelp == nullptr)
            {
                std::fputs("Unable to load debug help.\n", stdout);
                return false;
            }
            miniDump = reinterpret_cast<MiniDumpWriteDumpFn>(
                reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
            if (miniDump == nullptr)
            {
                std::fputs("Unable to import the 'MiniDumpWriteDump' function from debug help.\n", stdout);
                return false;
            }
        }

        const std::wstring path = BuildDumpPath(cfg, target, exceptionCode);
        std::fprintf(stdout, "%s Dump %d initiated: %s\n",
            TimeStamp().c_str(), dumpNumber, Narrow(path).c_str());

        ScopedHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            cfg.overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.Valid())
        {
            std::fprintf(stdout, "Error creating dump file:\n");
            return false;
        }

        if (ShowsEstimate(cfg))
        {
            const unsigned long long estimate = EstimateCommitted(proc);
            const unsigned long mb = static_cast<unsigned long>(
                (estimate + kBytesPerMb - 1) / kBytesPerMb);
            std::fprintf(stdout, "%s Dump %d writing: Estimated dump file size is %lu MB.\n",
                TimeStamp().c_str(), dumpNumber, mb);
        }

        DWORD flags = DumpFlags(cfg);
        const DWORD start = GetTickCount();
        BOOL ok = miniDump(proc, target.pid, file.Get(),
            static_cast<MINIDUMP_TYPE>(flags), exInfo, nullptr, nullptr);
        if (ok == 0)
        {
            DWORD err = GetLastError();
            if (err > 0)
            {
                err = (err & 0xFFFF) | 0x80070000;
            }
            if (err == 0x80070057 && (flags & kMiniDumpWithIptTrace) != 0)
            {
                flags &= ~kMiniDumpWithIptTrace;
                SetFilePointer(file.Get(), 0, nullptr, FILE_BEGIN);
                ok = miniDump(proc, target.pid, file.Get(),
                    static_cast<MINIDUMP_TYPE>(flags), exInfo, nullptr, nullptr);
            }
            if (ok == 0)
            {
                const DWORD finalErr = GetLastError();
                DWORD shown = finalErr;
                if (shown > 0)
                {
                    shown = (shown & 0xFFFF) | 0x80070000;
                }
                std::fprintf(stdout, "%s Dump %d error: Error writing dump file: 0x%08X\n",
                    TimeStamp().c_str(), dumpNumber, shown);
                return false;
            }
        }
        const DWORD elapsed = GetTickCount() - start;

        LARGE_INTEGER fileSize{};
        GetFileSizeEx(file.Get(), &fileSize);
        const unsigned long mb = static_cast<unsigned long>(
            (static_cast<unsigned long long>(fileSize.QuadPart) + kBytesPerMb - 1) / kBytesPerMb);
        std::fprintf(stdout, "%s Dump %d complete: %lu MB written in %0.1f seconds\n",
            TimeStamp().c_str(), dumpNumber, mb, static_cast<double>(elapsed) / 1000.0);
        return true;
    }

    // --- Monitoring header (printed only in monitoring mode). ---
    void PrintHeader(const Config& cfg, const Target& target)
    {
        std::fprintf(stdout, "Process:               %s (%u)\n",
            Narrow(target.name).c_str(), target.pid);
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

    [[nodiscard]] bool ProcessAlive(HANDLE proc) noexcept
    {
        DWORD code = 0;
        return GetExitCodeProcess(proc, &code) != 0 && code == STILL_ACTIVE;
    }

    // --- CPU sampling. Percentage is the process busy time over the wall-clock
    // interval, divided across the cores unless -u scopes it to a single core. ---
    [[nodiscard]] unsigned long long FileTimeToU64(const FILETIME& ft) noexcept
    {
        ULARGE_INTEGER u{};
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u.QuadPart;
    }

    [[nodiscard]] int SampleCpu(HANDLE proc, unsigned long long& prevProc,
        unsigned long long& prevWall, bool singleCore)
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

    [[nodiscard]] int SampleCommitMb(HANDLE proc)
    {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(proc, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
            sizeof(pmc)) == 0)
        {
            return 0;
        }
        return static_cast<int>(pmc.PrivateUsage / (1024ULL * 1024ULL));
    }

    [[nodiscard]] bool HasHungWindow(DWORD pid)
    {
        struct Ctx { DWORD pid; bool hung; } ctx{ pid, false };
        EnumWindows([](HWND hwnd, LPARAM param) -> BOOL
            {
                Ctx* c = reinterpret_cast<Ctx*>(param);
                DWORD windowPid = 0;
                GetWindowThreadProcessId(hwnd, &windowPid);
                if (windowPid != c->pid || IsWindowVisible(hwnd) == 0)
                {
                    return TRUE;
                }
                DWORD_PTR result = 0;
                const LRESULT ok = SendMessageTimeoutW(hwnd, WM_NULL, 0, 0,
                    SMTO_ABORTIFHUNG | SMTO_BLOCK, kHungTimeoutMs, &result);
                if (ok == 0)
                {
                    c->hung = true;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&ctx));
        return ctx.hung;
    }

    [[nodiscard]] const char* ExceptionName(DWORD code) noexcept
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
        case EXCEPTION_BREAKPOINT:            return "BREAKPOINT";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
        case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
        default:                              return "EXCEPTION";
        }
    }

    // --- Trigger loops. Returns the number of dumps written. ---
    [[nodiscard]] int RunImmediate(const Config& cfg, const Target& target, HANDLE proc)
    {
        int written = 0;
        if (WriteOneDump(cfg, target, proc, 1, 0, nullptr))
        {
            written = 1;
        }
        std::fprintf(stdout, "%s Dump count reached.\n", TimeStamp().c_str());
        return written;
    }

    [[nodiscard]] int RunTimed(const Config& cfg, const Target& target, HANDLE proc)
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
    [[nodiscard]] int RunCpuTrigger(const Config& cfg, const Target& target, HANDLE proc)
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
            const bool condition = cfg.cpuAbove ? (cpu >= cfg.cpuThreshold)
                : (cpu <= cfg.cpuThreshold);
            held = condition ? held + 1 : 0;
            if (held < 1)
            {
                continue;
            }
            const char* reason = (held >= cfg.seconds) ? "(Trigger)" : "";
            std::fprintf(stdout, "%s CPU: %d%% %lds %s\n",
                TimeStamp().c_str(), cpu, static_cast<long>(held), reason);
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
    [[nodiscard]] int RunLevelTrigger(const Config& cfg, const Target& target, HANDLE proc)
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
                const bool condition = cfg.memAbove ? (mb >= cfg.commitThresholdMb)
                    : (mb <= cfg.commitThresholdMb);
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
    [[nodiscard]] int RunExceptionTrigger(const Config& cfg, const Target& target, HANDLE proc)
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
                const bool wanted = (ev.u.Exception.dwFirstChance == 0 || cfg.firstChance)
                    && (!isBreakpoint || cfg.breakpoints);
                if (wanted)
                {
                    std::fprintf(stdout, "%s Exception: %08X.%s\n", TimeStamp().c_str(),
                        rec.ExceptionCode, ExceptionName(rec.ExceptionCode));

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

    [[nodiscard]] bool HasConditionTrigger(const Config& cfg) noexcept
    {
        return cfg.cpuAbove || cfg.cpuBelow || cfg.memAbove || cfg.memBelow
            || cfg.onException || cfg.hung || cfg.terminate;
    }

    // Parse one integer argument, returning false on a non-numeric token.
    [[nodiscard]] bool ParseInt(const wchar_t* s, int& out)
    {
        wchar_t* end = nullptr;
        const long value = std::wcstol(s, &end, 10);
        if (end == s || (end != nullptr && *end != L'\0'))
        {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    }

    [[nodiscard]] bool ParseArgs(int argc, wchar_t** argv, Config& cfg)
    {
        for (int i = 1; i < argc; ++i)
        {
            const wchar_t* a = argv[i];
            if (a[0] == L'-' || a[0] == L'/')
            {
                if (ArgEquals(a, L"-mm")) { cfg.type = DumpType::Mini; cfg.typeChosen = true; }
                else if (ArgEquals(a, L"-ma")) { cfg.type = DumpType::Full; cfg.typeChosen = true; }
                else if (ArgEquals(a, L"-mt")) { cfg.type = DumpType::Triage; cfg.typeChosen = true; }
                else if (ArgEquals(a, L"-mp")) { cfg.type = DumpType::MiniPlus; cfg.typeChosen = true; }
                else if (ArgEquals(a, L"-mc"))
                {
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    cfg.customMask = static_cast<DWORD>(std::wcstoul(argv[++i], nullptr, 16));
                    cfg.type = DumpType::Custom;
                    cfg.typeChosen = true;
                }
                else if (ArgEquals(a, L"-md"))
                {
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    cfg.callbackDll = argv[++i];
                    cfg.type = DumpType::Callback;
                    cfg.typeChosen = true;
                }
                else if (ArgEquals(a, L"-n"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.count) || cfg.count < 1)
                    {
                        return false;
                    }
                }
                else if (ArgEquals(a, L"-s"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.seconds) || cfg.seconds < 1)
                    {
                        return false;
                    }
                }
                else if (ArgEquals(a, L"-c"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.cpuThreshold))
                    {
                        return false;
                    }
                    cfg.cpuAbove = true;
                }
                else if (ArgEquals(a, L"-cl"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.cpuThreshold))
                    {
                        return false;
                    }
                    cfg.cpuBelow = true;
                }
                else if (ArgEquals(a, L"-m"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.commitThresholdMb))
                    {
                        return false;
                    }
                    cfg.memAbove = true;
                }
                else if (ArgEquals(a, L"-ml"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.commitThresholdMb))
                    {
                        return false;
                    }
                    cfg.memBelow = true;
                }
                else if (ArgEquals(a, L"-e")) { cfg.onException = true; }
                else if (ArgEquals(a, L"-b")) { cfg.breakpoints = true; }
                else if (ArgEquals(a, L"-h")) { cfg.hung = true; }
                else if (ArgEquals(a, L"-t")) { cfg.terminate = true; }
                else if (ArgEquals(a, L"-u")) { cfg.singleCore = true; }
                else if (ArgEquals(a, L"-o")) { cfg.overwrite = true; }
                else if (ArgEquals(a, L"-w")) { cfg.wait = true; }
                else
                {
                    return false;
                }
            }
            else if (a[0] == L'1' && a[1] == L'\0' && cfg.onException)
            {
                cfg.firstChance = true;
            }
            else
            {
                if (cfg.target.empty())
                {
                    cfg.target = a;
                }
                else if (!cfg.hasDumpArg)
                {
                    cfg.dumpArg = a;
                    cfg.hasDumpArg = true;
                }
                else
                {
                    return false;
                }
            }
        }
        return !cfg.target.empty();
    }
}

int wmain(int argc, wchar_t** argv)
{
    const bool noBanner = StripSwitch(argc, argv, L"-nobanner");
    const bool acceptEula = StripSwitch(argc, argv, L"-accepteula");

    if (!noBanner)
    {
        std::fputs(kBanner, stdout);
        std::fputc('\n', stdout);
    }

    if (!EnsureEula(acceptEula))
    {
        return kExitError;
    }

    for (int i = 1; i < argc; ++i)
    {
        if (ArgEquals(argv[i], L"-?") || ArgEquals(argv[i], L"-help"))
        {
            PrintUsage();
            return kExitError;
        }
    }

    Config cfg;
    if (argc < 2 || !ParseArgs(argc, argv, cfg))
    {
        PrintUsage();
        return kExitError;
    }

    if ((cfg.breakpoints) && !cfg.onException)
    {
        std::fputs("Breakpoints (-b) require exception monitoring (-e).\n", stdout);
        return kExitError;
    }
    if (cfg.singleCore && !cfg.cpuAbove && !cfg.cpuBelow)
    {
        std::fputs("Single core scale (-u) requires CPU usage (-c).\n", stdout);
        return kExitError;
    }

    Target target;
    if (!ResolveTarget(cfg, target))
    {
        return kExitError;
    }

    ScopedHandle proc(OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, target.pid));
    if (!proc.Valid())
    {
        proc.Reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            FALSE, target.pid));
    }
    if (!proc.Valid())
    {
        std::fprintf(stdout, "Error opening %s (%u):\n", Narrow(target.name).c_str(), target.pid);
        return kExitError;
    }

    const bool monitoring = cfg.count > 1 || HasConditionTrigger(cfg);
    if (monitoring)
    {
        PrintHeader(cfg, target);
    }

    int written = 0;
    if (cfg.onException)
    {
        written = RunExceptionTrigger(cfg, target, proc.Get());
    }
    else if (cfg.cpuAbove || cfg.cpuBelow)
    {
        written = RunCpuTrigger(cfg, target, proc.Get());
    }
    else if (cfg.memAbove || cfg.memBelow || cfg.hung)
    {
        written = RunLevelTrigger(cfg, target, proc.Get());
    }
    else if (monitoring)
    {
        written = RunTimed(cfg, target, proc.Get());
    }
    else
    {
        written = RunImmediate(cfg, target, proc.Get());
    }

    std::fputc('\n', stdout);
    return written;
}
