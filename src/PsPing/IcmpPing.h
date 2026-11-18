// PsPing ICMP ping loop (sub_140004E60): IcmpSendEcho / Icmp6SendEcho2 with QPC
// timing and a warmup count, feeding the stats accumulator and the histogram.
#pragma once

#include "Statistics.h"

namespace psping
{
    inline int RunIcmpPing(const Options& options)
    {
        Endpoint dest = {};
        const int resolveErr = ResolveEndpoint(options.destHost, {}, options.family, 0, dest);
        if (resolveErr != 0)
        {
            ReportResolveFailure(resolveErr);
            return -1;
        }
        const bool v6 = dest.family == AF_INET6;

        HANDLE icmp = v6 ? ::Icmp6CreateFile() : ::IcmpCreateFile();
        if (icmp == INVALID_HANDLE_VALUE)
        {
            std::fprintf(stderr, "Error opening ICMP handle:\n");
            pstools::PrintSystemError(::GetLastError());
            return 1;
        }

        LARGE_INTEGER freq = {};
        ::QueryPerformanceFrequency(&freq);

        const std::string destText = FormatEndpoint(dest, false);
        std::printf("Pinging %s with %d bytes of data:\n", destText.c_str(), options.reqSize);
        const std::string header = BuildHeader(options);
        if (!options.quiet)
        {
            std::printf("%s\n", header.c_str());
        }
        else
        {
            std::printf("%s  0%%", header.c_str());
            std::fflush(stdout);
        }

        std::vector<unsigned char> request(options.reqSize, 0);
        const DWORD replySize = static_cast<DWORD>((v6 ? 36 : 40) + 64 + options.reqSize);
        std::vector<unsigned char> reply(replySize);

        Stats stats;
        Histogram hist;

        PingTimer timer(options.intervalMs);
        HANDLE waits[3] = {g_stopEvent, g_breakEvent, timer.handle};
        const ULONGLONG startTick = ::GetTickCount64();
        int iter = 0;
        bool stopped = false;

        for (;;)
        {
            const DWORD w = ::WaitForMultipleObjects(3, waits, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0)
            {
                stopped = true;
                break;
            }
            if (w == WAIT_OBJECT_0 + 1)
            {
                stats.Print();
                continue;
            }

            LARGE_INTEGER t0 = {};
            ::QueryPerformanceCounter(&t0);
            DWORD replies = 0;
            if (v6)
            {
                sockaddr_in6 src = {};
                src.sin6_family = AF_INET6;
                replies = ::Icmp6SendEcho2(
                    icmp, nullptr, nullptr, nullptr, &src, reinterpret_cast<sockaddr_in6*>(&dest.addr), request.data(),
                    static_cast<WORD>(options.reqSize), nullptr, reply.data(), replySize, kIcmpTimeoutMs);
            }
            else
            {
                const IPAddr ip = reinterpret_cast<sockaddr_in*>(&dest.addr)->sin_addr.S_un.S_addr;
                replies = ::IcmpSendEcho(icmp, ip, request.data(), static_cast<WORD>(options.reqSize), nullptr,
                                         reply.data(), replySize, kIcmpTimeoutMs);
            }
            LARGE_INTEGER t1 = {};
            ::QueryPerformanceCounter(&t1);
            const float ms = ElapsedMs(t0.QuadPart, t1.QuadPart, freq.QuadPart);

            if (!options.quiet)
            {
                if (replies == 0)
                {
                    const DWORD err = ::GetLastError();
                    std::printf("Error pinging %s:\n", destText.c_str());
                    if (err == kIpReqTimedOut)
                    {
                        std::printf("Request timed out.\n");
                    }
                    else
                    {
                        pstools::PrintSystemError(err);
                    }
                }
                else
                {
                    std::printf("Reply from %s: %.2fms\n", destText.c_str(), static_cast<double>(ms));
                }
            }

            ++iter;
            if (iter > options.warmup)
            {
                stats.Add(replies != 0, ms);
                if (replies != 0)
                {
                    hist.Store(ms);
                }
            }
            if (LoopDone(options, iter, startTick))
            {
                break;
            }
        }

        if (options.quiet)
        {
            std::printf("\b\b\b\b100%%\n");
        }
        std::printf("\nPing statistics for %s:\n", destText.c_str());
        stats.Print();
        if (options.histogram)
        {
            hist.Print(options);
        }
        ::IcmpCloseHandle(icmp);
        static_cast<void>(stopped);
        return 0;
    }
} // namespace psping
