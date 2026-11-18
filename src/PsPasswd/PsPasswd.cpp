// PsPasswd -- changes the password of a local or domain account on the local or
// remote computers. Source-accurate C++20 console reconstruction of the
// Sysinternals pspasswd64.exe v1.25.
//
// Behaviour recovered with IDA (idalib) from pspasswd64.exe:
//   main                 sub_140004440
//   banner + EULA driver sub_140003420  (PrintBanner + EULA gate, run first)
//   OS version gate      sub_140003360  (VerifyVersionInfo, Windows XP or higher)
//   command-line parse   sub_140003EB0
//   usage                sub_140004200
//   per-computer worker  sub_140003D90  (connect, progress, change, report)
//   change the password  sub_140003AB0  (NetUserGetInfo/NetUserSetInfo level 1)
//   remote connect       sub_140003680  (WNetAddConnection2 to \\computer\IPC$)
//   clear console line    sub_140002F30
//
// The shared PsTools front end (banner, EULA gate, target iteration, alternate
// credentials, error reporting, local identity, hidden password prompt) lives in
// ../common/PsTools.h and is reused read-only.
//
// The core is a read-modify-write, not a single setinfo call. The original reads
// the account with NetUserGetInfo at level 1 (USER_INFO_1), overwrites only the
// usri1_password field, and writes the whole record back with NetUserSetInfo at
// level 1. That preserves every other account setting. A domain account (one that
// carries a backslash) is resolved against a domain controller found with
// NetGetAnyDCName rather than a named computer. An omitted new password applies an
// empty password, matching the original's default of a pointer to an empty string.
//
// PsPasswd parses its own command line rather than the shared single-account
// parser, because its grammar carries two positional arguments, the account and an
// optional new password, plus a rule that a domain account cannot be combined with
// a computer or alternate credentials.

#include "../common/PsTools.h"

#include <windows.h>
#include <lm.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    // Version-resource fields of pspasswd64.exe v1.25, used for the banner and the
    // EULA registry key. The original reads these from its own version resource at
    // run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsPasswd",
        "1.25",
        "Local and remote password changer",
        "Copyright (C) 2003-2023 Mark Russinovich",
        "Sysinternals - www.sysinternals.com",
    };

    // The EULA app name is the version-resource InternalName. The registry key is
    // case-insensitive, so this matches HKCU\Software\Sysinternals\PsPasswd.
    constexpr char kApp[] = "PsPasswd";

    // The USER_INFO level the original reads and writes back.
    constexpr DWORD kUserInfoLevel = 1;

    void PrintUsage()
    {
        std::printf("PsPasswd changes passwords on a local or remote system.\n");
        std::printf("\n");
        std::printf("Usage for local accounts:\n"
                    "  pspasswd [\\\\[computer[,computer,[,...]]|@file] [-u Username [-p Password]]] <Account> [NewPassword]\n");
        std::printf("Usage for domain accounts:\n");
        std::printf("  pspasswd <Domain\\Account> [NewPassword]\n");
        std::printf("\n");
        std::printf("     computer    Computer or computers on which the local account exists. If\n");
        std::printf("                 you omit the computer name, the local computer is assumed.\n");
        std::printf("                 If you specify a wildcard (\\\\*), PsPasswd runs the\n");
        std::printf("                 command on all computers in the current domain or workgroup.\n");
        std::printf("     @file       PsPasswd will change the password on the computers listed\n");
        std::printf("                 in the file.\n");
        std::printf("     -u          Specifies optional user name for login to remote computer.\n");
        std::printf("     -p          Specifies optional password for user name. If you omit this\n"
                    "                 you will be prompted to enter a hidden password.\n");
        std::printf("     -nobanner   Do not display the startup banner and copyright message.\n");
        std::printf("     Domain      Specifies name of the domain of the target account.\n");
        std::printf("     Account     Specifies name of the account for password change.\n");
        std::printf("     NewPassword New password. If omitted a NULL password is applied.\n");
        std::printf("\n");
    }

    // Erase the current console line (mirrors sub_140002F30). Progress text is
    // written to stderr, so on a redirected stream this is a harmless carriage
    // return that leaves stdout untouched.
    void ClearLine()
    {
        CONSOLE_SCREEN_BUFFER_INFO info = {};
        ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info);
        std::fputc('\r', stderr);
        for (int i = 0; i < info.dwSize.X - 1; ++i)
        {
            std::fputc(' ', stderr);
        }
        std::fputc('\r', stderr);
    }

    // The NetUser APIs are Unicode only, so the ANSI front-end strings are widened
    // at the boundary. CP_ACP matches the shared framework's other conversions.
    [[nodiscard]] std::wstring Widen(const char* s)
    {
        if (s == nullptr || s[0] == '\0')
        {
            return std::wstring();
        }
        const int n = ::MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        if (n <= 0)
        {
            return std::wstring();
        }
        std::wstring w(static_cast<size_t>(n - 1), L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
        return w;
    }

    // The OS gate the original runs before any work: Windows XP (5.1) or higher.
    // Mirrors sub_140003360.
    [[nodiscard]] bool OsVersionAtLeastXp()
    {
        OSVERSIONINFOEXW info = {};
        info.dwOSVersionInfoSize = sizeof(info);
        info.dwMajorVersion = 5;
        info.dwMinorVersion = 1;
        info.wServicePackMajor = 0;
        info.wServicePackMinor = 0;
        DWORDLONG mask = 0;
        VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
        VER_SET_CONDITION(mask, VER_MINORVERSION, VER_GREATER_EQUAL);
        VER_SET_CONDITION(mask, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
        VER_SET_CONDITION(mask, VER_SERVICEPACKMINOR, VER_GREATER_EQUAL);
        return ::VerifyVersionInfoW(&info,
                                    VER_MAJORVERSION | VER_MINORVERSION
                                        | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR,
                                    mask) != FALSE;
    }

    // Parsed command line. A single optional computer list, the account, an
    // optional new password, and the shared alternate-credential fields.
    struct Options
    {
        std::string computerList;   // \\computer (stripped), comma list, "@file", or "*"
        std::string account;        // the account, possibly "Domain\Account"
        std::string newPassword;    // the new password, empty when omitted
        std::string user;           // -u value
        std::string password;       // -p value (or prompted)
        bool computerSpecified = false;
        bool accountSpecified = false;
        bool haveUser = false;
        bool havePassword = false;
    };

    // Change the password of one account on one computer. Mirrors sub_140003AB0.
    // Returns a Windows or LANMAN status code, 0 on success.
    [[nodiscard]] DWORD ChangePassword(const char* computer, const char* account, const char* newPassword)
    {
        std::wstring wAccount = Widen(account);
        std::wstring wNewPassword = Widen(newPassword);
        std::wstring server;
        const wchar_t* user = wAccount.c_str();

        wchar_t* backslash = std::wcschr(wAccount.data(), L'\\');
        if (backslash != nullptr)
        {
            *backslash = L'\0';
            const wchar_t* domain = wAccount.c_str();
            user = backslash + 1;
            LPBYTE dc = nullptr;
            const NET_API_STATUS st = ::NetGetAnyDCName(nullptr, domain, &dc);
            if (st != NERR_Success)
            {
                return st;
            }
            server = reinterpret_cast<const wchar_t*>(dc);
            ::NetApiBufferFree(dc);
        }
        else
        {
            server = L"\\\\";
            server += Widen(computer);
        }

        LPBYTE buffer = nullptr;
        NET_API_STATUS status = ::NetUserGetInfo(server.c_str(), user, kUserInfoLevel, &buffer);
        if (status == NERR_Success)
        {
            auto* record = reinterpret_cast<USER_INFO_1*>(buffer);
            record->usri1_password = const_cast<LPWSTR>(wNewPassword.c_str());
            status = ::NetUserSetInfo(server.c_str(), user, kUserInfoLevel, buffer, nullptr);
        }
        if (buffer != nullptr)
        {
            ::NetApiBufferFree(buffer);
        }
        return status;
    }

    // Change the password on one computer and report the result. Mirrors
    // sub_140003D90. Returns true on success.
    [[nodiscard]] bool PerComputer(const char* computer, const Options& o)
    {
        bool connected = false;
        if (o.haveUser)
        {
            connected = pstools::RemoteConnect(computer, o.user.c_str(), o.password.c_str());
        }

        ClearLine();
        if (std::strchr(o.account.c_str(), '\\') != nullptr)
        {
            std::fprintf(stderr, "\rChanging password for %s...", o.account.c_str());
        }
        else
        {
            std::fprintf(stderr, "\rChanging password for %s\\%s...", computer, o.account.c_str());
        }
        std::fflush(stderr);

        const DWORD status = ChangePassword(computer, o.account.c_str(), o.newPassword.c_str());
        if (status != NERR_Success)
        {
            ClearLine();
            std::fprintf(stderr, "\rError changing password:\n");
            pstools::PrintSystemError(status);
            return false;
        }

        ClearLine();
        std::printf("\rPassword successfully changed.\n\n");
        std::fflush(stdout);
        if (connected)
        {
            pstools::RemoteDisconnect(computer);
        }
        return true;
    }

    // Parse the PsPasswd command line. Mirrors sub_140003EB0. Returns false when the
    // caller should print usage and fail. -nobanner and -accepteula have already
    // been stripped by the banner and EULA driver.
    [[nodiscard]] bool ParseCommandLine(int argc, char** argv, Options& o)
    {
        for (int i = 1; i < argc; ++i)
        {
            char* a = argv[i];
            if (a[0] == '-' || a[0] == '/')
            {
                if (a[1] == '\0')
                {
                    continue;  // a bare '-' is ignored, as in the original
                }
                const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(a[1])));
                if (c == 'u')
                {
                    if (i + 1 >= argc || o.haveUser)
                    {
                        return false;
                    }
                    o.user = argv[++i];
                    o.haveUser = true;
                }
                else if (c == 'p')
                {
                    if (i + 1 >= argc || o.havePassword)
                    {
                        return false;
                    }
                    o.password = argv[++i];
                    o.havePassword = true;
                }
                else
                {
                    return false;
                }
            }
            else if (a[0] == '@')
            {
                o.computerList = a;  // keep the '@' so ForEachComputer sees it
                o.computerSpecified = true;
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                if (o.computerSpecified)
                {
                    return false;
                }
                o.computerList = a + 2;
                o.computerSpecified = true;
            }
            else
            {
                if (o.accountSpecified)
                {
                    return false;
                }
                o.account = a;
                o.accountSpecified = true;
                // The argument immediately after the account, if any, is the new
                // password, taken verbatim whatever its leading character.
                if (i + 1 < argc)
                {
                    o.newPassword = argv[++i];
                }
            }
        }

        const bool domainAccount =
            o.accountSpecified && o.account.find('\\') != std::string::npos;

        if (!o.accountSpecified
            || (o.haveUser && !o.computerSpecified)
            || (o.havePassword && !o.haveUser)
            || (domainAccount && (o.computerSpecified || o.haveUser)))
        {
            return false;
        }

        if (o.computerList.empty())
        {
            o.computerList = pstools::LocalComputerName();
        }
        return true;
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

    if (!OsVersionAtLeastXp())
    {
        std::fprintf(stderr, "PsPasswd requires Windows XP or higher.\n\n");
        return -1;
    }

    Options o;
    if (!ParseCommandLine(argc, argv, o))
    {
        PrintUsage();
        return -1;
    }

    // -u without -p prompts for a hidden password, as the original does.
    if (o.haveUser && !o.havePassword)
    {
        o.password = pstools::PromptHiddenPassword();
        o.havePassword = true;
    }

    const bool ok = pstools::ForEachComputer(o.computerList, true,
        [&](const char* computer) { return PerComputer(computer, o); });
    return ok ? 0 : -1;
}
