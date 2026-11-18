// PsPing TCP connect loop (sub_1400059B0): per-tick socket, non-blocking connect
// with a deadline, and getsockname for the local endpoint on the reply line.
#pragma once

#include "Statistics.h"

namespace psping
{
    // Non-blocking connect with a deadline, mirroring the async connect in
    // sub_140009450. Returns 0 on success, otherwise a WSA error code.
    [[nodiscard]] inline int ConnectWithTimeout(SOCKET sock, const sockaddr* sa, int salen, DWORD timeoutMs)
    {
        u_long nonblock = 1;
        ::ioctlsocket(sock, FIONBIO, &nonblock);
        if (::connect(sock, sa, salen) == 0)
        {
            return 0;
        }
        if (::WSAGetLastError() != WSAEWOULDBLOCK)
        {
            return ::WSAGetLastError();
        }
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sock, &writeSet);
        fd_set errSet;
        FD_ZERO(&errSet);
        FD_SET(sock, &errSet);
        timeval tv = {};
        tv.tv_sec = static_cast<long>(timeoutMs / 1000);
        tv.tv_usec = static_cast<long>((timeoutMs % 1000) * 1000);
        const int sel = ::select(0, nullptr, &writeSet, &errSet, &tv);
        if (sel <= 0)
        {
            return WSAETIMEDOUT;
        }
        if (FD_ISSET(sock, &errSet))
        {
            int soErr = 0;
            int len = sizeof(soErr);
            ::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len);
            return soErr != 0 ? soErr : WSAECONNREFUSED;
        }
        return 0;
    }

    inline int RunTcpPing(const Options& options)
    {
        Endpoint dest = {};
        const int resolveErr = ResolveEndpoint(options.destHost, options.destPort, options.family, SOCK_STREAM, dest);
        if (resolveErr != 0)
        {
            ReportResolveFailure(resolveErr);
            return -1;
        }

        LARGE_INTEGER freq = {};
        ::QueryPerformanceFrequency(&freq);

        const std::string destText = FormatEndpoint(dest, true);
        std::printf("TCP connect to %s:\n", destText.c_str());
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

        Stats stats;
        Histogram hist;

        PingTimer timer(options.intervalMs);
        HANDLE waits[3] = {g_stopEvent, g_breakEvent, timer.handle};
        const ULONGLONG startTick = ::GetTickCount64();
        int iter = 0;

        for (;;)
        {
            const DWORD w = ::WaitForMultipleObjects(3, waits, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0)
            {
                break;
            }
            if (w == WAIT_OBJECT_0 + 1)
            {
                stats.Print();
                continue;
            }

            const SOCKET sock = ::socket(dest.family, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET)
            {
                if (!options.quiet)
                {
                    pstools::PrintSystemError(::WSAGetLastError());
                }
                ++iter;
                if (LoopDone(options, iter, startTick))
                {
                    break;
                }
                continue;
            }

            const bool warm = iter < options.warmup;
            if (!options.quiet)
            {
                std::printf(warm ? "Connecting to %s (warmup): " : "Connecting to %s: ", destText.c_str());
                std::fflush(stdout);
            }

            LARGE_INTEGER t0 = {};
            ::QueryPerformanceCounter(&t0);
            const int err =
                ConnectWithTimeout(sock, reinterpret_cast<sockaddr*>(&dest.addr), dest.len, kConnectTimeoutMs);
            LARGE_INTEGER t1 = {};
            ::QueryPerformanceCounter(&t1);
            const float ms = ElapsedMs(t0.QuadPart, t1.QuadPart, freq.QuadPart);

            Endpoint local = {};
            local.len = sizeof(local.addr);
            if (::getsockname(sock, reinterpret_cast<sockaddr*>(&local.addr), &local.len) == 0)
            {
                local.family = local.addr.ss_family;
                if (local.family == AF_INET)
                {
                    local.port = ::ntohs(reinterpret_cast<sockaddr_in*>(&local.addr)->sin_port);
                }
                else if (local.family == AF_INET6)
                {
                    local.port = ::ntohs(reinterpret_cast<sockaddr_in6*>(&local.addr)->sin6_port);
                }
            }
            const std::string localText = FormatEndpoint(local, true);

            if (err == 0)
            {
                if (!options.quiet)
                {
                    std::printf("from %s: %.2fms\n", localText.c_str(), static_cast<double>(ms));
                }
            }
            else if (!options.quiet)
            {
                std::printf("from %s:\n", localText.c_str());
                pstools::PrintSystemError(static_cast<DWORD>(err));
            }
            ::closesocket(sock);

            ++iter;
            if (iter > options.warmup)
            {
                stats.Add(err == 0, ms);
                if (err == 0)
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
        std::printf("\nTCP connect statistics for %s:\n", destText.c_str());
        stats.Print();
        if (options.histogram)
        {
            hist.Print(options);
        }
        return 0;
    }
} // namespace psping
