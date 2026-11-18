// PsService: a service viewer and controller over the Service Control Manager.
// Source-accurate C++20 console reconstruction of PsService64.exe v2.26. The RE
// map (sub_ addresses per routine) and validation evidence are in
// analysis/PsService.md. The shared banner, EULA gate, and system-error printer
// come from ../common/PsTools.h, reused read-only.

#include "CommandLine.h"
#include "ServiceCommands.h"

#include <clocale>

namespace
{
    constexpr pstools::Banner kBanner{
        "PsService",
        "2.26",
        "Service information and configuration utility",
        "Independent source-accurate recreation of PsService by Sysinternals, through reverse engineering and "
        "rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    constexpr char kApp[] = "PsService";
} // namespace

int wmain(int argc, wchar_t** argv)
{
    using namespace psservice;

    std::setlocale(LC_ALL, "");

    const bool noBanner = StripFlagW(argc, argv, L"-nobanner", L"/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = StripFlagW(argc, argv, L"-accepteula", L"/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    const HMODULE advapi = ::GetModuleHandleW(L"advapi32.dll");
    g_queryServiceConfig2W = reinterpret_cast<QueryServiceConfig2WFn>(
        reinterpret_cast<void*>(::GetProcAddress(advapi, "QueryServiceConfig2W")));

    CommandLine cmd;
    if (!ParseCommandLine(argc, argv, cmd))
    {
        PrintUsage(argv[0], 0);
        return -1;
    }

    if (cmd.haveUser && !cmd.havePassword)
    {
        cmd.password = PromptHiddenPasswordW();
        cmd.havePassword = true;
    }

    const std::wstring local = LocalComputerNameW();
    bool remote = false;
    if (cmd.haveMachine && cmd.machine.size() > 2)
    {
        remote = ::_wcsicmp(cmd.machine.c_str() + 2, local.c_str()) != 0;
    }

    if (!remote)
    {
        ScHandle scm(::OpenSCManagerW(nullptr, L"ServicesActive", kScmAccess));
        if (!scm)
        {
            std::wprintf(L"Unable to access Service Control Manager:\n");
            pstools::PrintSystemError(::GetLastError());
            return -1;
        }
        g_machine = L"\\\\" + local;
        Dispatch(scm.Get(), cmd.code, cmd.args, argv[0]);
        return 0;
    }

    const std::wstring bareName = cmd.machine.substr(2);
    if (cmd.haveUser)
    {
        if (!RemoteConnectW(bareName, cmd.user, cmd.password))
        {
            std::wprintf(L"Unable to connect to %s:\n", cmd.machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return -1;
        }
    }

    g_machine = cmd.machine;
    ScHandle scm(::OpenSCManagerW(cmd.machine.c_str(), L"ServicesActive", kScmAccess));
    if (scm)
    {
        Dispatch(scm.Get(), cmd.code, cmd.args, argv[0]);
        if (cmd.haveUser)
        {
            RemoteDisconnectW(bareName);
        }
        return 0;
    }

    std::wprintf(L"Unable to access Service Control Manager on %s:\n", g_machine.c_str());
    pstools::PrintSystemError(::GetLastError());
    if (cmd.haveUser)
    {
        RemoteDisconnectW(bareName);
    }
    return -1;
}
