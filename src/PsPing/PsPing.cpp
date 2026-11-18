// PsPing -- ICMP ping, TCP connect latency, and latency/bandwidth measurement.
// Source-accurate C++20 console reconstruction of the Sysinternals psping64.exe
// v2.12. This build reproduces the two ping engines the tool is built around,
// the ICMP ping path and the TCP connect path, with their per-ping lines, the
// summary statistics, and the latency histogram.
//
// Behaviour recovered from psping64.exe (idalib primary, rea cross-check):
//   main / dispatch        sub_14000AB70  (WSAStartup, mode switch, Ctrl handler)
//   argument parser        sub_140009B60  (switch grammar, mode resolution)
//   ICMP ping loop         sub_140004E60  (IcmpSendEcho, QPC timing, warmup)
//   TCP connect loop       sub_1400059B0  (socket/bind/connect, getsockname)
//   stats accumulator      sub_140004420 / sub_1400044E0 / sub_1400043D0
//   histogram build        sub_140004B30  (width = (max-min)/(buckets-1))
//   histogram / stats print sub_140004C90 / sub_140004DA0
//
// The shared Sysinternals front end (banner, -nobanner strip, EULA gate) is
// reused read only from ../common/PsTools.h. PsPing carries its own argument
// grammar, so the parser here is local and mirrors sub_140009B60.
//
// Out of scope and documented in analysis/PsPing.md: the UDP and TCP latency and
// bandwidth engines (modes 2..5), the -s server, and the quiet-mode scale
// decoration. The ICMP and TCP ping paths, their statistics, and the histogram
// are reproduced faithfully.

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
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace
{
    // Version-resource fields of psping64.exe v2.12, used for the banner and the
    // EULA registry key. The original reads these from its own version resource
    // at run time. Fixing them here does not change the output.
    constexpr pstools::Banner kBanner{
        "PsPing",
        "2.12",
        "PsPing - ping, latency, bandwidth measurement utility",
        "Independent source-accurate recreation of PsPing by Sysinternals, through reverse engineering and rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    constexpr char kApp[] = "PsPing";

    // Defaults recovered from the .data initializers and the parser.
    constexpr int kDefaultCount = 4;
    constexpr int kDefaultIntervalMs = 1000;
    constexpr int kFastIntervalMs = 1;
    constexpr unsigned kDefaultIcmpSize = 32;
    constexpr unsigned kMaxIcmpSize = 64000;
    constexpr DWORD kIcmpTimeoutMs = 5000;
    constexpr DWORD kConnectTimeoutMs = 5000;
    constexpr int kDefaultBuckets = 20;
    constexpr DWORD kIpReqTimedOut = 11010;  // IP_REQ_TIMED_OUT

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
        int count = kDefaultCount;         // -n, -1 for -t (infinite)
        bool seconds = false;              // -n Ns duration form
        int warmup = -1;                   // -w, resolved after mode is known
        int intervalMs = -1;               // -i, resolved after mode is known
        unsigned reqSize = 0;              // -l, 0 means unset
        int family = AF_UNSPEC;            // -4 / -6
        bool quiet = false;                // -q
        bool receive = false;              // -r
        bool udp = false;                  // -u
        bool bandwidth = false;            // -b
        bool firewall = false;             // -f
        bool histogram = false;            // -h given
        int buckets = kDefaultBuckets;     // -h count
        std::vector<float> customBounds;   // -h v1,v2,...
        std::string destHost;
        std::string destPort;              // empty for ICMP
        std::string serverSpec;            // -s address:port
        bool haveDest = false;
    };

    // --- Usage text (verbatim from psping64.exe) ---------------------------

    void PrintGeneralUsage()
    {
        std::printf(
            "PsPing implements Ping functionality, TCP ping, UDP/TCP latency, and UDP / TCP\n"
            "bandwidth measurement.\n\n"
            "Help usage: psping -? [i|t|l|b]\n"
            "   -? i   Usage for ICMP ping.\n"
            "   -? t   Usage for TCP ping.\n"
            "   -? l   Usage for latency test.\n"
            "   -? b   Usage for bandwidth test.\n"
            "  -nobanner   Do not display the startup banner and copyright message.\n");
    }

    void PrintIcmpUsage()
    {
        std::printf(
            "ICMP ping usage: psping [-t|-n count[s]] [-i interval] [-w count] [-q] [-h [buckets|val1,val2,...]] [-l requestsize[k]] [-6|-4] destination\n"
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

    void PrintTcpUsage()
    {
        std::printf(
            "TCP ping usage: psping [-t|-n count[s]] [-i interval] [-w count] [-q] [-h [buckets|val1,val2,...]] [-6|-4] destination:port\n"
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

    void PrintLatencyUsage()
    {
        std::printf(
            "TCP and UDP latency usage:\n"
            "server: psping [-6|-4] [-f] -s address:port\n"
            "client: psping -l requestsize[k|m] -n count[s] [-r] [-u] [-w count] [-f] [-h [buckets|val1,val2,...]] [-6|-4] destination:port\n"
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

    void PrintBandwidthUsage()
    {
        std::printf(
            "TCP and UDP bandwidth usage:\n"
            "server: psping [-6|-4] [-f] -s address:port\n"
            "client: psping -b -l requestsize[k|m] -n count[s] [-r] [-u [target]] [-i outstanding] [-w count] [-f] [-h [buckets|val1,val2,...]] [-6|-4] destination:port\n"
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
    void PrintUsage(int mode)
    {
        switch (mode)
        {
        case static_cast<int>(Mode::Icmp):          PrintIcmpUsage(); break;
        case static_cast<int>(Mode::TcpPing):       PrintTcpUsage(); break;
        case static_cast<int>(Mode::LatencySend):   PrintLatencyUsage(); break;
        case static_cast<int>(Mode::BandwidthSend): PrintBandwidthUsage(); break;
        default:                                    PrintGeneralUsage(); break;
        }
    }

    // --- Endpoint helpers --------------------------------------------------

    struct Endpoint
    {
        sockaddr_storage addr = {};
        int len = 0;
        int family = AF_UNSPEC;
        unsigned short port = 0;
    };

    [[nodiscard]] std::string FormatAddress(const sockaddr* sa, int salen)
    {
        char host[NI_MAXHOST] = {};
        if (::getnameinfo(sa, salen, host, static_cast<DWORD>(std::size(host)),
                          nullptr, 0, NI_NUMERICHOST) != 0)
        {
            return {};
        }
        return host;
    }

    [[nodiscard]] std::string FormatEndpoint(const Endpoint& ep, bool withPort)
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
    void SplitHostPort(const std::string& token, std::string& host, std::string& port)
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
    [[nodiscard]] int ResolveEndpoint(const std::string& host, const std::string& port,
                                      int family, int sockType, Endpoint& out)
    {
        addrinfo hints = {};
        hints.ai_family = family;
        hints.ai_socktype = sockType;
        addrinfo* result = nullptr;
        const int rc = ::getaddrinfo(host.c_str(), port.empty() ? nullptr : port.c_str(),
                                     &hints, &result);
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

    // Report a destination resolve failure the way psping does (sub_140008930):
    // the prefix and the error text go to stderr, and a host-not-found result
    // prints the tool's own wording rather than the system message.
    void ReportResolveFailure(int code)
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

    // --- Statistics (mirrors the five-uint struct at sub_1400044E0) ---------

    class Stats
    {
    public:
        void Add(bool success, float ms)
        {
            ++sent_;
            if (success)
            {
                ++received_;
                sum_ += ms;
                min_ = std::min(min_, ms);
                max_ = std::max(max_, ms);
            }
        }

        void Print() const
        {
            const int lost = sent_ - received_;
            const int lossPct = sent_ > 0 ? lost * 100 / sent_ : 0;
            std::printf("  Sent = %d, Received = %d, Lost = %d (%d%% loss),\n",
                        sent_, received_, lost, lossPct);
            const float avg = received_ != 0 ? sum_ / static_cast<float>(received_) : 0.0f;
            const float shownMin = received_ != 0 ? min_ : 0.0f;
            std::printf("  Minimum = %.2fms, Maximum = %.2fms, Average = %.2fms\n",
                        static_cast<double>(shownMin), static_cast<double>(max_),
                        static_cast<double>(avg));
        }

    private:
        int sent_ = 0;
        int received_ = 0;
        float min_ = (std::numeric_limits<float>::max)();
        float max_ = 0.0f;
        float sum_ = 0.0f;
    };

    // --- Histogram (mirrors sub_140004B30 and the printer sub_140004C90) ----

    class Histogram
    {
    public:
        void Store(float ms)
        {
            samples_.push_back(ms);
        }

        // Print with the supplied column header. Mirrors sub_140004C90: fewer
        // than two samples prints the "not enough data" line, otherwise the
        // header and one row per bucket.
        void Print(const Options& options) const
        {
            if (samples_.size() < 2)
            {
                std::printf("Not enough data for histogram. Trying increasing the request size or\n"
                            "iteration count.\n\n");
                return;
            }
            std::printf("\nLatency\tCount\n");
            if (!options.customBounds.empty())
            {
                PrintCustom(options.customBounds);
            }
            else
            {
                PrintAuto(options.buckets);
            }
        }

    private:
        // sub_140004B30: width = (max - min) / (buckets - 1), bound[i] = min +
        // i*width, and each sample lands in bucket (int)((sample - min) / width).
        void PrintAuto(int buckets) const
        {
            if (buckets < 2)
            {
                buckets = 2;
            }
            float lo = samples_[0];
            float hi = samples_[0];
            for (const float s : samples_)
            {
                lo = std::min(lo, s);
                hi = std::max(hi, s);
            }
            const float width = (hi - lo) / static_cast<float>(buckets - 1);
            std::vector<unsigned> counts(static_cast<size_t>(buckets), 0);
            for (const float s : samples_)
            {
                int idx = width > 0.0f ? static_cast<int>((s - lo) / width) : 0;
                idx = std::clamp(idx, 0, buckets - 1);
                ++counts[static_cast<size_t>(idx)];
            }
            for (int i = 0; i < buckets; ++i)
            {
                const float bound = lo + static_cast<float>(i) * width;
                std::printf("%.2f\t%d\n", static_cast<double>(bound), counts[static_cast<size_t>(i)]);
            }
        }

        // Explicit upper bounds plus an overflow bucket printed as "#INF",
        // modelled from the FLT_MAX branch of sub_140004C90.
        void PrintCustom(const std::vector<float>& boundsIn) const
        {
            std::vector<float> bounds = boundsIn;
            std::sort(bounds.begin(), bounds.end());
            std::vector<unsigned> counts(bounds.size() + 1, 0);
            for (const float s : samples_)
            {
                size_t idx = bounds.size();
                for (size_t i = 0; i < bounds.size(); ++i)
                {
                    if (s <= bounds[i])
                    {
                        idx = i;
                        break;
                    }
                }
                ++counts[idx];
            }
            for (size_t i = 0; i < bounds.size(); ++i)
            {
                std::printf("%.2f\t%d\n", static_cast<double>(bounds[i]), counts[i]);
            }
            std::printf("#INF\t%d\n", counts[bounds.size()]);
        }

        std::vector<float> samples_;
    };

    // --- Control events (mirror the stop/break events in sub_14000AB70) ------

    HANDLE g_stopEvent = nullptr;   // Ctrl+C: end the ping loop
    HANDLE g_breakEvent = nullptr;  // Ctrl+Break: print interim statistics

    BOOL WINAPI ConsoleHandler(DWORD type)
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

    // --- Timing ------------------------------------------------------------

    [[nodiscard]] float ElapsedMs(LONGLONG start, LONGLONG stop, LONGLONG freq)
    {
        return (static_cast<float>(stop - start) / static_cast<float>(freq)) * 1000.0f;
    }

    // Build the "N iterations (warmup M) ping test: " header. Mirrors the header
    // selection shared by both ping loops.
    [[nodiscard]] std::string BuildHeader(const Options& options)
    {
        char buf[256] = {};
        if (options.seconds)
        {
            std::snprintf(buf, std::size(buf), "%d seconds (%d warmup pings) connecting test: ",
                          options.count, options.warmup);
        }
        else if (options.count < 0)
        {
            std::snprintf(buf, std::size(buf), "Infinite iterations (warmup %d) ping test: ",
                          options.warmup);
        }
        else
        {
            std::snprintf(buf, std::size(buf), "%d iterations (warmup %d) ping test: ",
                          options.count + options.warmup, options.warmup);
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
    [[nodiscard]] bool LoopDone(const Options& options, int iter, ULONGLONG startTick)
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

    // --- ICMP ping (sub_140004E60) -----------------------------------------

    int RunIcmpPing(const Options& options)
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
        HANDLE waits[3] = { g_stopEvent, g_breakEvent, timer.handle };
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
                replies = ::Icmp6SendEcho2(icmp, nullptr, nullptr, nullptr, &src,
                                           reinterpret_cast<sockaddr_in6*>(&dest.addr),
                                           request.data(), static_cast<WORD>(options.reqSize),
                                           nullptr, reply.data(), replySize, kIcmpTimeoutMs);
            }
            else
            {
                const IPAddr ip = reinterpret_cast<sockaddr_in*>(&dest.addr)->sin_addr.S_un.S_addr;
                replies = ::IcmpSendEcho(icmp, ip, request.data(), static_cast<WORD>(options.reqSize),
                                         nullptr, reply.data(), replySize, kIcmpTimeoutMs);
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

    // --- TCP ping (sub_1400059B0) ------------------------------------------

    // Non-blocking connect with a deadline, mirroring the async connect in
    // sub_140009450. Returns 0 on success, otherwise a WSA error code.
    [[nodiscard]] int ConnectWithTimeout(SOCKET sock, const sockaddr* sa, int salen, DWORD timeoutMs)
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

    int RunTcpPing(const Options& options)
    {
        Endpoint dest = {};
        const int resolveErr = ResolveEndpoint(options.destHost, options.destPort,
                                               options.family, SOCK_STREAM, dest);
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
        HANDLE waits[3] = { g_stopEvent, g_breakEvent, timer.handle };
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
                std::printf(warm ? "Connecting to %s (warmup): " : "Connecting to %s: ",
                            destText.c_str());
                std::fflush(stdout);
            }

            LARGE_INTEGER t0 = {};
            ::QueryPerformanceCounter(&t0);
            const int err = ConnectWithTimeout(sock, reinterpret_cast<sockaddr*>(&dest.addr),
                                               dest.len, kConnectTimeoutMs);
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

    // --- Argument parsing (sub_140009B60) ----------------------------------

    // Parse an unsigned decimal with an optional trailing unit letter already
    // uppercased by the caller. Returns false on a non-numeric token.
    [[nodiscard]] bool ParseUnsigned(const char* s, unsigned& value, char& unit)
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

    [[nodiscard]] bool WantsArg(int argc, int i)
    {
        return i + 1 < argc;
    }

    // Parse the histogram spec that may follow -h. A leading digit marks a bucket
    // count or a comma list, anything else leaves the default bucket count.
    void ParseHistogramArg(const char* arg, Options& options, bool& consumed)
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
    [[nodiscard]] bool ParseArguments(int argc, char** argv, Options& options, int& exitCode)
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
                    if (family94 != 0) { PrintUsage(-1); return false; }
                    family94 = 2;
                    options.family = AF_INET;
                    break;
                case '6':
                    if (family94 != 0) { PrintUsage(-1); return false; }
                    family94 = 3;
                    options.family = AF_INET6;
                    break;
                case '?':
                {
                    int mode = -1;
                    if (WantsArg(argc, i))
                    {
                        const char n = static_cast<char>(std::toupper(static_cast<unsigned char>(argv[i + 1][0])));
                        if (n == 'B') { mode = static_cast<int>(Mode::BandwidthSend); }
                        else if (n == 'I') { mode = static_cast<int>(Mode::Icmp); }
                        else if (n == 'L') { mode = static_cast<int>(Mode::LatencySend); }
                        else if (n == 'T') { mode = static_cast<int>(Mode::TcpPing); }
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
                    if (!WantsArg(argc, i) || options.intervalMs != -1) { PrintUsage(-1); return false; }
                    options.intervalMs = std::atoi(argv[i + 1]);
                    consumedNext = true;
                    break;
                case 'L':
                {
                    if (!WantsArg(argc, i) || options.reqSize != 0) { PrintUsage(-1); return false; }
                    unsigned v = 0;
                    char unit = '\0';
                    if (!ParseUnsigned(argv[i + 1], v, unit)) { PrintUsage(-1); return false; }
                    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(unit)));
                    if (u == 'K') { v *= 1024; }
                    else if (u == 'M') { v *= 1024 * 1024; }
                    options.reqSize = v;
                    consumedNext = true;
                    break;
                }
                case 'N':
                {
                    if (!WantsArg(argc, i) || options.count != kDefaultCount) { PrintUsage(-1); return false; }
                    unsigned v = 0;
                    char unit = '\0';
                    if (!ParseUnsigned(argv[i + 1], v, unit)) { PrintUsage(-1); return false; }
                    const char u = static_cast<char>(std::toupper(static_cast<unsigned char>(unit)));
                    options.count = static_cast<int>(v);
                    if (u == 'S' || u == 'M' || u == 'H')
                    {
                        options.seconds = true;
                        if (u == 'M') { options.count *= 60; }
                        else if (u == 'H') { options.count *= 3600; }
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
                    if (!WantsArg(argc, i)) { PrintUsage(-1); return false; }
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
                    if (!WantsArg(argc, i) || options.warmup != -1) { PrintUsage(-1); return false; }
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
}

int main(int argc, char** argv)
{
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
        std::fprintf(stderr,
                     "psping: latency, bandwidth, and server modes are not reproduced in this build.\n"
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
