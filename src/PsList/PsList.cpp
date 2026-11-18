// PsList -- lists running processes with CPU and elapsed time, priority, thread
// and handle counts, and memory, plus per-thread detail and a process tree.
// Source-accurate C++20 console reconstruction of Sysinternals PsList64.exe v1.41.
// The column format strings, counter names, data-source choice, and the rest of
// the RE findings are in analysis/PsList.md. The shared banner, EULA gate,
// system-error printer, and local identity come from ../common/PsTools.h, reused read-only.

#include "CommandLine.h"
#include "ProcessPrint.h"

namespace
{
    // Version-resource fields of PsList64.exe v1.41, fixed here. Output is identical.
    constexpr pstools::Banner kBanner{
        "PsList",
        "1.41",
        "Process information lister",
        "Independent source-accurate recreation of PsList by Sysinternals, through reverse engineering and rewritten "
        "in modern C++20.",
        "Author: 0xntpower",
    };

    constexpr char kApp[] = "PsList";
} // namespace

int main(int argc, char** argv)
{
    using namespace pslist;

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
