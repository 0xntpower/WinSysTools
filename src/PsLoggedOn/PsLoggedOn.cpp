// PsLoggedOn -- shows the users logged on to a computer, both those logged on
// locally and those connected over resource shares. Source-accurate C++20
// console reconstruction of the Sysinternals PsLoggedon64.exe v1.35.
//
// Behaviour recovered with IDA (idalib) from PsLoggedon64.exe:
//   wmain                 0x140003220
//   local-logon enum      0x1400026D0  (HKEY_USERS subkey walk, LookupAccountSid)
//   resource-share enum   0x140002CA0  (NetSessionEnum level 10)
//   logon-time printer     0x140002C00  (GetDateFormat / GetTimeFormat)
//   Volatile Environment   0x140003090  (per-SID last-write time)
//   nano-server check      0x140002500
//   banner plus EULA       0x1400025A0 / 0x140001AA0
//   usage                  0x1400031D0
//
// Two enumeration paths, mirroring the original:
//   Local logons come from the loaded profile hives under HKEY_USERS. Every
//   subkey whose name is a SID (other than .default, the *Classes hives, and
//   the three service SIDs S-1-5-18/19/20) is resolved with LookupAccountSid.
//   The per-user logon time is the last-write time of that SID's
//   "Volatile Environment" subkey.
//   Resource-share logons come from NetSessionEnum at level 10. The logon time
//   is the current time minus the session's elapsed seconds.
//
// The shared PsTools front end (banner, EULA gate) lives in ../common/PsTools.h
// and is reused read-only. PsLoggedon has its own positional argument model
// ([-l] [-x] [\\computer] or [username]), which is implemented here rather than
// through pstools::ParseCommandLine.
//
// The reconstruction uses the ANSI registry and account APIs so the console
// bytes match the original's wide output for ASCII names, the same approach the
// PsGetSid rebuild validated. The SID subkey is turned into a binary SID with
// ConvertStringSidToSidA instead of the original's hand-rolled scanf plus
// AllocateAndInitializeSid. Well-formed SIDs resolve identically.

#include "../common/PsTools.h"

#include <windows.h>
#include <lm.h>
#include <sddl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "netapi32.lib")

namespace
{
    // Version-resource fields of PsLoggedon64.exe v1.35, used for the banner and
    // the EULA registry key HKCU\Software\Sysinternals\PsLoggedon. The original
    // reads these from its own version resource at run time. Fixed here, which
    // does not change the printed output.
    constexpr pstools::Banner kBanner{
        "PsLoggedon",
        "1.35",
        "See who's logged on",
        "Independent source-accurate recreation of PsLoggedon by Sysinternals, through reverse engineering and rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    constexpr char kApp[] = "PsLoggedon";

    // A carriage return, 78 spaces, and a carriage return. The original writes
    // this to erase the "Connecting..." or "Checking..." progress line before a
    // result. Matches the literal at 0x14001BAF0.
    constexpr char kClearLine[] =
        "\r                                                                              \r";
    static_assert(sizeof(kClearLine) == 1 + 78 + 1 + 1, "clear line must be 78 spaces");

    void PrintUsage(const char* exePath)
    {
        std::printf("Usage: %s [-l] [-x] [\\\\computername]\n    or %s [username]\n", exePath, exePath);
        std::printf("-l     Show only local logons\n");
        std::printf("-x     Don't show logon times\n");
        std::printf("-nobanner Do not display the startup banner and copyright message.\n");
        std::printf("\n");
    }

    [[nodiscard]] std::string Narrow(const wchar_t* w)
    {
        if (w == nullptr)
        {
            return {};
        }
        const int n = ::WideCharToMultiByte(CP_ACP, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1)
        {
            return {};
        }
        std::string s(static_cast<size_t>(n - 1), '\0');
        ::WideCharToMultiByte(CP_ACP, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    [[nodiscard]] std::wstring Widen(const char* s)
    {
        if (s == nullptr || s[0] == '\0')
        {
            return {};
        }
        const int n = ::MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        if (n <= 1)
        {
            return {};
        }
        std::wstring w(static_cast<size_t>(n - 1), L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
        return w;
    }

    // FormatMessage string for a Windows error code, matching the original's
    // FormatMessageW call (system source, language 0x400). The message already
    // carries its own trailing newline.
    [[nodiscard]] std::string ErrorText(DWORD code)
    {
        LPSTR buffer = nullptr;
        const DWORD n = ::FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, nullptr, code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
        std::string out = (n != 0 && buffer != nullptr) ? std::string(buffer, n) : std::string();
        if (buffer != nullptr)
        {
            ::LocalFree(buffer);
        }
        return out;
    }

    // HKLM ServerLevels\NanoServer == 1. Mirrors 0x140002500.
    [[nodiscard]] bool IsNanoServer()
    {
        HKEY key = nullptr;
        if (::RegOpenKeyW(HKEY_LOCAL_MACHINE,
                          L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Server\\ServerLevels",
                          &key) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD type = 0;
        DWORD data = 0;
        DWORD cb = sizeof(data);
        bool nano = false;
        if (::RegQueryValueExW(key, L"NanoServer", nullptr, &type, reinterpret_cast<LPBYTE>(&data), &cb)
                == ERROR_SUCCESS
            && type == REG_DWORD)
        {
            nano = data == 1;
        }
        ::RegCloseKey(key);
        return nano;
    }

    // Last-write time of HKU\<sid>\Volatile Environment, converted to local
    // SYSTEMTIME. Mirrors 0x140003090. A profile with no such subkey (a service
    // account, for example) returns false and the caller prints "<unknown time>".
    [[nodiscard]] bool VolatileEnvTime(HKEY users, const char* sid, SYSTEMTIME& out)
    {
        std::string sub = std::string(sid) + "\\Volatile Environment";
        HKEY key = nullptr;
        if (::RegOpenKeyA(users, sub.c_str(), &key) != ERROR_SUCCESS)
        {
            return false;
        }
        FILETIME ft = {};
        const LSTATUS st = ::RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                              nullptr, nullptr, nullptr, nullptr, &ft);
        ::RegCloseKey(key);
        if (st != ERROR_SUCCESS)
        {
            return false;
        }
        ::FileTimeToLocalFileTime(&ft, &ft);
        ::FileTimeToSystemTime(&ft, &out);
        return true;
    }

    // "     <date> <time>    " with five leading and four trailing spaces, no
    // newline. Mirrors 0x140002C00 and the resource-share time print.
    void PrintLogonTime(const SYSTEMTIME& time)
    {
        char dateStr[264] = {};
        char timeStr[264] = {};
        ::GetDateFormatA(LOCALE_USER_DEFAULT, 0, &time, nullptr, dateStr, 260);
        ::GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &time, nullptr, timeStr, 260);
        std::printf("     %s %s    ", dateStr, timeStr);
    }

    // Enumerate the loaded profile hives under HKEY_USERS and print each account.
    // Mirrors 0x1400026D0. computer == nullptr means the local machine.
    // searchUser == nullptr lists everyone, otherwise it reports only a match and
    // in a "logged onto X locally" form. Returns true when at least one line was
    // printed.
    [[nodiscard]] bool EnumLocalLogons(bool showTimes, const char* computer, const char* searchUser)
    {
        HKEY users = nullptr;
        if (computer != nullptr)
        {
            std::printf("Connecting to Registry of %s...", computer);
            std::fflush(stdout);
            if (::RegConnectRegistryA(computer, HKEY_USERS, &users) != ERROR_SUCCESS)
            {
                std::printf("%s", kClearLine);
                std::printf("Error opening HKEY_USERS for %s\n", computer);
                return false;
            }
            std::printf("%s", kClearLine);
        }
        else if (::RegOpenKeyA(HKEY_USERS, nullptr, &users) != ERROR_SUCCESS)
        {
            const DWORD err = ::GetLastError();
            std::printf("Error opening HKEY_USERS: %s\n", ErrorText(err).c_str());
            return false;
        }

        bool printedAny = false;
        char name[256] = {};
        for (DWORD i = 0;; ++i)
        {
            DWORD cch = static_cast<DWORD>(std::size(name));
            FILETIME ft = {};
            if (::RegEnumKeyExA(users, i, name, &cch, nullptr, nullptr, nullptr, &ft) != ERROR_SUCCESS)
            {
                break;
            }
            if (::_stricmp(name, ".default") == 0 || std::strstr(name, "Classes") != nullptr)
            {
                continue;
            }
            if (std::strcmp(name, "S-1-5-18") == 0 || std::strcmp(name, "S-1-5-19") == 0
                || std::strcmp(name, "S-1-5-20") == 0)
            {
                continue;
            }

            PSID sid = nullptr;
            if (!::ConvertStringSidToSidA(name, &sid) || sid == nullptr)
            {
                continue;
            }

            char account[1024] = {};
            char domain[1024] = {};
            DWORD cchAccount = static_cast<DWORD>(std::size(account));
            DWORD cchDomain = static_cast<DWORD>(std::size(domain));
            SID_NAME_USE use = SidTypeUnknown;
            if (::LookupAccountSidA(computer, sid, account, &cchAccount, domain, &cchDomain, &use))
            {
                bool doPrint = false;
                if (searchUser == nullptr)
                {
                    if (!printedAny)
                    {
                        std::printf("Users logged on locally:\n");
                    }
                    doPrint = true;
                }
                else if (::_stricmp(searchUser, account) == 0)
                {
                    doPrint = true;
                }

                if (doPrint)
                {
                    printedAny = true;
                    if (showTimes)
                    {
                        SYSTEMTIME time = {};
                        if (VolatileEnvTime(users, name, time))
                        {
                            PrintLogonTime(time);
                        }
                        else if (IsNanoServer())
                        {
                            std::printf("     <Logon time not support on Nano Server>\t");
                        }
                        else
                        {
                            std::printf("     <unknown time>\t");
                        }
                    }
                    if (searchUser != nullptr)
                    {
                        std::printf("%s", kClearLine);
                        std::printf("%s\\%s logged onto %s locally.\n", domain, searchUser, computer);
                    }
                    else
                    {
                        const char* prefix = showTimes ? "" : "     ";
                        std::printf("%s\t%s\\%s\n", prefix, domain, account);
                    }
                }
            }
            ::LocalFree(sid);
        }
        ::RegCloseKey(users);

        if (!printedAny && searchUser == nullptr)
        {
            std::printf("No one is logged on locally.\n");
        }
        return printedAny;
    }

    // Enumerate inbound SMB sessions with NetSessionEnum (level 10) and print the
    // connected accounts. Mirrors 0x140002CA0. The logon time is the current
    // local time minus the session's elapsed seconds.
    [[nodiscard]] bool EnumResourceLogons(bool showTimes, const char* computer, const char* searchUser)
    {
        const std::wstring serverW = Widen(computer);
        const LPWSTR server = computer != nullptr ? const_cast<LPWSTR>(serverW.c_str()) : nullptr;
        const std::wstring searchW = Widen(searchUser);

        bool printedAny = false;
        DWORD resume = 0;
        DWORD status = 0;
        do
        {
            SESSION_INFO_10* buffer = nullptr;
            DWORD entriesRead = 0;
            DWORD total = 0;
            status = ::NetSessionEnum(server, nullptr, nullptr, 10,
                                      reinterpret_cast<LPBYTE*>(&buffer), MAX_PREFERRED_LENGTH,
                                      &entriesRead, &total, &resume);
            if (status != NERR_Success && status != ERROR_MORE_DATA)
            {
                std::printf("Unable to query resource logons\n");
                printedAny = true;
                if (buffer != nullptr)
                {
                    ::NetApiBufferFree(buffer);
                }
                break;
            }
            if (buffer == nullptr)
            {
                continue;
            }

            for (DWORD i = 0; i < entriesRead; ++i)
            {
                const wchar_t* userW = buffer[i].sesi10_username;
                if (userW == nullptr || userW[0] == L'\0')
                {
                    continue;
                }
                if (printedAny == false && searchUser == nullptr)
                {
                    std::printf("\nUsers logged on via resource shares:\n");
                    printedAny = true;
                }

                const std::string user = Narrow(userW);
                const bool matches = searchUser == nullptr
                                     || ::_wcsicmp(searchW.c_str(), userW) == 0;

                BYTE sid[256] = {};
                DWORD cbSid = sizeof(sid);
                wchar_t domainW[1024] = {};
                DWORD cchDomain = static_cast<DWORD>(std::size(domainW));
                SID_NAME_USE use = SidTypeUnknown;
                const bool resolved = ::LookupAccountNameW(buffer[i].sesi10_cname, userW, sid, &cbSid,
                                                           domainW, &cchDomain, &use) != FALSE;

                if (!matches)
                {
                    continue;
                }
                printedAny = true;

                if (showTimes)
                {
                    SYSTEMTIME now = {};
                    FILETIME ft = {};
                    ::GetLocalTime(&now);
                    ::SystemTimeToFileTime(&now, &ft);
                    // The original truncates 10000000*seconds to 32 bits before
                    // subtracting, so a long session can wrap. Kept for fidelity.
                    const uint32_t delta = 10000000u * static_cast<uint32_t>(buffer[i].sesi10_time);
                    ULARGE_INTEGER t = {};
                    t.LowPart = ft.dwLowDateTime;
                    t.HighPart = ft.dwHighDateTime;
                    t.QuadPart -= delta;
                    ft.dwLowDateTime = t.LowPart;
                    ft.dwHighDateTime = t.HighPart;
                    SYSTEMTIME logon = {};
                    ::FileTimeToSystemTime(&ft, &logon);
                    PrintLogonTime(logon);
                }

                if (searchUser != nullptr)
                {
                    if (resolved)
                    {
                        const std::string domain = Narrow(domainW);
                        std::printf("%s", kClearLine);
                        std::printf("%s\\%s logged onto %s remotely.\n", domain.c_str(), searchUser,
                                    computer != nullptr ? computer : "");
                    }
                    else
                    {
                        const char* host = computer != nullptr ? computer : "";
                        std::printf("\r%s\\%s logged onto %s remotely.\n", host, searchUser, host);
                    }
                }
                else if (resolved)
                {
                    const std::string domain = Narrow(domainW);
                    const char* prefix = showTimes ? "" : "     ";
                    std::printf("%s\t%s\\%s\n", prefix, domain.c_str(), user.c_str());
                }
                else
                {
                    const char* prefix = showTimes ? "" : "      ";
                    const char* host = computer != nullptr ? computer : "";
                    std::printf("%s\t%s\\%s\n", prefix, host, user.c_str());
                }
            }
            ::NetApiBufferFree(buffer);
        } while (status == ERROR_MORE_DATA);

        if (!printedAny && searchUser == nullptr)
        {
            std::printf("\nNo one is logged on via resource shares.\n");
        }
        return printedAny;
    }

    // Search every server in the current domain for a named user. Mirrors the
    // NetServerEnum loop in wmain.
    void RunDomainSearch(bool showTimes, bool localOnly, const char* username)
    {
        int found = 0;
        DWORD resume = 0;
        DWORD status = 0;
        do
        {
            SERVER_INFO_100* buffer = nullptr;
            DWORD entriesRead = 0;
            DWORD total = 0;
            status = ::NetServerEnum(nullptr, 100, reinterpret_cast<LPBYTE*>(&buffer),
                                     MAX_PREFERRED_LENGTH, &entriesRead, &total, SV_TYPE_NT, nullptr,
                                     &resume);
            if (status == NERR_Success || status == ERROR_MORE_DATA)
            {
                for (DWORD i = 0; i < entriesRead; ++i)
                {
                    const std::string server = Narrow(buffer[i].sv100_name);
                    std::printf("Checking %s...", server.c_str());
                    std::fflush(stdout);
                    found += EnumLocalLogons(showTimes, server.c_str(), username) ? 1 : 0;
                    if (!localOnly)
                    {
                        found += EnumResourceLogons(showTimes, server.c_str(), username) ? 1 : 0;
                    }
                }
            }
            else
            {
                std::printf("%s: %s\n", "Error browing network", ErrorText(status).c_str());
            }
            if (buffer != nullptr)
            {
                ::NetApiBufferFree(buffer);
            }
        } while (status == ERROR_MORE_DATA);

        if (found == 0 && status == NERR_Success)
        {
            std::printf("\r%s is not logged on to any computer.\n\n", username);
        }
    }
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

    if (argc > 4)
    {
        PrintUsage(argv[0]);
        return -1;
    }

    bool showTimes = true;
    bool localOnly = false;
    const char* computer = nullptr;
    const char* username = nullptr;

    for (int i = 1; i < argc; ++i)
    {
        const char* a = argv[i];
        if (a[0] == '-' || a[0] == '/')
        {
            const char c = a[1];
            if (c == '?')
            {
                PrintUsage(argv[0]);
                return -1;
            }
            if (c == 'l' || c == 'L')
            {
                localOnly = true;
            }
            else if (c == 'x' || c == 'X')
            {
                showTimes = false;
            }
        }
        else if (a[0] == '\\')
        {
            if (computer != nullptr)
            {
                PrintUsage(argv[0]);
                return -1;
            }
            computer = a;
        }
        else
        {
            if (username != nullptr)
            {
                PrintUsage(argv[0]);
                return -1;
            }
            username = a;
        }
    }

    if (username != nullptr)
    {
        RunDomainSearch(showTimes, localOnly, username);
        return 0;
    }

    static_cast<void>(EnumLocalLogons(showTimes, computer, nullptr));
    if (!localOnly)
    {
        static_cast<void>(EnumResourceLogons(showTimes, computer, nullptr));
    }
    return 0;
}
