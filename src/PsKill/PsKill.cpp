// PsKill: terminates a process by PID or by name, on the local machine or on a
// remote computer, with an optional whole-tree kill. Source-accurate C++20
// console reconstruction of the Sysinternals pskill64.exe v1.17. The RE map
// (sub_ addresses per routine) and validation evidence are in analysis/PsKill.md.
// The shared banner, EULA gate, alternate credentials, error reporting, local
// identity, and hidden password prompt come from ../common/PsTools.h, reused
// read-only.

#include "CommandLine.h"
#include "KillProcess.h"
#include "Usage.h"

namespace
{
    // Version-resource fields of pskill64.exe v1.17, used for the banner and the
    // EULA registry key. The original reads these from its own version resource
    // at run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsKill",
        "1.17",
        "Terminates processes on local or remote systems",
        "Independent source-accurate recreation of PsKill by Sysinternals, through reverse engineering and rewritten "
        "in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name is the version-resource InternalName. The registry key
    // is case-insensitive, so this matches HKCU\Software\Sysinternals\PsKill.
    constexpr char kApp[] = "PsKill";
} // namespace

int main(int argc, char** argv)
{
    using namespace pskill;

    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    Options o;
    if (!ParseCommandLine(argc, argv, o))
    {
        PrintUsage();
        return -1;
    }

    // -u without -p prompts for a hidden password, as the original does.
    if (o.haveUser && !o.havePassword)
    {
        o.password = pstools::PromptHiddenPassword();
        o.havePassword = true;
    }

    const std::string local = pstools::LocalComputerName();
    const bool remote = o.computerSet && ::_stricmp(o.computer.c_str(), local.c_str()) != 0;

    DWORD count = 0;

    if (remote)
    {
        pstools::Targets creds;
        creds.user = o.user;
        creds.password = o.password;
        creds.haveUser = o.haveUser;
        creds.havePassword = o.havePassword;
        if (!KillRemote(o.computer.c_str(), o.name, o.pid, o.tree, creds, count))
        {
            return -1;
        }
        if (count > 1)
        {
            std::printf("\r%d processes named %s killed on %s.\n\n", count, o.name, o.computer.c_str());
        }
        else if (o.name != nullptr)
        {
            std::printf("\rProcess %s killed on %s.\n\n", o.name, o.computer.c_str());
        }
        else
        {
            std::printf("\rProcess %d on %s killed.\n\n", o.pid, o.computer.c_str());
        }
        return 0;
    }

    const DWORD err = KillLocal(o.name, o.pid, o.tree, count);
    if (err == 0)
    {
        if (count <= 1)
        {
            if (o.name != nullptr)
            {
                std::printf("Process %s killed.\n\n", o.name);
            }
            else
            {
                std::printf("Process %d killed.\n\n", o.pid);
            }
        }
        else if (o.tree)
        {
            if (o.name != nullptr)
            {
                std::printf("%d processes named %s and their descendants killed.\n\n", count, o.name);
            }
            else
            {
                std::printf("%d processes descended from and including %d killed.\n\n", count, o.pid);
            }
        }
        else
        {
            std::printf("%d processes named %s killed.\n\n", count, o.name);
        }
        return 0;
    }

    if (o.name != nullptr)
    {
        std::printf("Unable to kill process %s:\n", o.name);
    }
    else
    {
        std::printf("Unable to kill process %d:\n", o.pid);
    }
    if (err == ERROR_INVALID_PARAMETER)
    {
        std::printf("Process does not exist.\n");
        return -1;
    }
    pstools::PrintSystemError(err);
    return -1;
}
