// LiveKd - console recreation of the Sysinternals LiveKd utility (v5.65).
//
// Runs the kernel debugger (kd or windbg from the Debugging Tools for Windows)
// against the live system, or writes a live kernel dump to disk. Reverse
// engineered from targets/sysinternals/livekd64.exe. The banner (sub_14000BFE0),
// EULA gate (sub_1400018B0), admin and privilege checks (sub_140008170 /
// sub_140002240), argument parser and dispatch (main, sub_14000BBA0 usage),
// debugger-path discovery (sub_14000B7D0 / sub_14000B570), symbol-path setup
// (sub_14000AAD0) and the native live-dump writer (sub_140009830) were all
// recovered and matched against the original.
//
// Scope and safety: this recreation reproduces only the modern, safe live-dump
// path, NtSystemDebugControl with SysDbgGetLiveKernelDump (command 37), which
// snapshots kernel memory without bugchecking the machine. The original's legacy
// local-kernel-debugging driver path (LiveKdD.SYS, the DeviceIoControl 0x2345xxxx
// codes in sub_140009BE0), the mirror dump, and Hyper-V VM debugging (-hv / -hvl /
// -hvkl / -p) are deliberately out of scope and documented in analysis/LiveKd.md.
// Nothing here can crash, bugcheck, reboot or power off the system.

#include "CommandLine.h"
#include "Debugger.h"
#include "LiveDump.h"
#include "Startup.h"
#include "Usage.h"

namespace
{
    // Banner. Line one is byte-identical to the original (sub_14000BFE0). The
    // original's "Sysinternals" and copyright lines are replaced with the
    // recreation credit, matching the sibling console tools in this repo.
    constexpr char kBannerLine1[] = "\nLiveKd v5.65 - Execute kd/windbg on a live system\n";
    constexpr char kBannerCredit[] = "Independent source-accurate recreation of LiveKd by Sysinternals, through "
                                     "reverse engineering and rewritten in modern C++20.\n";
    constexpr char kBannerAuthor[] = "Author: 0xntpower\n\n";

    // Banner. Always printed. LiveKd has no -nobanner switch.
    void PrintBanner()
    {
        std::fputs(kBannerLine1, stdout);
        std::fputs(kBannerCredit, stdout);
        std::fputs(kBannerAuthor, stdout);
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace livekd;

    PrintBanner();

    bool acceptEula = false;
    for (int i = 1; i < argc; ++i)
    {
        if (ArgEquals(argv[i], L"-accepteula"))
        {
            acceptEula = true;
            break;
        }
    }
    if (!EnsureEula(acceptEula))
    {
        return 1;
    }

    if (!IsRunningElevated())
    {
        std::fputs("Livekd requires administrative rights. You must run it from an\n", stdout);
        std::fputs("elevated command prompt.\n\n", stdout);
        return kExitError;
    }

    if (!EnablePrivilege(L"SeDebugPrivilege"))
    {
        std::fputs("Unable to enable SeDebugPrivilege. You must run LiveKd from an elevated command prompt.\n", stdout);
        return kExitError;
    }

    Config cfg;
    cfg.debuggerBase = "kd";
    ScopedHandle processHandle;
    HANDLE rawProcess = nullptr;
    bool wantUsage = false;
    if (!ParseArgs(argc, argv, cfg, rawProcess, wantUsage))
    {
        return kExitError;
    }
    processHandle.Reset(rawProcess);
    if (wantUsage)
    {
        PrintUsage();
        return kExitError;
    }

    // This recreation reproduces only the native live-dump path, which needs
    // Windows 8.1 or above.
    if (!IsNativeLiveDumpSupported())
    {
        std::fputs("This LiveKd recreation uses the native live-dump API, which requires\n", stdout);
        std::fputs("Windows 8.1 or above.\n\n", stdout);
        return kExitError;
    }

    // -o path: write the live kernel dump and exit without launching a debugger.
    if (cfg.dumpToFile)
    {
        const bool ok =
            WriteLiveKernelDump(cfg.dumpFile, processHandle.Get(), true, cfg.hypervisorPages, cfg.dumpStorageStack);
        if (!ok)
        {
            std::fprintf(stdout, "Error saving dump %s:\n", cfg.dumpFile.c_str());
            return kExitError;
        }
        return 0;
    }

    // Launch path: resolve the debugger, make sure symbols are configured, then
    // run it against a consistent live snapshot.
    std::string debuggerFull;
    if (!cfg.debuggerPath.empty())
    {
        debuggerFull = cfg.debuggerPath;
        if (debuggerFull.find('\\') == std::string::npos)
        {
            std::fputs("A fully qualified debugger path must be supplied.\n", stdout);
            return kExitError;
        }
    }
    else
    {
        const std::string exeName = cfg.useWindbg ? "windbg.exe" : "kd.exe";
        if (!DiscoverDebugger(exeName, debuggerFull))
        {
            std::fprintf(stdout, "Error finding %s:\n", exeName.c_str());
            std::fputs("Use -k <path to 64-bit debugger directory>\\kd.exe\n", stdout);
            return kExitError;
        }
    }

    if (!EnsureSymbolPath())
    {
        return kExitError;
    }

    return LaunchDebugger(cfg, debuggerFull, processHandle.Get());
}
