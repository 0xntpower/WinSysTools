// PsInfo: prints system information for a local or remote computer. Source-accurate
// C++20 console reconstruction of PsInfo64.exe v1.79. The RE map (sub_ addresses
// per routine) and validation evidence are in analysis/PsInfo.md. The shared
// banner, EULA gate, target iteration, and error reporting come from
// ../common/PsTools.h, reused read-only.
//
// The field values come from the registry read through the wide APIs, and the
// output goes through wide printf exactly as the original does, so the wide to
// byte conversion on a redirected stream matches the original byte for byte.

#include "CommandLine.h"
#include "SystemInfo.h"

namespace
{
    // Version-resource fields of PsInfo64.exe v1.79, used for the banner and the
    // EULA registry key. The original reads these from its own version resource at
    // run time. Here they are fixed, which does not change the output.
    constexpr pstools::Banner kBanner{
        "PsInfo",
        "1.79",
        "Local and remote system information viewer",
        "Independent source-accurate recreation of PsInfo by Sysinternals, through reverse engineering and rewritten "
        "in modern C++20.",
        "Author: 0xntpower",
    };

    constexpr char kApp[] = "PsInfo";
} // namespace

int main(int argc, char** argv)
{
    using namespace psinfo;

    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::printf("Eula declined.\n\n");
        return -1;
    }

    if (!StripToolFlags(argc, argv))
    {
        PrintUsage();
        return -1;
    }

    pstools::Targets t;
    if (!pstools::ParseCommandLine(argc, argv, t))
    {
        PrintUsage();
        return -1;
    }

    if (t.accountSpecified)
    {
        g_opt.filter = Widen(t.account.c_str());
        g_opt.hasFilter = !g_opt.filter.empty();
    }
    if (t.haveUser)
    {
        g_opt.haveUser = true;
        g_opt.user = Widen(t.user.c_str());
        if (!t.havePassword)
        {
            t.password = pstools::PromptHiddenPassword();
        }
        g_opt.password = Widen(t.password.c_str());
    }

    g_term = g_opt.csv ? g_opt.delimiter : L'\n';

    const std::wstring local = Widen(pstools::LocalComputerName().c_str());
    const bool ok = pstools::ForEachComputer(t.computerList, false,
                                             [&](const char* computer)
                                             {
                                                 return PerComputer(computer, local);
                                             });
    return ok ? 0 : -1;
}
