// PsShutdown -- shuts down, reboots, logs off, locks, hibernates, suspends,
// powers off, or turns the monitor off on the local machine or a remote one,
// after an optional countdown and with an optional message. Source-accurate
// C++20 console reconstruction of the Sysinternals psshutdown64.exe v2.6.
//
// Behaviour recovered with IDA (idalib) from psshutdown64.exe:
//   main                 sub_140005EF0  (OS gate, front end, parse, iterate targets)
//   front end driver     sub_1400036C0  (version resource, banner, EULA)
//   usage + reason table sub_140005B30  (switch help plus User32 BuildReasonArray)
//   command parser       sub_140005500  (flags, -t, -e, -m, -u, -p, -n, -v, targets)
//   target iteration     sub_140003260  (single, comma list, @file, * domain)
//   per-target worker    sub_140004A90  (local immediate vs service or remote)
//   immediate action     sub_140004840  (ExitWindowsEx, Powrprof SetSuspendState)
//   monitor off          sub_140004820  (PostMessage SC_MONITORPOWER broadcast)
//   service client       sub_140004D20  (0x220-byte request over \pipe\psshutdownsvc)
//   enable a privilege   sub_140003180  (OpenProcessToken, AdjustTokenPrivileges)
//   clear console line   sub_140003100  (padding written to stderr)
//   print a system error sub_1400037F0  (FormatMessage, netmsg.dll for net codes)
//
// The shared PsTools front end (banner, EULA gate, alternate credentials, error
// text, local identity, hidden password prompt, target iteration) lives in
// ../common/PsTools.h and is reused read only. PsShutdown has its own argument
// grammar, so the parser here mirrors sub_140005500 rather than
// pstools::ParseCommandLine.
//
// Scope and safety: the local immediate actions (ExitWindowsEx, SetSuspendState,
// LockWorkStation, the monitor-off broadcast) are reproduced faithfully from the
// decompile for fidelity and are never driven during validation. The actual
// countdown, the on-screen notification dialog, and the system shutdown call
// (InitiateSystemShutdownEx) live in the bundled PSSDNSVC.EXE helper service, out
// of scope the same way PsSuspend leaves PSSPNDSVC. This reproduces the faithful
// client half of that service protocol and the abort path. The service deploy and
// teardown (sub_140003920 / sub_140003F70) are reproduced by inspection only.

#include "../common/PsTools.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
    // Version-resource fields of psshutdown64.exe v2.6, used for the banner and
    // the EULA registry key. The original reads these from its own version
    // resource at run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsShutdown",
        "2.6",
        "Shutdown, logoff and power manage local and remote systems",
        "Independent source-accurate recreation of PsShutdown by Sysinternals, through reverse engineering and rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name is the version-resource InternalName. The registry key is
    // case-insensitive, so this matches HKCU\Software\Sysinternals\PsShutdown.
    constexpr char kApp[] = "PsShutdown";

    // Action and modifier bits in the parser's flag word (dword_140049F98). The
    // nine action bits are mutually exclusive, force and allow-abort are modifiers.
    constexpr DWORD kLock = 0x001;       // -l  Lock the computer
    constexpr DWORD kReboot = 0x002;     // -r  Reboot after shutdown
    constexpr DWORD kLogoff = 0x004;     // -o  Logoff the console user
    constexpr DWORD kAbort = 0x008;      // -a  Abort a shutdown in progress
    constexpr DWORD kPoweroff = 0x010;   // -k  Poweroff the computer
    constexpr DWORD kShutdown = 0x020;   // -s  Shutdown without poweroff
    constexpr DWORD kForce = 0x040;      // -f  Force running applications to close
    constexpr DWORD kHibernate = 0x080;  // -h  Hibernate the computer
    constexpr DWORD kSuspend = 0x100;    // -d  Suspend the computer
    constexpr DWORD kMonitor = 0x200;    // -x  Turn monitor off

    // The exclusive action bits, counted at the end of parsing. Exactly one must be
    // set, otherwise the default is poweroff when none and usage when more than one.
    constexpr DWORD kActionMask =
        kLock | kReboot | kLogoff | kAbort | kPoweroff | kShutdown | kHibernate | kSuspend | kMonitor;

    // The actions handled without the shutdown service (lock, logoff, abort,
    // monitor off). For these a countdown, reason, or message is rejected.
    constexpr DWORD kNoServiceMask = kMonitor | kAbort | kLogoff | kLock;

    constexpr DWORD kDefaultCountdown = 20;
    constexpr DWORD kErrorSessionCredentialConflict = 1326;

    // SHTDN_REASON flags the parser folds into the reason word for -e.
    constexpr DWORD kReasonPlanned = 0x80000000;
    constexpr DWORD kReasonMajorBias = 0x400;

    // --- Parsed command line -----------------------------------------------

    struct Options
    {
        DWORD flags = 0;              // action and modifier bits
        DWORD reason = 0;             // -e folded reason code
        int countdown = 0;           // -t seconds, negative encodes an absolute time
        int displaySeconds = -1;     // -v notification seconds, -1 shows the dialog
        int connectTimeout = -1;     // -n remote connect timeout
        bool allowAbort = false;     // -c lets the interactive user abort
        bool haveCountdown = false;  // -t was given
        bool haveMessage = false;    // -m was given
        std::string message;         // -m value
        std::string computer;        // target list, empty means the local machine
        std::string user;            // -u value
        std::string password;        // -p value or prompted
        bool haveComputer = false;
        bool haveUser = false;
        bool havePassword = false;
    };

    // --- Usage with the live reason table ----------------------------------

    // User32 BuildReasonArray fills a pointer array of reason entries, each a DWORD
    // code followed by its title string. Recovered from sub_140005B30.
    struct ReasonList
    {
        int** reasons = nullptr;
        unsigned int count = 0;
    };
    using BuildReasonArrayFn = BOOL(__stdcall*)(ReasonList*, int, int);

    void PrintUsage()
    {
        std::printf("usage:\n");
        std::printf("psshutdown -s|-r|-h|-d|-k|-a|-l|-o|-x [-f] [-c] [-t [nn|h:m]] [-v nn] "
                    "[-e [u|p]:xx:yy] [-m \"message\"] [-u Username [-p password]] [-n s] "
                    "[\\\\computer[,computer[,...]|@file]\n");
        std::printf("   -a          Abort a shutdown (only possible while countdown is in progress)\n");
        std::printf("   -c          Allow the shutdown to be aborted by the interactive user\n");
        std::printf("   -d          Suspend the computer\n");
        std::printf("   -e          Shutdown reason code (available on Windows XP and higher).\n");
        std::printf("               Specify 'u' for unplanned and 'p' for planned\n");
        std::printf("               shutdown reason codes.\n");
        std::printf("               xx is the major reason code (must be less than 256)\n");
        std::printf("               yy is the minor reason code (must be less than 65536)\n");
        std::printf("   -f          Forces running applications to close\n");
        std::printf("   -h          Hibernate the computer\n");
        std::printf("   -k          Poweroff the computer (reboot if poweroff is not supported)\n");
        std::printf("   -l          Lock the computer\n");
        std::printf("   -m          Message to display to logged on users\n");
        std::printf("   -n          Specifies timeout in seconds connecting to remote computers\n");
        std::printf("   -o          Logoff the console user\n");
        std::printf("   -p          Specifies optional password for user name. If you omit this\n");
        std::printf("               you will be prompted to enter a hidden password.\n");
        std::printf("   -r          Reboot after shutdown\n");
        std::printf("   -s          Shutdown without poweroff\n");
        std::printf("   -t          Specifies countdown in seconds until shutdown (default is 20) or\n");
        std::printf("               the time of shutdown (in 24 hour notation)\n");
        std::printf("   -u          Specifies optional user name for login to remote\n               computer.\n");
        std::printf("   -v          Display message for the specified number of seconds before\n");
        std::printf("               the shutdown. If you omit this parameter the shutdown\n");
        std::printf("               notification dialog displays and specifying a value of 0\n");
        std::printf("               omits the dialog.\n");
        std::printf("   -x          Turn monitor off (system will initiate Modern Standby if supported)\n");
        std::printf("   -nobanner   Do not display the startup banner and copyright message.\n");
        std::printf("   computer    Shutdown the computer or computers specified\n");
        std::printf("   @file       Shutdown the computers listed in the file specified\n");
        std::printf("\n");

        const HMODULE user32 = ::GetModuleHandleA("User32.dll");
        const auto build = reinterpret_cast<BuildReasonArrayFn>(
            reinterpret_cast<void*>(::GetProcAddress(user32, "BuildReasonArray")));
        ReasonList list;
        if (build != nullptr && build(&list, 0, 0) != 0)
        {
            if (list.count == 0)
            {
                build(&list, 1, 1);
            }
            std::printf("\nReasons defined on this computer (U = unplanned, P = planned):\n");
            std::printf("Type   Major   Minor   Title\n");
            for (unsigned int i = 0; i < list.count; ++i)
            {
                const int* entry = list.reasons[i];
                if ((*entry & 0x8000000) == 0)
                {
                    // The title follows the code as a wide string, and the original
                    // prints it through a wide printf. Convert to the console code
                    // page so the bytes match the original's output.
                    const auto* title = reinterpret_cast<const wchar_t*>(entry + 1);
                    char titleA[512] = {};
                    ::WideCharToMultiByte(CP_ACP, 0, title, -1, titleA,
                                          static_cast<int>(std::size(titleA)), nullptr, nullptr);
                    std::printf("  %c     % 2d    % 4d     %s\n",
                                ((*entry >> 31) & 0xFFFFFFFB) + 85,
                                static_cast<unsigned char>((*entry >> 16) & 0xFF),
                                static_cast<unsigned short>(*entry),
                                titleA);
                }
            }
        }
        std::printf("\n");
    }

    // --- Command line parsing ----------------------------------------------

    // Fold a validated -e token (already uppercased, form "U:major:minor" or
    // "P:major:minor") into the reason word. Mirrors sub_140005500 case 'E'.
    [[nodiscard]] bool ParseReason(const char* token, DWORD& reason)
    {
        char type = 0;
        int major = 0;
        int minor = 0;
        if (std::sscanf(token, "%c:%d:%d", &type, &major, &minor) != 3)
        {
            return false;
        }
        const DWORD planned = (type == 'U') ? 0 : kReasonPlanned;
        reason = static_cast<DWORD>(minor) | planned
                 | ((static_cast<DWORD>(major) | kReasonMajorBias) << 16);
        return true;
    }

    // Convert a -t "h:m" token to the negative seconds encoding the original uses
    // for an absolute shutdown time. Mirrors the colon branch of case 'T'.
    [[nodiscard]] bool ParseClockCountdown(const char* token, int& countdown)
    {
        int hour = 0;
        int minute = 0;
        if (std::sscanf(token, "%d:%d", &hour, &minute) != 2)
        {
            return false;
        }
        SYSTEMTIME now = {};
        ::GetLocalTime(&now);
        if (hour < now.wHour || (hour == now.wHour && minute < now.wMinute))
        {
            hour += 24;
        }
        countdown = 60 * (-minute - 60 * hour);
        return true;
    }

    // Parse PsShutdown's own argument grammar, after -nobanner and -accepteula have
    // been stripped. Returns false when the caller should print usage. Mirrors
    // sub_140005500: a leading '-' or '/' introduces a run of switches, a single
    // '\\' prefixes a computer target, '@' names a response file, and value
    // switches (-e, -m, -n, -p, -t, -u, -v) consume the next argument.
    [[nodiscard]] bool Parse(int argc, char** argv, Options& o)
    {
        if (argc <= 1)
        {
            return false;
        }

        for (int i = 1; i < argc; ++i)
        {
            char* a = argv[i];
            if (a[0] != '-' && a[0] != '/')
            {
                if (a[0] == '\\')
                {
                    if (a[1] != '\\' || o.haveComputer)
                    {
                        return false;
                    }
                    o.computer = a + 2;
                }
                else if (a[0] == '@')
                {
                    if (o.haveComputer)
                    {
                        return false;
                    }
                    o.computer = a;  // keep the '@' so ForEachComputer sees it
                }
                else
                {
                    return false;
                }
                o.haveComputer = true;
                continue;
            }

            if (a[1] == '\0')
            {
                continue;  // a bare '-' is ignored, as in the original
            }

            bool consumedNext = false;
            for (int j = 1; a[j] != '\0' && !consumedNext; ++j)
            {
                const char c = static_cast<char>(::toupper(static_cast<unsigned char>(a[j])));
                const bool haveNext = (i + 1) < argc;
                switch (c)
                {
                case 'A':
                    if (o.flags & kAbort) { return false; }
                    o.flags |= kAbort;
                    break;
                case 'C':
                    o.allowAbort = true;
                    break;
                case 'D':
                    if (o.flags & kSuspend) { return false; }
                    o.flags |= kSuspend;
                    break;
                case 'F':
                    if (o.flags & kForce) { return false; }
                    o.flags |= kForce;
                    break;
                case 'H':
                    if (o.flags & kHibernate) { return false; }
                    o.flags |= kHibernate;
                    break;
                case 'K':
                    if (o.flags & kPoweroff) { return false; }
                    o.flags |= kPoweroff;
                    break;
                case 'L':
                    if (o.flags & kLock) { return false; }
                    o.flags |= kLock;
                    break;
                case 'O':
                    if (o.flags & kLogoff) { return false; }
                    o.flags |= kLogoff;
                    break;
                case 'R':
                    if (o.flags & kReboot) { return false; }
                    o.flags |= kReboot;
                    break;
                case 'S':
                    if (o.flags & kShutdown) { return false; }
                    o.flags |= kShutdown;
                    break;
                case 'X':
                    if (o.flags & kMonitor) { return false; }
                    o.flags |= kMonitor;
                    break;
                case 'E':
                {
                    if (!haveNext || o.reason != 0) { return false; }
                    char* token = argv[++i];
                    consumedNext = true;
                    ::_strupr(token);
                    if (token[0] != 'U' && token[0] != 'P') { return false; }
                    if (!ParseReason(token, o.reason)) { return false; }
                    break;
                }
                case 'M':
                    if (!haveNext || o.haveMessage) { return false; }
                    o.message = argv[++i];
                    o.haveMessage = true;
                    consumedNext = true;
                    break;
                case 'N':
                {
                    if (!haveNext || o.connectTimeout != -1) { return false; }
                    if (std::sscanf(argv[++i], "%d", &o.connectTimeout) == 0) { return false; }
                    consumedNext = true;
                    break;
                }
                case 'V':
                {
                    if (!haveNext || o.displaySeconds != -1) { return false; }
                    if (std::sscanf(argv[++i], "%d", &o.displaySeconds) == 0) { return false; }
                    consumedNext = true;
                    break;
                }
                case 'P':
                    if (!haveNext || o.havePassword) { return false; }
                    o.password = argv[++i];
                    o.havePassword = true;
                    consumedNext = true;
                    break;
                case 'U':
                    if (!haveNext || o.haveUser) { return false; }
                    o.user = argv[++i];
                    o.haveUser = true;
                    consumedNext = true;
                    break;
                case 'T':
                {
                    if (!haveNext) { return false; }
                    const char* token = argv[++i];
                    consumedNext = true;
                    if (std::strchr(token, ':') != nullptr)
                    {
                        if (!ParseClockCountdown(token, o.countdown)) { return false; }
                    }
                    else
                    {
                        o.countdown = std::atoi(token);
                    }
                    o.haveCountdown = true;
                    break;
                }
                default:
                    return false;
                }
            }
        }

        int actionCount = 0;
        for (DWORD bit = 1; bit <= kMonitor; bit <<= 1)
        {
            if ((bit & kActionMask) != 0 && (o.flags & bit) != 0)
            {
                ++actionCount;
            }
        }
        if (actionCount != 1)
        {
            if (actionCount != 0)
            {
                return false;
            }
            o.flags |= kPoweroff;  // no action named defaults to poweroff
        }

        if ((o.flags & kNoServiceMask) != 0)
        {
            if (o.reason != 0 || o.countdown != 0 || o.haveMessage
                || (o.flags & (kForce | kLogoff)) == kForce)
            {
                return false;  // force is only allowed with logoff in this group
            }
        }
        else if (!o.haveCountdown)
        {
            o.countdown = kDefaultCountdown;
        }
        return true;
    }

    // --- Console helpers ---------------------------------------------------

    // Blank the current console line so a \r-prefixed message overwrites the
    // progress text cleanly. Mirrors sub_140003100: the buffer width is queried
    // once and cached, the padding goes to stderr, and a redirected run leaves the
    // width at zero so nothing but the carriage returns is written. The query also
    // overwrites GetLastError, which the service-connect error paths read next.
    void ClearLine()
    {
        static CONSOLE_SCREEN_BUFFER_INFO info = {};
        if (info.dwSize.X == 0)
        {
            ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info);
        }
        std::fputc('\r', stderr);
        for (int i = 0; i < info.dwSize.X - 1; ++i)
        {
            std::fputc(' ', stderr);
        }
        std::fputc('\r', stderr);
    }

    // --- Privilege ---------------------------------------------------------

    // Enable one privilege on the current process token. Mirrors sub_140003180.
    [[nodiscard]] bool EnablePrivilege(const char* name)
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(),
                                TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        {
            return false;
        }
        LUID luid = {};
        bool ok = false;
        if (::LookupPrivilegeValueA(nullptr, name, &luid))
        {
            TOKEN_PRIVILEGES tp = {};
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            ok = ::AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) != 0
                 && ::GetLastError() == ERROR_SUCCESS;
        }
        ::CloseHandle(token);
        return ok;
    }

    // --- Immediate local actions -------------------------------------------
    //
    // These run only when the target is the local machine, there is no countdown,
    // and the action is not abort. They are the destructive calls the original
    // makes directly, reproduced from sub_140004840, sub_140004820, and the lock
    // branch of sub_140004A90. They are never driven during validation.

    using SetSuspendStateFn = BOOLEAN(__stdcall*)(BOOLEAN, BOOLEAN, BOOLEAN);
    using LockWorkStationFn = BOOL(__stdcall*)();

    // Perform the immediate shutdown, reboot, poweroff, hibernate, or suspend.
    // Mirrors sub_140004840: ExitWindowsEx carries the force bit shifted down from
    // the flag word, and hibernate or suspend go through Powrprof SetSuspendState.
    [[nodiscard]] bool ImmediateShutdown(DWORD flags, DWORD reason)
    {
        if (!EnablePrivilege("SeShutdownPrivilege"))
        {
            return false;
        }
        if (flags & kReboot)
        {
            return ::ExitWindowsEx(((flags & kForce) | 0x20) >> 4, reason) != 0;
        }
        if (flags & kPoweroff)
        {
            return ::ExitWindowsEx(((flags & kForce) | 0x80) >> 4, reason) != 0;
        }
        if ((flags & (kHibernate | kSuspend)) == 0)
        {
            return ::ExitWindowsEx(((flags & kForce) | 0x10) >> 4, reason) != 0;
        }
        const HMODULE powrprof = ::LoadLibraryA("Powrprof.dll");
        const auto setSuspendState = reinterpret_cast<SetSuspendStateFn>(
            reinterpret_cast<void*>(::GetProcAddress(powrprof, "SetSuspendState")));
        if (setSuspendState != nullptr)
        {
            return setSuspendState(static_cast<BOOLEAN>(flags & kHibernate), TRUE, FALSE) != 0;
        }
        return false;
    }

    // Turn the monitor off. Mirrors sub_140004820: broadcast WM_SYSCOMMAND with
    // SC_MONITORPOWER and the power-off argument.
    void MonitorOff()
    {
        ::PostMessageA(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
    }

    // Lock the console session. Mirrors the lock branch of sub_140004A90, which
    // resolves LockWorkStation dynamically so the message matches old systems.
    // Returns the worker result (true when the action completed or reported).
    [[nodiscard]] bool LocalLock()
    {
        const HMODULE user32 = ::GetModuleHandleA("User32.dll");
        const auto lockWorkStation = reinterpret_cast<LockWorkStationFn>(
            reinterpret_cast<void*>(::GetProcAddress(user32, "LockWorkStation")));
        if (lockWorkStation == nullptr)
        {
            std::fprintf(stderr, "Locking requires Windows 2000 or higher.\n\n");
            return false;
        }
        if (lockWorkStation())
        {
            std::printf("System locked.\n\n");
            return true;
        }
        std::fprintf(stderr, "Error locking system: ");
        pstools::PrintSystemError(::GetLastError());
        std::fprintf(stderr, "\n\n");
        return false;
    }

    // --- Service client ----------------------------------------------------

    // The 0x220-byte request and response the client exchanges with the PsShutdown
    // helper over its named pipe. Recovered from sub_140004D20: the flag word, the
    // allow-abort byte, the message, the countdown in seconds, the requesting
    // user, the reason, the notification seconds, and on the way back a status.
#pragma pack(push, 1)
    struct ShutdownPacket
    {
        DWORD flags;            // 0x000
        BYTE allowAbort;        // 0x004
        char message[263];      // 0x005
        DWORD countdown;        // 0x10C
        char user[260];         // 0x110
        DWORD reason;           // 0x214
        DWORD displaySeconds;   // 0x218
        DWORD status;           // 0x21C
    };
#pragma pack(pop)
    static_assert(sizeof(ShutdownPacket) == 0x220, "psshutdownsvc packet must be 544 bytes");

    // Verb for the "Error %s %s:" line when the service returns a nonzero status.
    [[nodiscard]] const char* ErrorVerb(DWORD flags)
    {
        if (flags & kLock) { return "locking"; }
        if (flags & kReboot) { return "rebooting"; }
        if (flags & kHibernate) { return "hibernating"; }
        if (flags & kSuspend) { return "suspending"; }
        if (flags & kPoweroff) { return "powering off"; }
        return "shutting down";
    }

    // Verb for the "scheduled to %s" success line.
    [[nodiscard]] const char* ScheduleVerb(DWORD flags)
    {
        if (flags & kReboot) { return "reboot"; }
        if (flags & kSuspend) { return "suspend"; }
        if (flags & kHibernate) { return "hibernate"; }
        if (flags & kPoweroff) { return "power off"; }
        if (flags & kMonitor) { return "turn monitor off"; }
        return "shut down";
    }

    // Fill the request's requesting-user field with DOMAIN\user from the process
    // token, defaulting to "<Unknown>". Mirrors the token lookup in sub_140004D20.
    void FillRequestingUser(char* out, size_t outSize)
    {
        std::snprintf(out, outSize, "<Unknown>");
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return;
        }
        BYTE buffer[0x800] = {};
        DWORD returned = sizeof(buffer);
        char name[260] = {};
        char domain[260] = {};
        DWORD cchName = static_cast<DWORD>(std::size(name));
        DWORD cchDomain = static_cast<DWORD>(std::size(domain));
        SID_NAME_USE use = SidTypeUnknown;
        auto* info = reinterpret_cast<TOKEN_USER*>(buffer);
        if (::GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &returned)
            && ::LookupAccountSidA(nullptr, info->User.Sid, name, &cchName, domain, &cchDomain, &use))
        {
            std::snprintf(out, outSize, "%s\\%s", domain, name);
        }
        ::CloseHandle(token);
    }

    // Reproduce the PsShutdown client half of the service protocol. Mirrors
    // sub_140004D20: connect to the target's \pipe\psshutdownsvc, send the 0x220
    // request, read the response, and print the same progress, error, and success
    // messages. Returns the worker result as the original does, zero on success.
    //
    // ponytail: the original first deploys the bundled PSSDNSVC.EXE to the target's
    // ADMIN$ and starts it through the SCM (sub_140003920), then tears it down
    // (sub_140003F70), and the service itself runs the countdown, the notification
    // dialog, and InitiateSystemShutdownEx. That helper executable and its service
    // lifecycle are out of scope, the same call PsSuspend makes for PSSPNDSVC. This
    // reproduces the faithful client protocol and every message and error path that
    // runs when the service is reached, or not reached, on the target. Because no
    // service binary is deployed, this client never schedules a real shutdown.
    [[nodiscard]] int ServiceClient(const Options& o, const std::string& target,
                                    const std::string& localName)
    {
        const bool isLocal = ::_stricmp(target.c_str(), localName.c_str()) == 0;
        const char* const display = isLocal ? "local system" : target.c_str();

        ClearLine();
        std::fprintf(stderr, "\rConnecting with psshutdown service on %s...", display);

        char pipe[MAX_PATH] = {};
        std::snprintf(pipe, std::size(pipe), "\\\\%s\\pipe\\psshutdownsvc", target.c_str());
        const HANDLE h = ::CreateFileA(pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            ClearLine();
            std::fprintf(stderr,
                         "\rError establishing communication with psshutdown service on %s:\n",
                         display);
            pstools::PrintSystemError(::GetLastError());
            std::printf("\n");
            return 1;
        }

        ShutdownPacket packet = {};
        packet.flags = o.flags;
        packet.allowAbort = o.allowAbort ? 1 : 0;
        const char* const messageSource = o.haveMessage ? o.message.c_str() : "";
        std::strncpy(packet.message, messageSource, sizeof(packet.message) - 1);
        if (o.countdown >= 0)
        {
            packet.countdown = static_cast<DWORD>(o.countdown);
        }
        else
        {
            SYSTEMTIME now = {};
            ::GetLocalTime(&now);
            packet.countdown = static_cast<DWORD>(-o.countdown - 60 * (now.wMinute + 60 * now.wHour));
        }
        packet.reason = o.reason;
        packet.displaySeconds = static_cast<DWORD>(o.displaySeconds);
        FillRequestingUser(packet.user, sizeof(packet.user));

        DWORD moved = 0;
        if (!::WriteFile(h, &packet, sizeof(packet), &moved, nullptr))
        {
            ClearLine();
            std::fprintf(stderr, "\rError communicating with psshutdown service on %s:\n", display);
            pstools::PrintSystemError(::GetLastError());
            std::printf("\n");
            ::CloseHandle(h);
            return 1;
        }
        if (!::ReadFile(h, &packet, sizeof(packet), &moved, nullptr))
        {
            const DWORD err = ::GetLastError();
            ClearLine();
            std::fprintf(stderr,
                         "\rError communicating with psshutdown service on %s. The system may\n"
                         "not have been %s:\n",
                         display, (o.flags & kLock) ? "locked" : "shutdown");
            pstools::PrintSystemError(err);
            std::printf("\n");
            ::CloseHandle(h);
            return 1;
        }
        ::CloseHandle(h);

        if (packet.status != 0)
        {
            ClearLine();
            std::fprintf(stderr, "\rError %s %s:\n", ErrorVerb(o.flags), display);
            if ((o.flags & kLock) && packet.status == 1)
            {
                std::fprintf(stderr, "%s does not support the LockWorkStation API.\n\n",
                             isLocal ? "The local system" : target.c_str());
            }
            else if ((o.flags & (kHibernate | kSuspend)) && packet.status == 1)
            {
                std::fprintf(stderr, "%s does not support %s.\n\n",
                             isLocal ? "The local system" : target.c_str(),
                             (o.flags & kSuspend) ? "suspend" : "hibernation");
            }
            else
            {
                pstools::PrintSystemError(packet.status);
                std::printf("\n");
            }
            return 1;
        }

        if (o.flags & kAbort)
        {
            std::printf("Shutdown of %s aborted.\n\n", display);
        }
        else if (o.flags & kLock)
        {
            std::printf("%s locked.\n\n", isLocal ? "The local system is" : target.c_str());
        }
        else if (o.flags & kLogoff)
        {
            std::printf("Console logoff initiated on %s.\n\n", display);
        }
        else
        {
            const DWORD seconds = packet.countdown % 60;
            const DWORD minutes = (packet.countdown % 3600) / 60;
            const DWORD hours = packet.countdown / 3600;
            std::printf("%s is scheduled to %s in %02d:%02d:%02d.\n\n",
                        isLocal ? "Local system" : target.c_str(), ScheduleVerb(o.flags),
                        hours, minutes, seconds);
        }
        return 0;
    }

    [[nodiscard]] std::string Upper(const std::string& s)
    {
        std::string out = s;
        ::_strupr(out.data());
        return out;
    }

    // Per-target worker. Mirrors sub_140004A90: a local target with no countdown
    // and no abort runs the immediate action directly, anything else (a remote
    // target, an alternate user, a countdown, or an abort) goes through the
    // service client. Returns the original's worker result, zero on success.
    [[nodiscard]] int Worker(const Options& o, const std::string& localName, const char* computer)
    {
        const bool local = ::_stricmp(computer, localName.c_str()) == 0 && !o.haveUser;
        if (!local)
        {
            return ServiceClient(o, Upper(computer), localName);
        }

        if ((o.flags & kAbort) == 0 && o.countdown == 0)
        {
            if (o.flags & kLock)
            {
                return LocalLock() ? 0 : 1;
            }
            if (o.flags & kMonitor)
            {
                MonitorOff();
                return 0;
            }
            if (ImmediateShutdown(o.flags, o.reason))
            {
                return 0;
            }
            std::fprintf(stderr, "Error shutting down system: ");
            pstools::PrintSystemError(::GetLastError());
            std::fprintf(stderr, "\n\n");
            return 1;
        }

        if (!EnablePrivilege("SeShutdownPrivilege"))
        {
            std::fprintf(stderr, "Error shutting down system: ");
            pstools::PrintSystemError(::GetLastError());
            std::fprintf(stderr, "\n\n");
            return 1;
        }
        return ServiceClient(o, Upper(localName), localName);
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

    // The original gates on Windows XP or higher through VerifyVersionInfoW and
    // prints "PsShutdown requires Windows XP or higher." when it fails. That check
    // is always true on supported systems, so it is dropped here, the same call
    // PsSuspend makes.

    Options o;
    if (!Parse(argc, argv, o))
    {
        PrintUsage();
        return -1;
    }

    // -u without -p prompts for a hidden password, as the original does before it
    // reaches the credentialed remote connect.
    if (o.haveUser && !o.havePassword)
    {
        o.password = pstools::PromptHiddenPassword();
        o.havePassword = true;
    }

    const std::string localName = pstools::LocalComputerName();
    if (!o.haveComputer)
    {
        o.computer = localName;
    }

    const bool ok = pstools::ForEachComputer(o.computer, true, [&](const char* computer)
    {
        return Worker(o, localName, computer) == 0;
    });
    return ok ? 0 : 1;
}
