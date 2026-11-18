// PsExec -- runs a program on a local or remote computer. Source-accurate C++20
// console reconstruction of the Sysinternals PsExec v2.43 (PsExec64.exe).
//
// PsExec is the largest PsTool. This reconstruction reproduces the full command
// line and switch set, and the LOCAL direct-execution path in full. The remote
// path and the local System/interactive paths run through the PSEXESVC helper
// service, reproduced here as a faithful client whose helper-binary auto-deploy
// and encrypted named-pipe stdio are out of scope, the same boundary PsKill drew
// for its PSKLLSVC remote path.
//
// The local direct path runs when the target is the local machine AND none of -h,
// -s, -i, -x are present AND an alternate user is not a service account. Everything
// else routes to the PSEXESVC service path, so -s and -i are reproduced but not
// exercised. The sub_ map and the full dispatch findings live in analysis/PsExec.md.
//
// The shared PsTools front end (banner, EULA gate, error printer, local computer
// name, hidden password prompt, IPC$ credentials, target iteration) lives in
// ../common/PsTools.h and is reused read-only.

#include "CommandLine.h"
#include "LocalExec.h"
#include "ServiceExec.h"

namespace
{
    // Version-resource fields of PsExec v2.43, used for the banner and the EULA
    // registry key. Verified byte-identical against the original banner on stderr.
    constexpr pstools::Banner kBanner{
        "PsExec",
        "2.43",
        "Execute processes remotely",
        "Independent source-accurate recreation of PsExec by Sysinternals, through reverse engineering and rewritten "
        "in modern C++20.",
        "Author: 0xntpower",
    };

    constexpr char kApp[] = "PsExec";
} // namespace

int main(int argc, char** argv)
{
    using namespace psexec;

    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    Options o;
    bool needsService = false;
    if (!ParseCommandLine(argc, argv, o, needsService))
    {
        return -1;
    }

    const std::string local = pstools::LocalComputerName();
    const std::string target = o.computerSpecified ? o.computerList : local;

    // -u without -p prompts for a hidden password, unless the account is a service
    // account that carries no password, which routes to the service path instead.
    if (o.haveUser && !o.havePassword)
    {
        std::string account;
        std::string domain;
        SplitUser(o.user, account, domain);
        if (::_stricmp(domain.c_str(), "NT AUTHORITY") == 0 || ::_stricmp(domain.c_str(), "NT SERVICE") == 0)
        {
            needsService = true;
        }
        else
        {
            o.password = pstools::PromptHiddenPassword();
            o.havePassword = true;
        }
    }

    const bool isLocalTarget = ::_stricmp(target.c_str(), local.c_str()) == 0 || target == "." || target.empty();
    const bool localDirect = isLocalTarget && !o.elevated && !needsService;

    if (localDirect)
    {
        return RunLocal(o);
    }

    int last = 0;
    pstools::ForEachComputer(target, true,
                             [&](const char* computer) -> bool
                             {
                                 last = RunViaService(computer, o);
                                 return last == 0;
                             });
    return last;
}
