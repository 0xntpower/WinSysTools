// PsShutdown: shuts down, reboots, logs off, locks, hibernates, suspends, powers
// off, or turns the monitor off on a local or remote machine, after an optional
// countdown and with an optional message. Source-accurate C++20 console
// reconstruction of Sysinternals psshutdown64.exe v2.6. The RE map (sub_
// addresses per routine) and validation evidence are in analysis/PsShutdown.md.
// The shared banner, EULA gate, alternate credentials, system-error printer,
// local identity, hidden password prompt, and target iteration come from
// ../common/PsTools.h, reused read-only. PsShutdown has its own argument grammar,
// so the parser here mirrors sub_140005500 rather than pstools::ParseCommandLine.
//
// SAFETY: the local immediate actions (ExitWindowsEx, SetSuspendState,
// LockWorkStation, the monitor-off broadcast) really power the machine. They are
// reproduced faithfully from the decompile and are never driven during
// validation. The countdown, the notification dialog, and the system shutdown
// call live in the bundled PSSDNSVC.EXE helper service, out of scope the same way
// PsSuspend leaves PSSPNDSVC. See analysis/PsShutdown.md for the safety warning.

#include "CommandLine.h"
#include "ShutdownActions.h"
#include "Usage.h"

namespace
{
    // Version-resource fields of psshutdown64.exe v2.6, used for the banner and
    // the EULA registry key. The original reads these from its own version
    // resource at run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsShutdown",
        "2.6",
        "Shutdown, logoff and power manage local and remote systems",
        "Independent source-accurate recreation of PsShutdown by Sysinternals, through reverse engineering and "
        "rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name is the version-resource InternalName. The registry key is
    // case-insensitive, so this matches HKCU\Software\Sysinternals\PsShutdown.
    constexpr char kApp[] = "PsShutdown";
} // namespace

int main(int argc, char** argv)
{
    using namespace psshutdown;

    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    // The original gates on Windows XP or higher through VerifyVersionInfoW and
    // prints "PsShutdown requires Windows XP or higher." when it fails. That check
    // is always true on supported systems, so it is dropped here, the same call
    // PsSuspend makes.

    Options o;
    if (!Parse(argc, argv, o))
    {
        PrintUsage();
        return -1;
    }

    // -u without -p prompts for a hidden password, as the original does before it
    // reaches the credentialed remote connect.
    if (o.haveUser && !o.havePassword)
    {
        o.password = pstools::PromptHiddenPassword();
        o.havePassword = true;
    }

    const std::string localName = pstools::LocalComputerName();
    if (!o.haveComputer)
    {
        o.computer = localName;
    }

    const bool ok = pstools::ForEachComputer(o.computer, true,
                                             [&](const char* computer)
                                             {
                                                 return Worker(o, localName, computer) == 0;
                                             });
    return ok ? 0 : 1;
}
