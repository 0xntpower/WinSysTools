// PsGetSid -- displays the SID of a computer or user account, or translates a
// SID back to its account name. Source-accurate C++20 console reconstruction of
// the Sysinternals PsGetsid64.exe v1.46.
//
// Behaviour recovered with IDA (idalib) from PsGetsid64.exe:
//   main                 sub_1400062E0
//   per-computer worker  sub_140005EA0
//   account/SID xlate    sub_1400053C0
//   machine SID (LSA)    sub_140004B40  (LsaQueryInformationPolicy, account domain)
//   SID -> string        sub_140004DC0 / sub_140004B40  (manual, 0x hex for a
//                                       high identifier authority, else decimal)
//   string -> SID        sub_140004900
//   remote machine SID   sub_1400059D0  (GETSIDSV service over a named pipe)
//   usage                sub_140006090
//
// The shared PsTools front end (argument parsing, banner, EULA gate, target
// list, alternate credentials, error text) lives in ../common/PsTools.h.
//
// The SID<->string conversions use the platform's ConvertSidToStringSidA and
// ConvertStringSidToSidA. Their output is identical to the original's hand-built
// format (decimal authority, or a 12-digit 0x form when the top word is set),
// so the printed SIDs match byte for byte while the code stays idiomatic.

#include "../common/PsTools.h"

#include <windows.h>
#include <sddl.h>
#include <ntsecapi.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    // Version-resource fields of PsGetsid64.exe v1.46, used for the banner and
    // the EULA registry key. The original reads these from its own version
    // resource at run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsGetSid",
        "1.46",
        "Translates SIDs to names and vice versa",
        "Independent source-accurate recreation of PsGetSid by Sysinternals, through reverse engineering and rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name is the version-resource InternalName. The registry key
    // is case-insensitive, so this matches HKCU\Software\Sysinternals\PsGetsid.
    constexpr char kApp[] = "PsGetSid";

    void PrintUsage(const char* exePath)
    {
        std::printf("\nUsage: %s [\\\\computer[,computer2[,...] | @file] [-u Username [-p Password]]] [account | SID]\n",
                    exePath);
        std::printf("     -u         Specifies optional user name for login to\n"
                    "                remote computer.\n");
        std::printf("     -p         Specifies optional password for user name. If you omit this\n");
        std::printf("                you will be prompted to enter a hidden password.\n");
        std::printf("     account    PsGetSid will report the SID for the specified user account\n"
                    "                rather than the computer.\n");
        std::printf("     SID        PsGetSid will report the account for the specified SID.\n");
        std::printf("     computer   Direct PsGetSid to perform the command on the remote\n");
        std::printf("                computer or computers specified. If you omit the computer\n");
        std::printf("                name PsGetSid runs the command on the local system, \n");
        std::printf("                and if you specify a wildcard (\\\\*), PsGetSid runs the\n");
        std::printf("                command on all computers in the current domain.\n");
        std::printf("     @file      PsGetSid will execute the command on each of the computers listed\n");
        std::printf("                in the file.\n");
        std::printf("     -nobanner  Do not display the startup banner and copyright message.\n");
        std::printf("\n");
    }

    // Erase the current console line (mirrors sub_140003110). Progress text is
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

    const char* SidTypeName(SID_NAME_USE use)
    {
        switch (use)
        {
        case SidTypeUser:            return "User";
        case SidTypeGroup:           return "Group";
        case SidTypeDomain:          return "Domain";
        case SidTypeAlias:           return "Alias";
        case SidTypeWellKnownGroup:  return "Well Known Group";
        case SidTypeDeletedAccount:  return "Deleted Account";
        case SidTypeInvalid:         return "Invalid";
        case SidTypeComputer:        return "Computer";
        case SidTypeLabel:           return "Label";
        default:                     return "Unknown";
        }
    }

    [[nodiscard]] std::string SidToString(PSID sid)
    {
        LPSTR text = nullptr;
        if (!::ConvertSidToStringSidA(sid, &text))
        {
            return {};
        }
        std::string out = text;
        ::LocalFree(text);
        return out;
    }

    // Query the local machine SID: the account-domain SID from the LSA policy
    // database. Mirrors sub_140004B40. Returns "" and sets last error on failure.
    [[nodiscard]] std::string MachineSid()
    {
        LSA_OBJECT_ATTRIBUTES oa = {};
        oa.Length = sizeof(oa);
        LSA_HANDLE policy = nullptr;
        NTSTATUS st = ::LsaOpenPolicy(nullptr, &oa, POLICY_VIEW_LOCAL_INFORMATION, &policy);
        if (st != 0)
        {
            ::SetLastError(::LsaNtStatusToWinError(st));
            return {};
        }
        POLICY_ACCOUNT_DOMAIN_INFO* info = nullptr;
        st = ::LsaQueryInformationPolicy(policy, PolicyAccountDomainInformation,
                                         reinterpret_cast<void**>(&info));
        if (st != 0 || info == nullptr)
        {
            ::LsaClose(policy);
            ::SetLastError(::LsaNtStatusToWinError(st));
            return {};
        }
        std::string sid = SidToString(info->DomainSid);
        ::LsaFreeMemory(info);
        ::LsaClose(policy);
        return sid;
    }

    // An argument is a SID string when it starts with "S-1" (case-insensitive on
    // the leading S), matching the original's toupper check.
    [[nodiscard]] bool LooksLikeSid(const char* s)
    {
        return (s[0] == 'S' || s[0] == 's') && s[1] == '-' && s[2] == '1';
    }

    // Report the SID for an account name on the given computer. Mirrors the
    // account branch of sub_1400053C0.
    [[nodiscard]] bool ReportAccountSid(const char* computer, const char* account)
    {
        DWORD cbSid = 0;
        DWORD cchDomain = 0;
        SID_NAME_USE use = SidTypeUnknown;
        ::LookupAccountNameA(computer, account, nullptr, &cbSid, nullptr, &cchDomain, &use);
        if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<BYTE> sid(cbSid);
            std::string domain(cchDomain, '\0');
            if (::LookupAccountNameA(computer, account, sid.data(), &cbSid,
                                     domain.data(), &cchDomain, &use))
            {
                domain.resize(cchDomain);
                const std::string sidText = SidToString(sid.data());
                if (std::strchr(account, '\\') != nullptr)
                {
                    std::printf("SID for %s:\n", account);
                }
                else
                {
                    std::printf("SID for %s\\%s:\n", domain.c_str(), account);
                }
                std::printf("%s\n\n", sidText.c_str());
                return true;
            }
        }
        std::fprintf(stderr, "Error querying account:\n");
        pstools::PrintSystemError(::GetLastError());
        return false;
    }

    // Report the account for a SID string on the given computer. Mirrors the SID
    // branch of sub_1400053C0.
    [[nodiscard]] bool ReportSidAccount(const char* computer, const char* sidArg, const std::string& local)
    {
        PSID sid = nullptr;
        if (!::ConvertStringSidToSidA(sidArg, &sid) || sid == nullptr)
        {
            std::fprintf(stderr, "Error: The SID specified is not a valid SID.\n\n");
            return false;
        }

        DWORD cchName = 0;
        DWORD cchDomain = 0;
        SID_NAME_USE use = SidTypeUnknown;
        ::LookupAccountSidA(computer, sid, nullptr, &cchName, nullptr, &cchDomain, &use);
        if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            std::string name(cchName, '\0');
            std::string domain(cchDomain, '\0');
            if (::LookupAccountSidA(computer, sid, name.data(), &cchName,
                                    domain.data(), &cchDomain, &use))
            {
                name.resize(cchName);
                domain.resize(cchDomain);
                ::LocalFree(sid);
                if (::_stricmp(computer, local.c_str()) == 0)
                {
                    std::printf("Account for %s:\n", sidArg);
                }
                else
                {
                    std::printf("Account for %s\\%s:\n", computer, sidArg);
                }
                std::printf("%s", SidTypeName(use));
                std::printf(": ");
                if (!name.empty())
                {
                    std::printf("%s\\%s\n", domain.c_str(), name.c_str());
                }
                else
                {
                    std::printf("%s\n", domain.c_str());
                }
                std::printf("\n");
                return true;
            }
        }
        ::LocalFree(sid);
        std::fprintf(stderr, "Error querying SID:\n");
        pstools::PrintSystemError(::GetLastError());
        return false;
    }

    [[nodiscard]] bool TranslateAccountOrSid(const char* computer, const std::string& arg, const std::string& local)
    {
        if (LooksLikeSid(arg.c_str()))
        {
            return ReportSidAccount(computer, arg.c_str(), local);
        }
        return ReportAccountSid(computer, arg.c_str());
    }

    // Report a remote computer's machine SID through the PsGetSid service.
    // Mirrors the client half of sub_1400059D0.
    //
    // ponytail: the original first deploys a bundled GETSIDSV helper to the
    // remote ADMIN$ and starts it through the SCM (sub_140003930) before talking
    // to its \pipe\getsidsv named pipe. That helper executable is out of scope,
    // so this reproduces the faithful client protocol (a 260-byte request and
    // response, status DWORD then SID text) and the exact error path that runs
    // when the service is not present on the target.
    [[nodiscard]] bool RemoteMachineSid(const char* computer)
    {
        char pipe[MAX_PATH] = {};
        std::snprintf(pipe, std::size(pipe), "\\\\%s\\pipe\\getsidsv", computer);
        const HANDLE h = ::CreateFileA(pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            ClearLine();
            std::fprintf(stderr, "\rError establishing communication with psgetsid service on %s:\n", computer);
            pstools::PrintSystemError(::GetLastError());
            return false;
        }

        constexpr DWORD kPacket = 0x104;
        unsigned char buffer[kPacket] = {};
        DWORD moved = 0;
        if (!::WriteFile(h, buffer, kPacket, &moved, nullptr)
            || !::ReadFile(h, buffer, kPacket, &moved, nullptr))
        {
            ClearLine();
            std::fprintf(stderr, "\rError communicating with psgetsid service on %s:\n", computer);
            pstools::PrintSystemError(::GetLastError());
            ::CloseHandle(h);
            return false;
        }
        ::CloseHandle(h);

        DWORD status = 0;
        std::memcpy(&status, buffer, sizeof(status));
        ClearLine();
        if (status != 0)
        {
            std::printf("\rError getting SID for %s:\n", computer);
            pstools::PrintSystemError(status);
            return false;
        }
        std::printf("\rSID for \\\\%s:\n", computer);
        std::printf("%s\n\n", reinterpret_cast<const char*>(buffer + sizeof(status)));
        return true;
    }

    // Run the requested query against one computer. Mirrors sub_140005EA0.
    [[nodiscard]] bool PerComputer(const char* computer, const pstools::Targets& t, const std::string& local)
    {
        const bool isLocal = ::_stricmp(computer, local.c_str()) == 0;

        if (t.accountSpecified)
        {
            bool connected = false;
            if (!isLocal && t.haveUser && t.havePassword)
            {
                std::fprintf(stderr, "\rRemote login on %s...\n", computer);
                if (!pstools::RemoteConnect(computer, t.user.c_str(), t.password.c_str()))
                {
                    std::fprintf(stderr, "\rRemote login failed.\n");
                    return false;
                }
                connected = true;
            }
            const bool ok = TranslateAccountOrSid(computer, t.account, local);
            if (connected)
            {
                pstools::RemoteDisconnect(computer);
            }
            return ok;
        }

        if (isLocal)
        {
            const std::string sid = MachineSid();
            if (sid.empty())
            {
                std::fprintf(stderr, "Error querying SID:\n");
                pstools::PrintSystemError(::GetLastError());
                return false;
            }
            ClearLine();
            std::printf("\rSID for \\\\%s:\n", computer);
            std::printf("%s\n\n", sid.c_str());
            return true;
        }

        return RemoteMachineSid(computer);
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

    pstools::Targets t;
    if (!pstools::ParseCommandLine(argc, argv, t))
    {
        PrintUsage(argv[0]);
        return -1;
    }

    // -u without -p prompts for a hidden password, as the original does.
    if (t.haveUser && !t.havePassword)
    {
        t.password = pstools::PromptHiddenPassword();
        t.havePassword = true;
    }

    const std::string local = pstools::LocalComputerName();
    const bool ok = pstools::ForEachComputer(t.computerList, true,
        [&](const char* computer) { return PerComputer(computer, t, local); });
    return ok ? 0 : -1;
}
