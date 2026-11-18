// Whois - console recreation of the Sysinternals Whois utility (v1.21).
//
// A WHOIS client over TCP port 43. Given a domain name it queries an initial
// server (an explicit server argument, or <TLD>.whois-servers.net when none is
// given), then follows the referral chain printed by each server to the
// authoritative registry and registrar, printing the raw responses. Reverse
// engineered from targets/sysinternals/whois64.exe: main at 0x1400033a0, the
// connect and query routine at 0x1400031e0, usage at 0x1400031a0, the WSA error
// printer at 0x140003120, and the banner and EULA helpers at 0x140001050 and
// 0x140002220.
//
// Behaviour reproduced:
//  - port 43 protocol: gethostbyname, connect, send "<query>\r\n", recv to end.
//  - initial server from the last label of the registrable domain.
//  - referral following via the marker set the original scans for, in order.
//  - -v verbose prints every server queried and its full response.
//  - server override as a second positional argument.
//  - -accepteula and -nobanner, banner on stderr, everything else on stdout.
//
// The fixed 112-byte name buffers of the original are replaced with std::string
// so long inputs keep full fidelity instead of truncating. The referral marker
// extraction keeps the original's fixed plus-fourteen offset, which is what
// lands on the value in the "domain:" and "Whois Server: " layouts.

#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    constexpr const char* kName = "Whois";
    constexpr const char* kVersion = "1.21";
    constexpr const char* kDescription = "Domain information lookup";
    constexpr const char* kCopyright = "Independent source-accurate recreation of Whois by Sysinternals, through reverse engineering and rewritten in modern C++20.";
    constexpr const char* kCompany = "Author: 0xntpower";

    constexpr unsigned short kWhoisPort = 43;
    constexpr int kRecvChunk = 0x1000;

    // The original adds this fixed offset to the found marker to reach the value.
    // It matches the "Whois Server: " and the "domain:       " field widths.
    constexpr int kMarkerValueOffset = 14;

    // Last labels for which the input is reduced to its registrable two labels.
    // Anything else is queried whole. Table at 0x140078190 in the original.
    constexpr const char* kKnownTlds[] = { "COM", "ORG", "NET", "BIZ", "GOV", "EDU", "INFO" };

    class WinsockScope
    {
    public:
        WinsockScope() noexcept
        {
            WSADATA data{};
            started_ = WSAStartup(MAKEWORD(1, 1), &data) == 0;
        }
        ~WinsockScope()
        {
            if (started_)
            {
                WSACleanup();
            }
        }
        WinsockScope(const WinsockScope&) = delete;
        WinsockScope& operator=(const WinsockScope&) = delete;
        [[nodiscard]] bool Started() const noexcept { return started_; }

    private:
        bool started_ = false;
    };

    class ScopedSocket
    {
    public:
        explicit ScopedSocket(SOCKET sock) noexcept : socket_(sock) {}
        ~ScopedSocket()
        {
            if (socket_ != INVALID_SOCKET)
            {
                closesocket(socket_);
            }
        }
        ScopedSocket(const ScopedSocket&) = delete;
        ScopedSocket& operator=(const ScopedSocket&) = delete;
        [[nodiscard]] SOCKET Get() const noexcept { return socket_; }
        [[nodiscard]] bool Valid() const noexcept { return socket_ != INVALID_SOCKET; }

    private:
        SOCKET socket_ = INVALID_SOCKET;
    };

    [[nodiscard]] bool ArgEquals(const char* arg, const char* dash, const char* slash) noexcept
    {
        return _stricmp(arg, dash) == 0 || _stricmp(arg, slash) == 0;
    }

    // Remove the first argument matching either spelling of a switch, closing the
    // gap in argv and shrinking argc. The original strips -nobanner and
    // -accepteula this way before the main parse.
    [[nodiscard]] bool StripSwitch(int& argc, char** argv, const char* dash, const char* slash)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (ArgEquals(argv[i], dash, slash))
            {
                for (int j = i; j < argc - 1; ++j)
                {
                    argv[j] = argv[j + 1];
                }
                --argc;
                return true;
            }
        }
        return false;
    }

    void PrintBanner()
    {
        std::fprintf(stderr, "\n%s v%s - %s\n%s\n%s\n\n",
            kName, kVersion, kDescription, kCopyright, kCompany);
        std::fflush(stderr);
    }

    int PrintUsage()
    {
        std::printf("\nUsage: whois [-v] domainname [whois.server]\n");
        std::printf(" -v   Print whois information for referrals\n");
        std::printf(" -nobanner\n");
        std::printf("      Do not display the startup banner and copyright message.\n\n");
        return 1;
    }

    // Confirm EULA acceptance. -accepteula or a prior run both allow the tool to
    // run and record HKCU\Software\Sysinternals\Whois\EulaAccepted. A first run
    // with neither prints the accept notice and stops.
    [[nodiscard]] bool EnsureEula(bool acceptedOnCommandLine)
    {
        bool accepted = acceptedOnCommandLine;

        if (!accepted)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Sysinternals\\Whois",
                0, KEY_READ, &key) == ERROR_SUCCESS)
            {
                DWORD value = 0;
                DWORD size = sizeof(value);
                DWORD type = 0;
                if (RegQueryValueExA(key, "EulaAccepted", nullptr, &type,
                    reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS
                    && type == REG_DWORD && value != 0)
                {
                    accepted = true;
                }
                RegCloseKey(key);
            }
        }

        if (!accepted)
        {
            std::fprintf(stderr, "%s License Agreement\n", kName);
            std::fprintf(stderr,
                "This is the first run of this program. You must accept EULA to continue.\n");
            std::fprintf(stderr, "Use -accepteula to accept EULA.\n\n");
            return false;
        }

        HKEY key = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Sysinternals\\Whois",
            0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            RegSetValueExA(key, "EulaAccepted", 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&one), sizeof(one));
            RegCloseKey(key);
        }
        return true;
    }

    // Print the last Winsock error on stderr, the way the original's error
    // routine at 0x140003120 does with FormatMessage over WSAGetLastError.
    void PrintWsaError()
    {
        const DWORD error = static_cast<DWORD>(WSAGetLastError());
        char* message = nullptr;
        const DWORD count = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&message), 0, nullptr);
        if (count != 0 && message != nullptr)
        {
            std::fputs(message, stderr);
        }
        LocalFree(message);
    }

    // Open a TCP connection to server:43, send the query, and read the full
    // response. Returns false when nothing could be read, after reporting the
    // Winsock error, exactly like the original's routine at 0x1400031e0.
    [[nodiscard]] bool QueryServer(const char* server, const char* query, std::string& response)
    {
        std::printf("Connecting to %s...\n", server);
        std::fflush(stdout);

        const hostent* host = gethostbyname(server);
        if (host == nullptr || host->h_addr_list[0] == nullptr)
        {
            PrintWsaError();
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kWhoisPort);
        std::memcpy(&address.sin_addr, host->h_addr_list[0], 4);

        ScopedSocket sock(socket(AF_INET, SOCK_STREAM, 0));
        if (!sock.Valid())
        {
            PrintWsaError();
            return false;
        }

        if (connect(sock.Get(), reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) != 0)
        {
            PrintWsaError();
            return false;
        }

        if (send(sock.Get(), query, static_cast<int>(std::strlen(query)), 0) == SOCKET_ERROR)
        {
            PrintWsaError();
            return false;
        }

        response.clear();
        char buffer[kRecvChunk] = {};
        int received = 0;
        while ((received = recv(sock.Get(), buffer, kRecvChunk, 0)) > 0)
        {
            response.append(buffer, static_cast<size_t>(received));
        }
        return !response.empty();
    }

    [[nodiscard]] bool IsKnownTld(const char* tld) noexcept
    {
        for (const char* known : kKnownTlds)
        {
            if (_stricmp(tld, known) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // Reduce a known-gTLD domain to its registrable two labels, so a host or
    // subdomain is stripped before the query. ccTLD and unknown inputs are kept
    // whole, which is what the original's loop at 0x140003598 settles on.
    [[nodiscard]] std::string RegistrableDomain(const std::string& upper, bool knownGtld)
    {
        if (!knownGtld)
        {
            return upper;
        }
        const std::string::size_type lastDot = upper.find_last_of('.');
        if (lastDot == std::string::npos)
        {
            return upper;
        }
        const std::string::size_type prevDot = upper.find_last_of('.', lastDot - 1);
        if (prevDot == std::string::npos)
        {
            return upper;
        }
        return upper.substr(prevDot + 1);
    }

    // Find the referral marker inside a response. Mirrors the original's order:
    // the "Whois Server: " that follows a "Domain Name: <domain>" line first,
    // then WHOIS Server:, domain:, Domain ID:, Registrant:, and finally the whole
    // response. viaDomainName is set when the first path matched, because the
    // original only prints the non-verbose body for the later markers.
    [[nodiscard]] const char* FindReferralMarker(const std::string& response,
        const std::string& domain, bool& viaDomainName)
    {
        const char* resp = response.c_str();
        viaDomainName = false;

        const std::string domainNameLine = "Domain Name: " + domain + "\n";
        const char* dn = std::strstr(resp, domainNameLine.c_str());
        if (dn != nullptr)
        {
            const char* marker = std::strstr(dn, "Whois Server: ");
            if (marker != nullptr)
            {
                viaDomainName = true;
                return marker;
            }
        }

        const char* marker = std::strstr(resp, "WHOIS Server:");
        if (marker != nullptr)
        {
            return marker;
        }
        marker = std::strstr(resp, "domain:");
        if (marker != nullptr)
        {
            return marker;
        }
        marker = std::strstr(resp, "Domain ID:");
        if (marker != nullptr)
        {
            return marker;
        }
        marker = std::strstr(resp, "Registrant:\n");
        if (marker != nullptr)
        {
            return marker + 12;
        }
        return resp;
    }
}

int main(int argc, char** argv)
{
    const bool noBanner = StripSwitch(argc, argv, "-nobanner", "/nobanner");
    const bool acceptEula = StripSwitch(argc, argv, "-accepteula", "/accepteula");

    if (!noBanner)
    {
        PrintBanner();
    }

    if (!EnsureEula(acceptEula))
    {
        return 1;
    }

    WinsockScope winsock;
    if (!winsock.Started())
    {
        PrintWsaError();
        return 1;
    }

    const char* domainArg = nullptr;
    const char* serverArg = nullptr;
    bool verbose = false;

    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        const unsigned char first = static_cast<unsigned char>(arg[0]);
        if (first == '-' || first == '/')
        {
            if (arg[1] != 'v')
            {
                return PrintUsage();
            }
            verbose = true;
        }
        else if (domainArg == nullptr)
        {
            if (std::strchr(arg, '.') == nullptr)
            {
                return PrintUsage();
            }
            domainArg = arg;
        }
        else if (serverArg == nullptr)
        {
            serverArg = arg;
        }
        else
        {
            return PrintUsage();
        }
    }

    if (domainArg == nullptr)
    {
        return PrintUsage();
    }

    // When the argument parses as a dotted-quad address, query the reverse-DNS
    // name instead, as the original does before the TLD handling.
    std::string working;
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
    if (std::sscanf(domainArg, "%d.%d.%d.%d", &a, &b, &c, &d) == 4)
    {
        const unsigned long addr = inet_addr(domainArg);
        const hostent* host = gethostbyaddr(reinterpret_cast<const char*>(&addr), 4, AF_INET);
        if (host == nullptr)
        {
            PrintWsaError();
            return 0;
        }
        working = host->h_name;
    }
    else
    {
        working = domainArg;
    }

    std::transform(working.begin(), working.end(), working.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

    std::string::size_type lastDot = working.find_last_of('.');
    const bool knownGtld = lastDot != std::string::npos && IsKnownTld(working.c_str() + lastDot + 1);

    std::string domain = RegistrableDomain(working, knownGtld);

    lastDot = domain.find_last_of('.');
    if (lastDot == std::string::npos)
    {
        std::printf("%s is not a valid domain name.\n", domain.c_str());
        return 1;
    }
    const std::string tld = domain.substr(lastDot + 1);

    std::string server = serverArg != nullptr
        ? std::string(serverArg)
        : tld + ".whois-servers.net";

    // Japanese registry whois returns English for a "domain/e" query.
    std::string query = (!knownGtld && tld == "JP")
        ? domain + "/e\r\n"
        : domain + "\r\n";

    std::string response;
    if (!QueryServer(server.c_str(), query.c_str(), response))
    {
        return 0;
    }

    for (;;)
    {
        if (verbose)
        {
            std::printf("Server %s returned the following for %s\n", server.c_str(), query.c_str());
            std::printf("%s\n\n", response.c_str());
        }

        if (std::strstr(response.c_str(), "To single out one record") != nullptr)
        {
            query = "=" + domain + "\r\n";
        }
        else if (std::strstr(response.c_str(), "No match") != nullptr)
        {
            std::printf("No whois information found.\n\n");
            return 0;
        }
        else
        {
            bool viaDomainName = false;
            const char* marker = FindReferralMarker(response, domain, viaDomainName);

            if (!viaDomainName && !verbose)
            {
                std::printf("\n%s\n", marker);
            }

            const std::string::size_type markerOffset =
                static_cast<std::string::size_type>(marker - response.c_str());
            std::string::size_type valueStart = markerOffset + kMarkerValueOffset;
            valueStart = std::min(valueStart, response.size());

            std::string::size_type valueEnd = response.find('\n', valueStart);
            std::string nextServer = response.substr(valueStart,
                valueEnd == std::string::npos ? std::string::npos : valueEnd - valueStart);

            // The original compares over the shorter of the two names, then
            // stops when a plain-domain query would revisit the same server.
            const std::string::size_type compareLength =
                std::min(server.size(), nextServer.size());
            const bool sameServer =
                std::strncmp(server.c_str(), nextServer.c_str(), compareLength) == 0;
            const bool plainQuery =
                std::strncmp(query.c_str(), domain.c_str(), domain.size()) == 0;

            if (sameServer && plainQuery)
            {
                if (!verbose)
                {
                    std::printf("\n%s\n", response.c_str());
                }
                return 0;
            }

            if (const std::string::size_type cr = nextServer.find('\r');
                cr != std::string::npos)
            {
                nextServer.erase(cr);
            }
            server = nextServer;
            query = domain + "\r\n";
        }

        if (!QueryServer(server.c_str(), query.c_str(), response))
        {
            return 0;
        }
    }
}
