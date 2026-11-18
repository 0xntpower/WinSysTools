// PsPing usage help, verbatim from psping64.exe, with the mode selector that
// mirrors sub_1400096E0.
#pragma once

#include "PingCommon.h"

namespace psping
{
    inline void PrintGeneralUsage()
    {
        std::printf("PsPing implements Ping functionality, TCP ping, UDP/TCP latency, and UDP / TCP\n"
                    "bandwidth measurement.\n\n"
                    "Help usage: psping -? [i|t|l|b]\n"
                    "   -? i   Usage for ICMP ping.\n"
                    "   -? t   Usage for TCP ping.\n"
                    "   -? l   Usage for latency test.\n"
                    "   -? b   Usage for bandwidth test.\n"
                    "  -nobanner   Do not display the startup banner and copyright message.\n");
    }

    inline void PrintIcmpUsage()
    {
        std::printf("ICMP ping usage: psping [-t|-n count[s]] [-i interval] [-w count] [-q] [-h "
                    "[buckets|val1,val2,...]] [-l requestsize[k]] [-6|-4] destination\n"
                    "  -t    Ping until stopped with Ctrl+C and type Ctrl+Break\n"
                    "        for statistics.\n"
                    "  -n    Number of pings or append 's' to specify seconds e.g. '10s'.\n"
                    "  -i    Interval in seconds. Specify 0 for fast ping.\n"
                    "  -w    Warmup with the specified number of iterations (default is 1).\n"
                    "  -q    Don't output during pings.\n"
                    "  -h    Print histogram (default bucket count is 20).\n"
                    "        If you specify a single argument, it's interpreted as a bucket\n"
                    "        count and the histogram will contain that number of\n"
                    "        buckets covering the entire time range of values.\n"
                    "        Specify a comma-separated list of times to create a custom\n"
                    "        histogram (e.g. \"0.01,0.05,1,5,10\").\n"
                    "  -l    Request size. Append 'k' for kilobytes.\n"
                    "  -4    Force using IPv4.\n"
                    "  -6    Force using IPv6.\n\n"
                    "For high-speed ping tests use -q and -i 0.\n\n");
    }

    inline void PrintTcpUsage()
    {
        std::printf("TCP ping usage: psping [-t|-n count[s]] [-i interval] [-w count] [-q] [-h "
                    "[buckets|val1,val2,...]] [-6|-4] destination:port\n"
                    "  -t    Ping until stopped with Ctrl+C and type Ctrl+Break\n"
                    "        for statistics.\n"
                    "  -n    Number of pings or append 's' to specify seconds e.g. '10s'.\n"
                    "  -i    Interval in seconds. Specify 0 for fast ping.\n"
                    "  -w    Warmup with the specified number of iterations (default is 1).\n"
                    "  -q    Don't output during pings.\n"
                    "  -h    Print histogram (default bucket count is 20).\n"
                    "        If you specify a single argument, it's interpreted as a bucket\n"
                    "        count and the histogram will contain that number of\n"
                    "        buckets covering the entire time range of values.\n"
                    "        Specify a comma-separated list of times to create a custom\n"
                    "        histogram (e.g. \"0.01,0.05,1,5,10\").\n"
                    "  -4    Force using IPv4.\n"
                    "  -6    Force using IPv6.\n\n"
                    "For high-speed ping tests use -q and -i 0.\n\n");
    }

    inline void PrintLatencyUsage()
    {
        std::printf("TCP and UDP latency usage:\n"
                    "server: psping [-6|-4] [-f] -s address:port\n"
                    "client: psping -l requestsize[k|m] -n count[s] [-r] [-u] [-w count] [-f] [-h "
                    "[buckets|val1,val2,...]] [-6|-4] destination:port\n"
                    "  -l    Request size. Append 'k' for kilobytes and 'm' for megabytes.\n"
                    "  -n    Number of sends/receives. Append 's' to specify seconds e.g. '10s'.\n"
                    "  -r    Receive from the server instead of sending.\n"
                    "  -u    UDP (default is TCP).\n"
                    "  -w    Warmup with the specified number of iterations (default is 5).\n"
                    "  -f    Open source firewall port during the run.\n"
                    "  -h    Print histogram (default bucket count is 20).\n"
                    "        If you specify a single argument, it's interpreted as a bucket\n"
                    "        count and the histogram will contain that number of\n"
                    "        buckets covering the entire time range of values.\n"
                    "        Specify a comma-separated list of times to create a custom\n"
                    "        histogram (e.g. \"0.01,0.05,1,5,10\").\n"
                    "  -4    Force using IPv4.\n"
                    "  -6    Force using IPv6.\n"
                    "  -s    Server listening address and port.\n\n"
                    "The server can serve both latency and bandwidth tests and remains active until\n"
                    "you terminate it with Control-C.\n\n");
    }

    inline void PrintBandwidthUsage()
    {
        std::printf("TCP and UDP bandwidth usage:\n"
                    "server: psping [-6|-4] [-f] -s address:port\n"
                    "client: psping -b -l requestsize[k|m] -n count[s] [-r] [-u [target]] [-i outstanding] [-w count] "
                    "[-f] [-h [buckets|val1,val2,...]] [-6|-4] destination:port\n"
                    "  -b    Bandwidth test.\n"
                    "  -l    Request size. Append 'k' for kilobytes and 'm' for megabytes.\n"
                    "  -n    Number of sends/receives. Append 's' to specify seconds e.g. '10s'.\n"
                    "  -r    Receive from the server instead of sending.\n"
                    "  -u    UDP (default is TCP). Specify target bandwidth in MB/s.\n"
                    "  -i    Number of outstanding I/Os (default is min of 16 and 2x CPU cores).\n"
                    "  -w    Warmup for the specified iterations (default is 2x CPU cores).\n"
                    "  -f    Open source firewall port during the run.\n"
                    "  -h    Print histogram (default bucket count is 20).\n"
                    "        If you specify a single argument, it's interpreted as a bucket\n"
                    "        count and the histogram will contain that number of\n"
                    "        buckets covering the entire time range of values.\n"
                    "        Specify a comma-separated list of times to create a custom\n"
                    "        histogram (e.g. \"0.01,0.05,1,5,10\").\n"
                    "  -4    Force using IPv4.\n"
                    "  -6    Force using IPv6.\n"
                    "  -s    Server listening address and port.\n\n"
                    "The server can serve both latency and bandwidth tests and remains active until\n"
                    "you terminate it with Control-C.\n\n");
    }

    // Mirrors sub_1400096E0: a mode of -1 prints the general help, otherwise the
    // mode-specific usage.
    inline void PrintUsage(int mode)
    {
        switch (mode)
        {
        case static_cast<int>(Mode::Icmp):
            PrintIcmpUsage();
            break;
        case static_cast<int>(Mode::TcpPing):
            PrintTcpUsage();
            break;
        case static_cast<int>(Mode::LatencySend):
            PrintLatencyUsage();
            break;
        case static_cast<int>(Mode::BandwidthSend):
            PrintBandwidthUsage();
            break;
        default:
            PrintGeneralUsage();
            break;
        }
    }
} // namespace psping
