// PsPing shared core: the winsock and ICMP includes, the recovered constants and
// mode enum, the options record, the endpoint helpers, the stop and break events,
// and the waitable-timer pacing shared by both ping loops. The sub_ map lives in
// analysis/PsPing.md.
#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include "../common/PsTools.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace psping
{
    // Defaults recovered from the .data initializers and the parser.
    constexpr int kDefaultCount = 4;
    constexpr int kDefaultIntervalMs = 1000;
    constexpr int kFastIntervalMs = 1;
    constexpr unsigned kDefaultIcmpSize = 32;
    constexpr unsigned kMaxIcmpSize = 64000;
    constexpr DWORD kIcmpTimeoutMs = 5000;
    constexpr DWORD kConnectTimeoutMs = 5000;
    constexpr int kDefaultBuckets = 20;
    constexpr DWORD kIpReqTimedOut = 11010; // IP_REQ_TIMED_OUT

    enum class Mode : int
    {
        Icmp = 0,
        TcpPing = 1,
        LatencySend = 2,
        LatencyRecv = 3,
        BandwidthSend = 4,
        BandwidthRecv = 5,
        Server = 6,
    };

    struct Options
    {
        Mode mode = Mode::Icmp;
        int count = kDefaultCount;       // -n, -1 for -t (infinite)
        bool seconds = false;            // -n Ns duration form
        int warmup = -1;                 // -w, resolved after mode is known
        int intervalMs = -1;             // -i, resolved after mode is known
        unsigned reqSize = 0;            // -l, 0 means unset
        int family = AF_UNSPEC;          // -4 / -6
        bool quiet = false;              // -q
        bool receive = false;            // -r
        bool udp = false;                // -u
        bool bandwidth = false;          // -b
        bool firewall = false;           // -f
        bool histogram = false;          // -h given
        int buckets = kDefaultBuckets;   // -h count
        std::vector<float> customBounds; // -h v1,v2,...
        std::string destHost;
        std::string destPort;   // empty for ICMP
        std::string serverSpec; // -s address:port
        bool haveDest = false;
    };

    struct Endpoint
    {
        sockaddr_storage addr = {};
        int len = 0;
        int family = AF_UNSPEC;
        unsigned short port = 0;
    };

    [[nodiscard]] inline std::string FormatAddress(const sockaddr* sa, int salen)
    {
        char host[NI_MAXHOST] = {};
        if (::getnameinfo(sa, salen, host, static_cast<DWORD>(std::size(host)), nullptr, 0, NI_NUMERICHOST) != 0)
        {
            return {};
        }
        return host;
    }

    [[nodiscard]] inline std::string FormatEndpoint(const Endpoint& ep, bool withPort)
    {
        const std::string host = FormatAddress(reinterpret_cast<const sockaddr*>(&ep.addr), ep.len);
        if (!withPort || ep.port == 0)
        {
            return host;
        }
        if (ep.family == AF_INET6)
        {
            return "[" + host + "]:" + std::to_string(ep.port);
        }
        return host + ":" + std::to_string(ep.port);
    }

    // Split a "host", "host:port", or "[v6]:port" token. The bracket form carries
    // an IPv6 literal. Without brackets a single colon separates host and port,
    // and two or more colons mark a bare IPv6 literal with no port.
    inline void SplitHostPort(const std::string& token, std::string& host, std::string& port)
    {
        port.clear();
        if (!token.empty() && token.front() == '[')
        {
            const size_t close = token.find(']');
            if (close != std::string::npos)
            {
                host = token.substr(1, close - 1);
                if (close + 1 < token.size() && token[close + 1] == ':')
                {
                    port = token.substr(close + 2);
                }
                return;
            }
        }
        const size_t colons = std::count(token.begin(), token.end(), ':');
        if (colons == 1)
        {
            const size_t pos = token.find(':');
            host = token.substr(0, pos);
            port = token.substr(pos + 1);
            return;
        }
        host = token;
    }

    // Returns 0 on success, otherwise the getaddrinfo error code.
    [[nodiscard]] inline int ResolveEndpoint(const std::string& host, const std::string& port, int family, int sockType,
                                             Endpoint& out)
    {
        addrinfo hints = {};
        hints.ai_family = family;
        hints.ai_socktype = sockType;
        addrinfo* result = nullptr;
        const int rc = ::getaddrinfo(host.c_str(), port.empty() ? nullptr : port.c_str(), &hints, &result);
        if (rc != 0 || result == nullptr)
        {
            return rc != 0 ? rc : WSAHOST_NOT_FOUND;
        }
        std::memcpy(&out.addr, result->ai_addr, result->ai_addrlen);
        out.len = static_cast<int>(result->ai_addrlen);
        out.family = result->ai_family;
        if (out.family == AF_INET)
        {
            out.port = ::ntohs(reinterpret_cast<sockaddr_in*>(&out.addr)->sin_port);
        }
        else if (out.family == AF_INET6)
        {
            out.port = ::ntohs(reinterpret_cast<sockaddr_in6*>(&out.addr)->sin6_port);
        }
        ::freeaddrinfo(result);
        return 0;
    }

    // Report a destination resolve failure the way psping does (sub_140008930): the
    // prefix and the error message go to stderr, and a host-not-found result prints
    // the tool's own wording rather than the system message.
    inline void ReportResolveFailure(int code)
    {
        std::fprintf(stderr, "Invalid destination address:\n");
        if (code == WSAHOST_NOT_FOUND)
        {
            std::fprintf(stderr, "Host not found.\n");
        }
        else
        {
            pstools::PrintSystemError(static_cast<DWORD>(code));
        }
    }

    // Control events mirror the stop and break events in sub_14000AB70.
    inline HANDLE g_stopEvent = nullptr;  // Ctrl+C: end the ping loop
    inline HANDLE g_breakEvent = nullptr; // Ctrl+Break: print interim statistics

    inline BOOL WINAPI ConsoleHandler(DWORD type)
    {
        if (type == CTRL_BREAK_EVENT)
        {
            if (g_breakEvent != nullptr)
            {
                ::SetEvent(g_breakEvent);
            }
            return TRUE;
        }
        if (g_stopEvent != nullptr)
        {
            ::SetEvent(g_stopEvent);
        }
        return TRUE;
    }

    [[nodiscard]] inline float ElapsedMs(LONGLONG start, LONGLONG stop, LONGLONG freq)
    {
        return (static_cast<float>(stop - start) / static_cast<float>(freq)) * 1000.0f;
    }

    // Build the "N iterations (warmup M) ping test: " header. Mirrors the header
    // selection shared by both ping loops.
    [[nodiscard]] inline std::string BuildHeader(const Options& options)
    {
        char buf[256] = {};
        if (options.seconds)
        {
            std::snprintf(buf, std::size(buf), "%d seconds (%d warmup pings) connecting test: ", options.count,
                          options.warmup);
        }
        else if (options.count < 0)
        {
            std::snprintf(buf, std::size(buf), "Infinite iterations (warmup %d) ping test: ", options.warmup);
        }
        else
        {
            std::snprintf(buf, std::size(buf), "%d iterations (warmup %d) ping test: ", options.count + options.warmup,
                          options.warmup);
        }
        return buf;
    }

    // A waitable timer paced at the configured interval. The first tick is
    // immediate, matching SetWaitableTimer with a zero due time.
    struct PingTimer
    {
        HANDLE handle = nullptr;

        explicit PingTimer(int intervalMs)
        {
            handle = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
            if (handle != nullptr)
            {
                LARGE_INTEGER due = {};
                ::SetWaitableTimer(handle, &due, intervalMs, nullptr, nullptr, FALSE);
            }
        }

        ~PingTimer()
        {
            if (handle != nullptr)
            {
                ::CancelWaitableTimer(handle);
                ::CloseHandle(handle);
            }
        }

        PingTimer(const PingTimer&) = delete;
        PingTimer& operator=(const PingTimer&) = delete;
    };

    // Decide whether the loop has reached its iteration target. Mirrors the count
    // context (sub_140004730): finite count stops after count+warmup iterations,
    // -t runs until the stop event, and the seconds form runs for its duration.
    [[nodiscard]] inline bool LoopDone(const Options& options, int iter, ULONGLONG startTick)
    {
        if (options.count < 0)
        {
            return false;
        }
        if (options.seconds)
        {
            return ::GetTickCount64() - startTick >= static_cast<ULONGLONG>(options.count) * 1000;
        }
        return iter >= options.count + options.warmup;
    }

    struct WsaScope
    {
        bool ok = false;
        WsaScope()
        {
            WSADATA data = {};
            ok = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }
        ~WsaScope()
        {
            if (ok)
            {
                ::WSACleanup();
            }
        }
        WsaScope(const WsaScope&) = delete;
        WsaScope& operator=(const WsaScope&) = delete;
    };
} // namespace psping
