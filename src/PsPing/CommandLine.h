// PsPing argument parser and mode resolution (sub_140009B60): the switch grammar,
// the histogram spec, and the destination/request-size rules that pick the mode.
#pragma once

#include "Usage.h"

#include <cctype>
#include <cstdlib>

namespace psping
{
    // Parse an unsigned decimal with an optional trailing unit letter already
    // uppercased by the caller. Returns false on a non-numeric token.
    [[nodiscard]] inline bool ParseUnsigned(const char* s, unsigned& value, char& unit)
    {
        char* end = nullptr;
        const unsigned long v = std::strtoul(s, &end, 10);
        if (end == s)
        {
            return false;
        }
        value = static_cast<unsigned>(v);
        unit = (*end != '\0') ? *end : '\0';
        return true;
    }

    [[nodiscard]] inline bool WantsArg(int argc, int i)
    {
        return i + 1 < argc;
    }

    // Parse the histogram spec that may follow -h. A leading digit marks a bucket
    // count or a comma list, anything else leaves the default bucket count.
    inline void ParseHistogramArg(const char* arg, Options& options, bool& consumed)
    {
        consumed = false;
        if (arg == nullptr || !(arg[0] == '.' || (arg[0] >= '0' && arg[0] <= '9')))
        {
            return;
        }
        std::vector<float> values;
        const char* p = arg;
        while (*p != '\0')
        {
            char* end = nullptr;
            const float f = std::strtof(p, &end);
            if (end == p)
            {
                break;
            }
            values.push_back(f);
            p = end;
            if (*p == ',')
            {
                ++p;
            }
        }
        if (values.empty())
        {
            return;
        }
        consumed = true;
        if (values.size() == 1)
        {
            options.buckets = static_cast<int>(values[0]);
        }
        else
        {
            options.customBounds = values;
        }
    }

    // Returns true when the command line is well formed and the caller should run.
    // On a usage error it prints the matching usage and returns false. Mirrors the
    // switch grammar and the mode resolution of sub_140009B60.
    [[nodiscard]] inline bool ParseArguments(int argc, char** argv, Options& options, int& exitCode)
    {
        exitCode = -1;
        bool bandwidth = false;
        bool receive = false;
        int family94 = 0;

        for (int i = 1; i < argc; ++i)
        {
            const char* a = argv[i];
            if (a[0] != '-' && a[0] != '/')
            {
                std::string host;
                std::string port;
                SplitHostPort(a, host, port);
                options.destHost = host;
                options.destPort = port;
                options.haveDest = true;
                continue;
            }

            for (int c = 1; a[c] != '\0'; ++c)
            {
                const char sw = static_cast<char>(std::toupper(static_cast<unsigned char>(a[c])));
                bool consumedNext = false;
                switch (sw)
                {
                case '4':
                    if (family94 != 0)
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    family94 = 2;
                    options.family = AF_INET;
                    break;
                case '6':
                    if (family94 != 0)
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    family94 = 3;
                    options.family = AF_INET6;
                    break;
                case '?':
                {
                    int mode = -1;
                    if (WantsArg(argc, i))
                    {
                        const char n = static_cast<char>(std::toupper(static_cast<unsigned char>(argv[i + 1][0])));
                        if (n == 'B')
                        {
                            mode = static_cast<int>(Mode::BandwidthSend);
                        }
                        else if (n == 'I')
                        {
                            mode = static_cast<int>(Mode::Icmp);
                        }
                        else if (n == 'L')
                        {
                            mode = static_cast<int>(Mode::LatencySend);
                        }
                        else if (n == 'T')
                        {
                            mode = static_cast<int>(Mode::TcpPing);
                        }
                    }
                    PrintUsage(mode);
                    return false;
                }
                case 'B':
                    bandwidth = true;
                    break;
                case 'F':
                    options.firewall = true;
                    break;
                case 'H':
                    options.histogram = true;
                    if (WantsArg(argc, i))
                    {
                        ParseHistogramArg(argv[i + 1], options, consumedNext);
                    }
                    break;
                case 'I':
                    if (!WantsArg(argc, i) || options.intervalMs != -1)
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    options.intervalMs = std::atoi(argv[i + 1]);
                    consumedNext = true;
                    break;
                case 'L':
                {
                    if (!WantsArg(argc, i) || options.reqSize != 0)
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    unsigned v = 0;
                    char unit = '\0';
                    if (!ParseUnsigned(argv[i + 1], v, unit))
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(unit)));
                    if (u == 'K')
                    {
                        v *= 1024;
                    }
                    else if (u == 'M')
                    {
                        v *= 1024 * 1024;
                    }
                    options.reqSize = v;
                    consumedNext = true;
                    break;
                }
                case 'N':
                {
                    if (!WantsArg(argc, i) || options.count != kDefaultCount)
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    unsigned v = 0;
                    char unit = '\0';
                    if (!ParseUnsigned(argv[i + 1], v, unit))
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(unit)));
                    options.count = static_cast<int>(v);
                    if (u == 'S' || u == 'M' || u == 'H')
                    {
                        options.seconds = true;
                        if (u == 'M')
                        {
                            options.count *= 60;
                        }
                        else if (u == 'H')
                        {
                            options.count *= 3600;
                        }
                    }
                    consumedNext = true;
                    break;
                }
                case 'Q':
                    options.quiet = true;
                    break;
                case 'R':
                    receive = true;
                    break;
                case 'S':
                {
                    if (!WantsArg(argc, i))
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    options.serverSpec = argv[i + 1];
                    consumedNext = true;
                    break;
                }
                case 'T':
                    options.count = -1;
                    break;
                case 'U':
                    options.udp = true;
                    break;
                case 'W':
                    if (!WantsArg(argc, i) || options.warmup != -1)
                    {
                        PrintUsage(-1);
                        return false;
                    }
                    options.warmup = std::atoi(argv[i + 1]);
                    consumedNext = true;
                    break;
                default:
                    PrintUsage(-1);
                    return false;
                }
                if (consumedNext)
                {
                    ++i;
                    break;
                }
            }
        }

        options.bandwidth = bandwidth;
        options.receive = receive;

        // Mode resolution (sub_140009B60 tail).
        const bool haveServer = !options.serverSpec.empty();
        const bool havePort = options.haveDest && !options.destPort.empty();

        if (!havePort && !haveServer)
        {
            if (!options.haveDest)
            {
                PrintUsage(-1);
                return false;
            }
            options.mode = Mode::Icmp;
            if (options.reqSize == 0)
            {
                options.reqSize = kDefaultIcmpSize;
            }
            else if (options.reqSize > kMaxIcmpSize)
            {
                std::printf("\n\nRequest size must be < 64000\n\n");
                return false;
            }
            if (options.warmup == -1)
            {
                options.warmup = 1;
            }
        }
        else if (haveServer && !options.haveDest)
        {
            options.mode = Mode::Server;
        }
        else if (options.reqSize == 0)
        {
            if (bandwidth)
            {
                std::printf("Bandwidth test requires a request size.\n\n");
                return false;
            }
            options.mode = Mode::TcpPing;
            if (options.warmup == -1)
            {
                options.warmup = 1;
            }
        }
        else
        {
            if (bandwidth)
            {
                options.mode = receive ? Mode::BandwidthRecv : Mode::BandwidthSend;
            }
            else
            {
                options.mode = receive ? Mode::LatencyRecv : Mode::LatencySend;
            }
            if (options.warmup == -1)
            {
                options.warmup = 5;
            }
        }

        // Interval resolution for the ping modes (sub_140009B60 tail): an unset
        // interval becomes one second, -i 0 becomes the fast one-millisecond
        // path, otherwise the value is seconds converted to milliseconds.
        if (options.intervalMs == -1)
        {
            options.intervalMs = kDefaultIntervalMs;
        }
        else if (options.intervalMs == 0)
        {
            options.intervalMs = kFastIntervalMs;
        }
        else
        {
            options.intervalMs *= 1000;
        }
        return true;
    }
} // namespace psping
