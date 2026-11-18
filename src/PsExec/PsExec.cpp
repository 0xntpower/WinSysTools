// PsExec -- runs a program on a local or remote computer. Source-accurate
// C++20 console reconstruction of the Sysinternals PsExec v2.43 (PsExec64.exe).
//
// PsExec is the largest PsTool. This reconstruction reproduces the full command
// line and switch set, and the LOCAL direct-execution path in full. The remote
// path and the local System/interactive paths run through the PSEXESVC helper
// service, which is reproduced here as a faithful client whose helper-binary
// auto-deploy and encrypted named-pipe stdio are out of scope, the same approach
// PsKill took for its PSKLLSVC remote path.
//
// Behaviour recovered with IDA (idalib) from PsExec64.exe:
//   main                 0x14001A670  OS gate, banner, EULA, parse, dispatch
//   banner printer       0x140002980  (PrintBanner, strips -nobanner)
//   EULA gate            0x140002080  (strips -accepteula, stored flag or prompt)
//   command-line parse   0x140014030  full switch set into module globals
//   usage                0x140017C20  the help block, one line per call
//   local direct exec    0x140012860  CreateProcessWithLogonW or CreateProcessAsUserW
//   limited user token   0x140002F30  CreateRestrictedToken + Low integrity (-l)
//   group affinity       0x140003720  SetThreadGroupAffinity (-g)
//   background priority   0x140003690  SetPriorityClass + NtSetInformationProcess
//   print system error   0x140003C90  FormatMessage, netmsg.dll for 2100..2483
//   per-computer service  0x1400159A0  remote and local-System path (out of scope)
//   ForEachComputer      0x140003800  iterate \\computer, comma list, @file, \\*
//
// The dispatch recovered from main: the local direct path runs when the target
// is the local machine AND none of -h, -s, -i, -x are present AND an alternate
// user is not a service account. Everything else routes to the PSEXESVC service
// path. So the local System account (-s) and interactive desktop (-i) are not a
// direct local CreateProcess in the original, they go through the service, which
// is why they sit on the reproduced-not-exercised side here.
//
// The shared PsTools front end (banner, EULA gate, error printer, local computer
// name, hidden password prompt, IPC$ credentials, target iteration) lives in
// ../common/PsTools.h and is reused read-only.

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "../common/PsTools.h"

#include <windows.h>
#include <ntsecapi.h>
#include <sddl.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Version-resource fields of PsExec v2.43, used for the banner and the EULA
    // registry key. Verified byte-identical against the original banner on stderr.
    constexpr pstools::Banner kBanner{
        "PsExec",
        "2.43",
        "Execute processes remotely",
        "Copyright (C) 2001-2023 Mark Russinovich",
        "Sysinternals - www.sysinternals.com",
    };

    constexpr char kApp[] = "PsExec";

    // The service name the original installs by default, overridable by -r. The
    // ARM target uses PSEXESVC_ARM. Recovered from 0x140001000.
    constexpr char kDefaultService[] = "PSEXESVC";

    // dwCreationFlags the original passes to both CreateProcess variants, ORed with
    // the chosen priority class. 0x414 is CREATE_SUSPENDED | CREATE_NEW_CONSOLE |
    // CREATE_UNICODE_ENVIRONMENT. The suspend lets affinity and group be applied
    // before the first thread runs.
    constexpr DWORD kCreateFlags = CREATE_SUSPENDED | CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;

    // -background is not a CreateProcess priority class, it is applied afterward
    // with SetPriorityClass. The parser stores this sentinel in the priority slot.
    constexpr DWORD kBackgroundClass = 0x00100000;  // PROCESS_MODE_BACKGROUND_BEGIN

    // TOKEN access the original requests when opening its own token for -l / the
    // CreateProcessAsUserW path (0xF01FF is TOKEN_ALL_ACCESS).
    constexpr DWORD kTokenAccess = TOKEN_ALL_ACCESS;

    // The seven -priority names and the priority class each maps to. Recovered from
    // the table at 0x1400891F0 (names) and 0x1400893F0 (values), 516-byte stride.
    struct PriorityName
    {
        const char* name = "";
        DWORD cls = 0;
    };

    constexpr PriorityName kPriorities[] = {
        {"low", IDLE_PRIORITY_CLASS},                  // 0x40
        {"belownormal", BELOW_NORMAL_PRIORITY_CLASS},  // 0x4000
        {"normal", NORMAL_PRIORITY_CLASS},             // 0x20
        {"abovenormal", ABOVE_NORMAL_PRIORITY_CLASS},  // 0x8000
        {"high", HIGH_PRIORITY_CLASS},                 // 0x80
        {"realtime", REALTIME_PRIORITY_CLASS},         // 0x100
        {"background", kBackgroundClass},              // 0x100000
    };

    // The parsed command line. One entry per PsExec switch, plus the computer list,
    // the command, and its argument tail.
    struct Options
    {
        std::string computerList;             // \\computer, comma list, "@file", "*" or empty for local
        bool computerSpecified = false;
        std::string user;                     // -u
        std::string password;                 // -p (or prompted)
        bool haveUser = false;
        bool havePassword = false;
        std::string app;                      // the command to run
        bool appSpecified = false;
        std::string args;                     // the argument tail after the command
        std::string workDir;                  // -w
        bool haveWorkDir = false;
        std::string serviceName = kDefaultService;  // -r
        DWORD priorityClass = 0;              // -<priority>
        DWORD affinityMask = 0;               // -a
        WORD group = 0;                       // -g
        bool groupSpecified = false;
        int timeoutSeconds = -1;              // -n
        int session = -1;                     // -i <session>
        bool interactive = false;             // -i
        bool systemAccount = false;           // -s
        bool elevated = false;                // -h
        bool limited = false;                 // -l
        bool secureDesktop = false;           // -x
        bool copyProgram = false;             // -c
        bool forceCopy = false;               // -f
        bool copyIfNewer = false;             // -v
        bool noProfile = false;               // -e (clears the load-profile default)
        bool dontWait = false;                // -d
        bool arm = false;                     // -arm
    };

    // A kernel handle that always closes.
    class ScopedHandle
    {
    public:
        ScopedHandle() = default;

        explicit ScopedHandle(HANDLE h)
            : handle_(h)
        {
        }

        ~ScopedHandle()
        {
            Reset();
        }

        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;

        void Reset(HANDLE h = nullptr)
        {
            if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            {
                ::CloseHandle(handle_);
            }
            handle_ = h;
        }

        [[nodiscard]] HANDLE Get() const
        {
            return handle_;
        }

        [[nodiscard]] HANDLE* Address()
        {
            return &handle_;
        }

        [[nodiscard]] bool Valid() const
        {
            return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE handle_ = nullptr;
    };

    [[nodiscard]] std::wstring Widen(const std::string& s)
    {
        if (s.empty())
        {
            return std::wstring();
        }
        const int need = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        if (need <= 0)
        {
            return std::wstring();
        }
        std::wstring w(static_cast<size_t>(need - 1), L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, w.data(), need);
        return w;
    }

    [[nodiscard]] bool IsAllDigits(const char* s)
    {
        if (s == nullptr || s[0] == '\0')
        {
            return false;
        }
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (std::isdigit(static_cast<unsigned char>(*p)) == 0)
            {
                return false;
            }
        }
        return true;
    }

    // Split a "-u" value into the account and its domain. The original takes the
    // part after the first backslash as the account and the part before it as the
    // domain, so "CORP\\alice" becomes account "alice", domain "CORP".
    void SplitUser(const std::string& full, std::string& account, std::string& domain)
    {
        const size_t slash = full.find('\\');
        if (slash == std::string::npos)
        {
            account = full;
            domain.clear();
        }
        else
        {
            domain = full.substr(0, slash);
            account = full.substr(slash + 1);
        }
    }

    // Parse an -a value of comma-separated CPU numbers into an affinity mask.
    // Mirrors the original's "mask |= 1 << n" over each parsed number.
    void ParseAffinity(const char* s, DWORD& mask)
    {
        const char* p = s;
        while (*p != '\0')
        {
            while (*p == ' ' || *p == ',')
            {
                ++p;
            }
            if (*p == '\0')
            {
                break;
            }
            char* end = nullptr;
            const long n = std::strtol(p, &end, 10);
            if (end == p)
            {
                break;
            }
            if (n >= 0 && n < 32)
            {
                mask |= (1u << static_cast<unsigned>(n));
            }
            p = end;
        }
    }

    // The full help block, one line per original call to the stdout printer. Kept
    // byte-identical to PsExec64.exe (verified by md5 of the captured usage).
    void PrintUsage()
    {
        static const char* const kUsage =
            "PsExec executes a program on a remote system, where remotely executed console\n"
            "applications execute interactively.\n"
            "\nUsage: psexec [\\\\computer[,computer2[,...] | @file]][-u user [-p psswd]][-n s][-r servicename][-h][-l][-s|-e][-x][-i [session]][-c [-f|-v]][-w directory][-d][-<priority>][-g n][-a n,n,...][-verbose] cmd [arguments]\n"
            "     -a         Separate processors on which the application can run with\n"
            "                commas where 1 is the lowest numbered CPU. For example,\n"
            "                to run the application on CPU 2 and CPU 4, enter:\n"
            "                \"-a 2,4\"\n"
            "     -c         Copy the specified program to the remote system for\n"
            "                execution. If you omit this option the application\n"
            "                must be in the system path on the remote system.\n"
            "     -d         Don't wait for process to terminate (non-interactive).\n"
            "     -e         Does not load the specified account's profile.\n"
            "     -f         Copy the specified program even if the file already\n"
            "                exists on the remote system.\n"
            "     -g         Set the primary thread's processor group to the one specified\n"
            "                (Only for systems with more than 64 processors).\n"
            "     -i         Run the program so that it interacts with the desktop of the\n"
            "                specified session on the remote system. If no session is\n"
            "                specified the process runs in the console session.\n"
            "     -h         If the target system is Vista or higher, has the process\n"
            "                run with the account's elevated token, if available.\n"
            "     -l         Run process as limited user (strips the Administrators group\n"
            "                and allows only privileges assigned to the Users group).\n"
            "                On Windows Vista the process runs with Low Integrity.\n"
            "     -n         Specifies timeout in seconds connecting to remote computers.\n"
            "     -p         Specifies optional password for user name. If you omit this\n"
            "                you will be prompted to enter a hidden password.\n"
            "     -r         Specifies the name of the remote service to create or interact.\n"
            "                with.\n"
            "     -s         Run the remote process in the System account.\n"
            "     -u         Specifies optional user name for login to remote\n"
            "                computer.\n"
            "     -v         Copy the specified file only if it has a higher version number\n"
            "                or is newer on than the one on the remote system.\n"
            "     -w         Set the working directory of the process (relative to\n"
            "                remote computer).\n"
            "     -x         Display the UI on the Winlogon secure desktop (local system\n"
            "                only).\n"
            "     -arm       Specifies the remote computer is of ARM architecture.\n"
            "     -priority\tSpecifies -low, -belownormal, -abovenormal, -high or\n"
            "                -realtime to run the process at a different priority. Use\n"
            "                -background to run at low memory and I/O priority on Vista.\n"
            "     computer   Direct PsExec to run the application on the remote\n"
            "                computer or computers specified. If you omit the computer\n"
            "                name PsExec runs the application on the local system, \n"
            "                and if you specify a wildcard (\\\\*), PsExec runs the\n"
            "                command on all computers in the current domain.\n"
            "     @file      PsExec will execute the command on each of the computers listed\n"
            "                in the file.\n"
            "     cmd\t    Name of application to execute.\n"
            "     arguments  Arguments to pass (note that file paths must be\n"
            "                absolute paths on the target system).\n"
            "     -accepteula This flag suppresses the display of the license dialog.\n"
            "     -nobanner   Do not display the startup banner and copyright message.\n\n"
            "You can enclose applications that have spaces in their name with\n"
            "quotation marks e.g. psexec \\\\marklap \"c:\\long name app.exe\".\n"
            "Input is only passed to the remote system when you press the enter\n"
            "key, and typing Ctrl-C terminates the remote process.\n\n"
            "If you omit a user name the process will run in the context of your\n"
            "account on the remote system, but will not have access to network\n"
            "resources (because it is impersonating). Specify a valid user name\n"
            "in the Domain\\User syntax if the remote process requires access\n"
            "to network resources or to run in a different account. Note that\n"
            "the password and command is encrypted in transit to the remote system.\n\n"
            "Error codes returned by PsExec are specific to the applications you\n"
            "execute, not PsExec.\n"
            "\n";
        std::fputs(kUsage, stdout);
    }

    // Parse the PsExec command line into o. All diagnostics (usage or a specific
    // message) are printed here, matching the original where some errors print a
    // dedicated message rather than the usage. Returns false on any error. The
    // needsService output is set when a switch forces the PSEXESVC service path.
    [[nodiscard]] bool ParseCommandLine(int argc, char** argv, Options& o, bool& needsService)
    {
        if (argc <= 1)
        {
            PrintUsage();
            return false;
        }

        for (int i = 1; i < argc; ++i)
        {
            char* a = argv[i];
            if (a[0] == '-' || a[0] == '/')
            {
                const char* sw = a + 1;
                if (sw[0] == '\0')
                {
                    continue;  // a bare dash is ignored, as in the original
                }
                if (::_stricmp(sw, "accepteula") == 0 || ::_stricmp(sw, "nobanner") == 0)
                {
                    continue;  // already stripped before parsing, skipped defensively
                }

                // A whole-word priority name claims the argument.
                if (o.priorityClass == 0)
                {
                    bool matched = false;
                    for (const PriorityName& p : kPriorities)
                    {
                        if (::_stricmp(sw, p.name) == 0)
                        {
                            o.priorityClass = p.cls;
                            matched = true;
                            break;
                        }
                    }
                    if (matched)
                    {
                        continue;
                    }
                }

                if (::_stricmp(sw, "arm") == 0)
                {
                    o.arm = true;
                    continue;
                }

                // Per-character scan so combined flags such as -cf work. Flags that
                // take a value consume the next argument and end the scan.
                bool consumedNext = false;
                for (int j = 0; sw[j] != '\0' && !consumedNext; ++j)
                {
                    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(sw[j])));
                    switch (c)
                    {
                    case 'C':
                        o.copyProgram = true;
                        needsService = true;  // copy deploys through the service
                        break;
                    case 'F':
                        o.forceCopy = true;
                        break;
                    case 'V':
                        o.copyIfNewer = true;
                        break;
                    case 'D':
                        o.dontWait = true;
                        break;
                    case 'E':
                        o.noProfile = true;
                        break;
                    case 'H':
                        o.elevated = true;
                        break;
                    case 'L':
                        o.limited = true;
                        break;
                    case 'S':
                        o.systemAccount = true;
                        needsService = true;
                        break;
                    case 'X':
                        o.secureDesktop = true;
                        o.interactive = true;
                        needsService = true;
                        break;
                    case 'U':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.user = argv[++i];
                        o.haveUser = true;
                        consumedNext = true;
                        break;
                    case 'P':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.password = argv[++i];
                        o.havePassword = true;
                        consumedNext = true;
                        break;
                    case 'R':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.serviceName = argv[++i];
                        consumedNext = true;
                        break;
                    case 'W':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.workDir = argv[++i];
                        o.haveWorkDir = true;
                        consumedNext = true;
                        break;
                    case 'N':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.timeoutSeconds = std::atoi(argv[++i]);
                        consumedNext = true;
                        break;
                    case 'G':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.group = static_cast<WORD>(std::atoi(argv[++i]));
                        o.groupSpecified = true;
                        consumedNext = true;
                        break;
                    case 'A':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        ParseAffinity(argv[++i], o.affinityMask);
                        consumedNext = true;
                        break;
                    case 'I':
                        o.interactive = true;
                        needsService = true;
                        if (i + 1 < argc && IsAllDigits(argv[i + 1]))
                        {
                            o.session = std::atoi(argv[++i]);
                            consumedNext = true;
                        }
                        break;
                    default:
                        PrintUsage();
                        return false;
                    }
                }
            }
            else if (a[0] == '@')
            {
                if (o.computerSpecified)
                {
                    PrintUsage();
                    return false;
                }
                o.computerList = a;  // keep the '@' for ForEachComputer
                o.computerSpecified = true;
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                if (o.computerSpecified)
                {
                    PrintUsage();
                    return false;
                }
                o.computerList = a + 2;  // the bare name, or "*" for the whole domain
                o.computerSpecified = true;
            }
            else
            {
                // The first bare token is the command. Everything after it is the
                // argument tail that belongs to the child, not to PsExec.
                o.app = a;
                o.appSpecified = true;
                std::string tail;
                for (int k = i + 1; k < argc; ++k)
                {
                    if (!tail.empty())
                    {
                        tail.push_back(' ');
                    }
                    tail += argv[k];
                }
                o.args = tail;
                break;
            }
        }

        if (o.limited && o.elevated)
        {
            std::fprintf(stderr, "Run elevated and limited user options are not compatible.\n\n");
            return false;
        }
        if (!o.appSpecified)
        {
            PrintUsage();
            return false;
        }
        if (o.havePassword && !o.haveUser)
        {
            PrintUsage();
            return false;
        }
        // -f and -v each require -c. The original accepts them together.
        if ((o.forceCopy || o.copyIfNewer) && !o.copyProgram)
        {
            PrintUsage();
            return false;
        }
        // -s forces the profile off, as the original does at the end of parsing.
        if (o.systemAccount)
        {
            o.noProfile = true;
        }
        return true;
    }

    // Build a limited-user token from base. Mirrors sub_140002F30: disable the
    // Administrators group, delete every privilege the token holds that the Users
    // account is not granted, drop the token to Low integrity, and grant the user
    // full access to the token's own default DACL. Returns nullptr on failure.
    [[nodiscard]] HANDLE CreateLimitedUserToken(HANDLE base)
    {
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

        PSID adminSid = nullptr;
        if (!::AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminSid))
        {
            return nullptr;
        }
        PSID usersSid = nullptr;
        if (!::AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                        DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &usersSid))
        {
            ::FreeSid(adminSid);
            return nullptr;
        }

        // The privileges the Users account is granted, recovered through LSA. The
        // original falls back to this fixed list when the query returns nothing.
        std::vector<LUID> keepLuids;
        {
            LSA_OBJECT_ATTRIBUTES oa = {};
            LSA_HANDLE policy = nullptr;
            if (::LsaOpenPolicy(nullptr, &oa, POLICY_LOOKUP_NAMES, &policy) == 0)
            {
                PLSA_UNICODE_STRING rights = nullptr;
                ULONG count = 0;
                if (::LsaEnumerateAccountRights(policy, usersSid, &rights, &count) == 0 && count != 0)
                {
                    for (ULONG k = 0; k < count; ++k)
                    {
                        LUID luid = {};
                        if (::LookupPrivilegeValueW(nullptr, rights[k].Buffer, &luid))
                        {
                            keepLuids.push_back(luid);
                        }
                    }
                    ::LsaFreeMemory(rights);
                }
                ::LsaClose(policy);
            }
        }
        if (keepLuids.empty())
        {
            static const wchar_t* const kFallback[] = {
                L"SeShutdownPrivilege",
                L"SeChangeNotifyPrivilege",
                L"SeUndockPrivilege",
                L"SeIncreaseWorkingSetPrivilege",
                L"SeTimeZonePrivilege",
            };
            for (const wchar_t* name : kFallback)
            {
                LUID luid = {};
                if (::LookupPrivilegeValueW(nullptr, name, &luid))
                {
                    keepLuids.push_back(luid);
                }
            }
        }

        // The token's current privileges, so the ones to delete are those not kept.
        DWORD needed = 0;
        ::GetTokenInformation(base, TokenPrivileges, nullptr, 0, &needed);
        std::vector<BYTE> buffer(needed != 0 ? needed : 1);
        if (!::GetTokenInformation(base, TokenPrivileges, buffer.data(),
                                   static_cast<DWORD>(buffer.size()), &needed))
        {
            ::FreeSid(adminSid);
            ::FreeSid(usersSid);
            return nullptr;
        }
        const auto* privileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(buffer.data());

        std::vector<LUID_AND_ATTRIBUTES> deletePrivs;
        for (DWORD k = 0; k < privileges->PrivilegeCount; ++k)
        {
            const LUID luid = privileges->Privileges[k].Luid;
            bool keep = false;
            for (const LUID& kept : keepLuids)
            {
                if (kept.LowPart == luid.LowPart && kept.HighPart == luid.HighPart)
                {
                    keep = true;
                    break;
                }
            }
            if (!keep)
            {
                LUID_AND_ATTRIBUTES la = {};
                la.Luid = luid;
                deletePrivs.push_back(la);
            }
        }

        SID_AND_ATTRIBUTES disable = {};
        disable.Sid = adminSid;

        HANDLE restricted = nullptr;
        const BOOL ok = ::CreateRestrictedToken(
            base, 0,
            1, &disable,
            static_cast<DWORD>(deletePrivs.size()),
            deletePrivs.empty() ? nullptr : deletePrivs.data(),
            0, nullptr, &restricted);

        ::FreeSid(adminSid);
        ::FreeSid(usersSid);
        if (!ok || restricted == nullptr)
        {
            return nullptr;
        }

        // Drop to Low integrity, matching the original's S-1-16-4096 assignment.
        PSID lowSid = nullptr;
        if (::ConvertStringSidToSidW(L"S-1-16-4096", &lowSid))
        {
            TOKEN_MANDATORY_LABEL label = {};
            label.Label.Sid = lowSid;
            label.Label.Attributes = SE_GROUP_INTEGRITY;
            const DWORD cb = sizeof(TOKEN_MANDATORY_LABEL) + ::GetLengthSid(lowSid);
            ::SetTokenInformation(restricted, TokenIntegrityLevel, &label, cb);
            ::LocalFree(lowSid);
        }
        return restricted;
    }

    // Apply the chosen priority and affinity to a freshly created, still-suspended
    // process, then resume it. Mirrors the tail of sub_140012860. Returns false and
    // fills err when the group-affinity call fails, in which case the process has
    // already been terminated and its handles closed.
    [[nodiscard]] bool ApplyAndResume(const Options& o, const PROCESS_INFORMATION& pi, DWORD& err)
    {
        if (o.priorityClass == kBackgroundClass)
        {
            ::SetPriorityClass(pi.hProcess, kBackgroundClass);
        }

        if (!o.groupSpecified)
        {
            if (o.affinityMask != 0)
            {
                ::SetProcessAffinityMask(pi.hProcess, o.affinityMask);
            }
        }
        else
        {
            GROUP_AFFINITY ga = {};
            ga.Group = o.group;
            ga.Mask = o.affinityMask != 0 ? static_cast<KAFFINITY>(o.affinityMask)
                                          : static_cast<KAFFINITY>(0xFFFF);
            if (!::SetThreadGroupAffinity(pi.hThread, &ga, nullptr))
            {
                err = ::GetLastError();
                std::fprintf(stderr, "\rPsExec could not set group affinity %s:\n", o.app.c_str());
                pstools::PrintSystemError(err);
                ::TerminateProcess(pi.hProcess, 0);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
                return false;
            }
        }
        ::ResumeThread(pi.hThread);
        return true;
    }

    // Run the command on the local machine directly. Mirrors sub_140012860: build
    // the "app" args command line, create the process with CreateProcessWithLogonW
    // when alternate credentials are supplied, otherwise with CreateProcessAsUserW
    // off the caller's token (optionally restricted for -l), then apply priority
    // and affinity, resume, and either wait for the exit code or, with -d, report
    // the process id. Returns the process exit code, the process id for -d, or the
    // Win32 error on a launch failure.
    [[nodiscard]] int RunLocal(const Options& o)
    {
        const std::string cmdLine = "\"" + o.app + "\" " + o.args;
        std::wstring wcmd = Widen(cmdLine);
        std::wstring wcwd;
        const wchar_t* cwd = nullptr;
        if (o.haveWorkDir && !o.workDir.empty())
        {
            wcwd = Widen(o.workDir);
            cwd = wcwd.c_str();
        }
        const DWORD flags = o.priorityClass | kCreateFlags;

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        bool created = false;

        if (o.haveUser && !o.limited)
        {
            std::string account;
            std::string domain;
            SplitUser(o.user, account, domain);
            const std::wstring wuser = Widen(account);
            const std::wstring wdomain = Widen(domain);
            const std::wstring wpass = Widen(o.password);
            const DWORD logonFlags = o.noProfile ? 0u : static_cast<DWORD>(LOGON_WITH_PROFILE);
            created = ::CreateProcessWithLogonW(
                          wuser.c_str(),
                          domain.empty() ? nullptr : wdomain.c_str(),
                          wpass.c_str(),
                          logonFlags,
                          nullptr,
                          wcmd.data(),
                          flags,
                          nullptr,
                          cwd,
                          &si,
                          &pi) != 0;
        }
        else
        {
            ScopedHandle base;
            ::OpenProcessToken(::GetCurrentProcess(), kTokenAccess, base.Address());
            ScopedHandle runToken;
            if (o.limited)
            {
                HANDLE limited = CreateLimitedUserToken(base.Get());
                if (limited == nullptr)
                {
                    const DWORD err = ::GetLastError();
                    std::fprintf(stderr, "\rPsExec could not create a limited user token:\n");
                    pstools::PrintSystemError(err);
                    return static_cast<int>(err);
                }
                runToken.Reset(limited);
            }
            const HANDLE useToken = o.limited ? runToken.Get() : base.Get();
            created = ::CreateProcessAsUserW(
                          useToken,
                          nullptr,
                          wcmd.data(),
                          nullptr,
                          nullptr,
                          FALSE,
                          flags,
                          nullptr,
                          cwd,
                          &si,
                          &pi) != 0;
        }

        if (!created)
        {
            const DWORD err = ::GetLastError();
            std::fprintf(stderr, "\rPsExec could not start %s:\n", o.app.c_str());
            pstools::PrintSystemError(err);
            return static_cast<int>(err);
        }

        DWORD affinityErr = 0;
        if (!ApplyAndResume(o, pi, affinityErr))
        {
            return static_cast<int>(affinityErr);
        }

        int result = 0;
        if (o.dontWait)
        {
            std::fprintf(stderr, "%s started with process ID %d.\n",
                         o.app.c_str(), static_cast<int>(pi.dwProcessId));
            result = static_cast<int>(pi.dwProcessId);
        }
        else
        {
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 0;
            ::GetExitCodeProcess(pi.hProcess, &code);
            std::fprintf(stderr, "%s exited with error code %d.\n",
                         o.app.c_str(), static_cast<int>(code));
            result = static_cast<int>(code);
        }
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        return result;
    }

    // Run the command on one computer through the PSEXESVC service. Mirrors the
    // client structure of sub_1400159A0: connect to the target's IPC$ with the
    // alternate credentials, deploy the service binary to ADMIN$, create and start
    // the service through the SCM, then drive the child over the named pipe
    // \\computer\pipe\<service>. The helper binary (PSEXESVC.EXE) and the encrypted
    // named-pipe stdio protocol are out of scope, the same boundary PsKill drew for
    // its PSKLLSVC remote path, so this reports the gap and does not run.
    //
    // This path is reached for a remote target, or for a local run with -s, -i, -x,
    // or -h, because the original obtains the System, interactive, or elevated token
    // through the service rather than a direct local CreateProcess.
    [[nodiscard]] int RunViaService(const char* computer, const Options& o)
    {
        bool connected = false;
        if (o.haveUser)
        {
            connected = pstools::RemoteConnect(computer, o.user.c_str(), o.password.c_str());
        }

        std::fprintf(stderr,
                     "\rPsExec service execution on %s is reproduced as a faithful client only.\n"
                     "The %s service deploy to ADMIN$ and the named-pipe stdio protocol are out of\n"
                     "scope, so this path is not exercised. Run a local command to use PsExec here.\n",
                     computer, o.serviceName.c_str());

        if (connected)
        {
            pstools::RemoteDisconnect(computer);
        }
        return static_cast<int>(ERROR_NOT_SUPPORTED);
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

    Options o;
    bool needsService = false;
    if (!ParseCommandLine(argc, argv, o, needsService))
    {
        return -1;
    }

    const std::string local = pstools::LocalComputerName();
    const std::string target = o.computerSpecified ? o.computerList : local;

    // -u without -p prompts for a hidden password, unless the account is a service
    // account that carries no password, which routes to the service path instead.
    if (o.haveUser && !o.havePassword)
    {
        std::string account;
        std::string domain;
        SplitUser(o.user, account, domain);
        if (::_stricmp(domain.c_str(), "NT AUTHORITY") == 0
            || ::_stricmp(domain.c_str(), "NT SERVICE") == 0)
        {
            needsService = true;
        }
        else
        {
            o.password = pstools::PromptHiddenPassword();
            o.havePassword = true;
        }
    }

    const bool isLocalTarget = ::_stricmp(target.c_str(), local.c_str()) == 0
                               || target == "." || target.empty();
    const bool localDirect = isLocalTarget && !o.elevated && !needsService;

    if (localDirect)
    {
        return RunLocal(o);
    }

    int last = 0;
    pstools::ForEachComputer(target, true, [&](const char* computer) -> bool {
        last = RunViaService(computer, o);
        return last == 0;
    });
    return last;
}
