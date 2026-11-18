// PsLogList: dumps the records of an event log on a local or remote NT system
// through the classic ReadEventLog API, resolving each record's message from the
// source's registered message DLLs. Source-accurate C++20 console reconstruction of
// Sysinternals psloglist64.exe v2.82. The RE map (sub_ addresses per routine) and
// validation evidence are in analysis/PsLogList.md. The shared PsTools front end
// (banner, EULA gate, alternate credentials, system-error printer, local identity,
// hidden password prompt, target iteration) comes from ../common/PsTools.h, reused
// read-only.

#include "CommandLine.h"
#include "EventLog.h"
#include "Usage.h"

namespace
{
    // Version-resource fields of psloglist64.exe v2.82, used for the banner and the
    // EULA registry key. The original reads these from its own version resource at
    // run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsLoglist",
        "2.82",
        "local and remote event log viewer",
        "Independent source-accurate recreation of PsLoglist by Sysinternals, through reverse engineering and "
        "rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name is the version-resource InternalName, matching
    // HKCU\Software\Sysinternals\PsLoglist.
    constexpr char kApp[] = "PsLoglist";
} // namespace

int main(int argc, char** argv)
{
    using namespace psloglist;

    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    Options options;
    if (!ParseCommandLine(argc, argv, options))
    {
        PrintUsage();
        return -1;
    }

    if (options.haveUser && !options.havePassword)
    {
        options.password = pstools::PromptHiddenPassword();
        options.havePassword = true;
    }

    if (options.computerList.empty())
    {
        options.computerList = pstools::LocalComputerName();
    }

    const bool ok = pstools::ForEachComputer(options.computerList, true,
                                             [&options](const char* computer)
                                             {
                                                 return DumpEventLog(computer, options);
                                             });
    return ok ? 0 : 1;
}
