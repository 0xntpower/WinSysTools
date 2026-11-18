// ProcDump - console recreation of the Sysinternals ProcDump utility (v11.0).
//
// Monitors a target process and writes a user-mode crash dump when a trigger
// fires. Reverse engineered from targets/sysinternals/procdump64.exe. The dump
// writer, the per-type flag words, the status lines, filename scheme and EULA
// gate are matched against the original's captured output. The core triggers
// (immediate, timed, CPU, commit, exception, hung window) are reproduced. The
// clone, WER, AeDebug, launch, performance-counter, logging and filter paths are
// out of scope and noted in analysis/ProcDump.md.

#include "CommandLine.h"
#include "Monitor.h"
#include "Usage.h"

namespace
{
    // Banner and version resource strings (InternalName ProcDump, 11.0).
    constexpr char kBanner[] = "\nProcDump v11.0 - Sysinternals process dump utility\n"
                               "Independent source-accurate recreation of ProcDump by Sysinternals, through reverse "
                               "engineering and rewritten in modern C++20.\n"
                               "Author: 0xntpower\n";

    constexpr int kExitError = 127;
} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace procdump;

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

    ScopedHandle proc(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE, FALSE, target.pid));
    if (!proc.Valid())
    {
        proc.Reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, target.pid));
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
