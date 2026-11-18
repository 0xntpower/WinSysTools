// PsSuspend -- suspends or resumes a process by PID or name, on the local machine
// or a remote one. Source-accurate C++20 console reconstruction of Sysinternals
// pssuspend64.exe v1.08. main is sub_140005B30. The argument parse, the local and
// remote engine, and the result reporting live in the headers beside this file.
// The shared banner, EULA gate, error printer, and remote connect come from
// ../common/PsTools.h, reused read only. See analysis/PsSuspend.md for the sub_ map.

#include "CommandLine.h"
#include "Report.h"
#include "SuspendEngine.h"

namespace
{
    // Version-resource fields of pssuspend64.exe v1.08, used for the banner and the
    // EULA registry key. Fixed here, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsSuspend",
        "1.08",
        "Process Suspender",
        "Independent source-accurate recreation of PsSuspend by Sysinternals, through reverse engineering and "
        "rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name, matching HKCU\Software\Sysinternals\PsSuspend.
    constexpr char kApp[] = "PsSuspend";
} // namespace

int main(int argc, char** argv)
{
    using namespace pssuspend;

    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    Options o;
    if (!Parse(argc, argv, o))
    {
        PrintUsage();
        return -1;
    }

    // A remote target that is really the local machine runs the local path, the
    // same test sub_140005B30 makes before choosing the service worker.
    const std::string local = pstools::LocalComputerName();
    const bool isRemote = o.haveComputer && ::_stricmp(o.computer.c_str(), local.c_str()) != 0;

    // -u without -p prompts for a hidden password, as the original does.
    if (o.haveUser && !o.havePassword)
    {
        o.password = pstools::PromptHiddenPassword();
        o.havePassword = true;
    }

    if (isRemote)
    {
        int count = 0;
        if (!RemoteAct(o, count))
        {
            return -1;
        }
        ReportRemoteSuccess(o, count);
        return 0;
    }

    int count = 0;
    const DWORD err = LocalAct(o, count);
    if (err == 0)
    {
        ReportLocalSuccess(o, count);
        return 0;
    }
    return ReportLocalFailure(o, err);
}
