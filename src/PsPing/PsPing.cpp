// PsPing: ICMP ping, TCP connect latency, and latency/bandwidth measurement.
// Source-accurate C++20 console reconstruction of the Sysinternals psping64.exe
// v2.12, built around the two ping engines, the ICMP ping path and the TCP
// connect path, with their per-ping lines, the summary statistics, and the
// latency histogram. The shared banner, -nobanner strip, and EULA gate come from
// ../common/PsTools.h, reused read-only. The sub_ map and the out-of-scope UDP,
// latency, and bandwidth engines are documented in analysis/PsPing.md.

#include "CommandLine.h"
#include "IcmpPing.h"
#include "TcpPing.h"

namespace
{
    // Version-resource fields of psping64.exe v2.12, used for the banner and the
    // EULA registry key. The original reads these from its own version resource
    // at run time. Fixing them here does not change the output.
    constexpr pstools::Banner kBanner{
        "PsPing",
        "2.12",
        "PsPing - ping, latency, bandwidth measurement utility",
        "Independent source-accurate recreation of PsPing by Sysinternals, through reverse engineering and rewritten "
        "in modern C++20.",
        "Author: 0xntpower",
    };

    constexpr char kApp[] = "PsPing";
} // namespace

int main(int argc, char** argv)
{
    using namespace psping;

    const bool noBanner = pstools::StripFlag(argc, argv, "-nobanner", "/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = pstools::StripFlag(argc, argv, "-accepteula", "/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    WsaScope wsa;
    if (!wsa.ok)
    {
        std::fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }

    Options options;
    int exitCode = -1;
    if (!ParseArguments(argc, argv, options, exitCode))
    {
        return exitCode;
    }

    g_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_breakEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ::SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    int rc = 0;
    switch (options.mode)
    {
    case Mode::Icmp:
        rc = RunIcmpPing(options);
        break;
    case Mode::TcpPing:
        rc = RunTcpPing(options);
        break;
    default:
        std::fprintf(stderr, "psping: latency, bandwidth, and server modes are not reproduced in this build.\n"
                             "The ICMP ping and TCP ping paths are.\n");
        rc = 1;
        break;
    }

    if (g_stopEvent != nullptr)
    {
        ::CloseHandle(g_stopEvent);
    }
    if (g_breakEvent != nullptr)
    {
        ::CloseHandle(g_breakEvent);
    }
    return rc;
}
