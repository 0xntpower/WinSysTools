// PsSuspend -- suspends or resumes a process by PID or name, on the local
// machine or a remote one. Source-accurate C++20 console reconstruction of the
// Sysinternals pssuspend64.exe v1.08.
//
// Behaviour recovered with IDA (idalib) from pssuspend64.exe:
//   main                 sub_140005B30
//   front end driver     sub_140003490  (banner, EULA, run before the OS check)
//   banner + nobanner    sub_1400029D0 / sub_140002C80
//   EULA gate            sub_140001B80 / sub_1400020D0 / sub_1400022F0
//   command parser       sub_140004FD0  (-r, \\computer, -u, -p, pid or name)
//   usage                sub_140005960
//   local worker         sub_1400045F0  (resolve ntdll, enumerate, act)
//   act on one process   sub_1400048D0  (OpenProcess 0x800, NtSuspend/Resume)
//   remote worker        sub_1400052B0  (PSSPNDSVC over \pipe\psspndsvc)
//   enable a privilege   sub_140003180  (OpenProcessToken, AdjustTokenPrivileges)
//   print a system error sub_1400035C0  (FormatMessage, netmsg.dll for net codes)
//
// The shared PsTools front end (banner, EULA gate, alternate credentials, error
// text, local identity, hidden password prompt) lives in ../common/PsTools.h and
// is reused read only. PsSuspend has its own argument grammar, so the parser here
// mirrors sub_140004FD0 rather than pstools::ParseCommandLine. SeDebugPrivilege
// enabling, the ntdll resolver, ClearLine, and the remote packet layout are not
// in the shared header and are implemented locally.

#include "../common/PsTools.h"

#include <windows.h>
#include <tlhelp32.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    // Version-resource fields of pssuspend64.exe v1.08, used for the banner and
    // the EULA registry key. The original reads these from its own version
    // resource at run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsSuspend",
        "1.08",
        "Process Suspender",
        "Independent source-accurate recreation of PsSuspend by Sysinternals, through reverse engineering and rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name is the version-resource InternalName. The registry key is
    // case-insensitive, so this matches HKCU\Software\Sysinternals\PsSuspend.
    constexpr char kApp[] = "PsSuspend";

    // PROCESS_SUSPEND_RESUME, the only access the Nt suspend and resume calls need.
    constexpr DWORD kProcessSuspendResume = 0x0800;
    constexpr DWORD kErrorInvalidParameter = 87;

    // --- Parsed command line -----------------------------------------------

    struct Options
    {
        bool resume = false;          // -r, otherwise suspend
        bool byName = false;          // a process name was given rather than a PID
        std::string name;             // process name when byName
        int pid = 0;                  // process id when not byName
        std::string computer;         // \\computer, empty for local
        std::string user;             // -u value
        std::string password;         // -p value or prompted
        bool haveProcess = false;
        bool haveComputer = false;
        bool haveUser = false;
        bool havePassword = false;
    };

    void PrintUsage()
    {
        std::printf("PsSuspend suspends or resumes processes on a local or remote NT system.\n");
        std::printf("\nUsage: pssuspend [-r] [\\\\RemoteComputer [-u Username [-p Password]]] <process Id or name>\n");
        std::printf("     -r    Resume.\n");
        std::printf("     -u    Specifies optional user name for login to\n           remote computer.\n");
        std::printf("     -p    Specifies optional password for user name. If you omit this\n");
        std::printf("           you will be prompted to enter a hidden password.\n");
        std::printf("     -nobanner Do not display the startup banner and copyright message.\n");
        std::printf("\n");
    }

    [[nodiscard]] bool AllDigits(const char* s)
    {
        if (*s == '\0')
        {
            return false;
        }
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (::isdigit(static_cast<unsigned char>(*p)) == 0)
            {
                return false;
            }
        }
        return true;
    }

    // Parse PsSuspend's own argument grammar, after -nobanner and -accepteula have
    // been stripped. Returns false when the caller should print usage. Mirrors
    // sub_140004FD0: a leading '-' or '/' is a switch (-r, -u value, -p value), a
    // '\\' prefixes a computer, an all-digit token is a PID, anything else is a
    // process name. -u needs a computer target and -p needs -u.
    [[nodiscard]] bool Parse(int argc, char** argv, Options& o)
    {
        for (int i = 1; i < argc; ++i)
        {
            const char* a = argv[i];
            if (a[0] == '-' || a[0] == '/')
            {
                if (a[1] == '\0')
                {
                    continue;  // a bare '-' is ignored, as in the original
                }
                for (int j = 1; a[j] != '\0'; ++j)
                {
                    const char c = static_cast<char>(::tolower(static_cast<unsigned char>(a[j])));
                    if (c == 'r')
                    {
                        o.resume = true;
                        continue;
                    }
                    if (c == 'u')
                    {
                        if (i + 1 >= argc || o.haveUser)
                        {
                            return false;
                        }
                        o.user = argv[++i];
                        o.haveUser = true;
                        break;
                    }
                    if (c == 'p')
                    {
                        if (i + 1 >= argc || o.havePassword)
                        {
                            return false;
                        }
                        o.password = argv[++i];
                        o.havePassword = true;
                        break;
                    }
                    return false;
                }
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                if (o.haveComputer)
                {
                    return false;
                }
                o.computer = a + 2;
                o.haveComputer = true;
            }
            else if (AllDigits(a))
            {
                if (o.haveProcess)
                {
                    return false;
                }
                int value = 0;
                std::from_chars(a, a + std::strlen(a), value);
                o.pid = value;
                o.byName = false;
                o.haveProcess = true;
            }
            else
            {
                if (o.haveProcess)
                {
                    return false;
                }
                o.name = a;
                o.byName = true;
                o.haveProcess = true;
            }
        }

        if (!o.haveProcess || (o.haveUser && !o.haveComputer) || (o.havePassword && !o.haveUser))
        {
            return false;
        }
        return true;
    }

    // --- Console helpers ---------------------------------------------------

    // Blank the current console line so a \r-prefixed message overwrites the
    // progress text cleanly. Mirrors sub_140003100.
    void ClearLine()
    {
        CONSOLE_SCREEN_BUFFER_INFO info = {};
        if (!::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info))
        {
            return;
        }
        std::fputc('\r', stdout);
        for (int i = 0; i < info.dwSize.X - 1; ++i)
        {
            std::fputc(' ', stdout);
        }
        std::fputc('\r', stdout);
    }

    // --- Privilege ---------------------------------------------------------

    // Enable one privilege on the current process token. Mirrors sub_140003180.
    bool EnablePrivilege(const char* name)
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

    // --- Local suspend and resume through ntdll ----------------------------

    using NtProcessFn = LONG(NTAPI*)(HANDLE);

    struct NtProc
    {
        NtProcessFn suspend = nullptr;
        NtProcessFn resume = nullptr;
    };

    [[nodiscard]] NtProc ResolveNtProc()
    {
        NtProc fns;
        const HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
        if (ntdll != nullptr)
        {
            fns.suspend = reinterpret_cast<NtProcessFn>(
                reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtSuspendProcess")));
            fns.resume = reinterpret_cast<NtProcessFn>(
                reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtResumeProcess")));
        }
        return fns;
    }

    // Open one process and apply the suspend or resume. Mirrors the modern branch
    // of sub_1400048D0. Returns 0 on success, otherwise a Windows error (from
    // OpenProcess) or the Nt status the call returned.
    [[nodiscard]] DWORD ActOnPid(const NtProc& fns, bool suspend, DWORD pid)
    {
        const HANDLE h = ::OpenProcess(kProcessSuspendResume, FALSE, pid);
        if (h == nullptr)
        {
            return ::GetLastError();
        }
        const NtProcessFn fn = suspend ? fns.suspend : fns.resume;
        const LONG status = (fn != nullptr) ? fn(h) : -1;
        ::CloseHandle(h);
        return static_cast<DWORD>(status);
    }

    // A process name matches when it equals the target up to a '.' or the end, so
    // "notepad" matches "notepad.exe". Mirrors the strnicmp plus '.'/'\0' test in
    // sub_1400045F0.
    [[nodiscard]] bool NameMatches(const char* image, const char* target)
    {
        const size_t n = std::strlen(target);
        if (::_strnicmp(image, target, n) != 0)
        {
            return false;
        }
        const char after = image[n];
        return after == '.' || after == '\0';
    }

    // Run the suspend or resume locally. Mirrors sub_1400045F0. Returns 0 on
    // success with the number of affected processes in count, otherwise the last
    // Windows error (ERROR_INVALID_PARAMETER when a name matched nothing).
    [[nodiscard]] DWORD LocalAct(const Options& o, int& count)
    {
        EnablePrivilege("SeDebugPrivilege");
        count = 0;
        const NtProc fns = ResolveNtProc();
        const bool suspend = !o.resume;

        if (!o.byName)
        {
            const DWORD err = ActOnPid(fns, suspend, static_cast<DWORD>(o.pid));
            if (err == 0)
            {
                count = 1;
                return 0;
            }
            return err;
        }

        const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return ::GetLastError();
        }
        DWORD last = kErrorInvalidParameter;
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snapshot, &entry))
        {
            do
            {
                char image[MAX_PATH] = {};
                ::WideCharToMultiByte(CP_ACP, 0, entry.szExeFile, -1, image,
                                      static_cast<int>(std::size(image)), nullptr, nullptr);
                if (NameMatches(image, o.name.c_str()))
                {
                    const DWORD err = ActOnPid(fns, suspend, entry.th32ProcessID);
                    if (err == 0)
                    {
                        ++count;
                    }
                    else
                    {
                        last = err;
                    }
                }
            }
            while (::Process32NextW(snapshot, &entry));
        }
        ::CloseHandle(snapshot);
        return count > 0 ? 0 : last;
    }

    // --- Remote suspend and resume through the PSSPNDSVC service ------------

    // The 276-byte request and response the client exchanges with the remote
    // PSSPNDSVC helper over its named pipe. Recovered from sub_1400052B0: the
    // suspend flag and a "by name" flag, then either the name or the PID, and on
    // the way back a status code and the affected-process count.
#pragma pack(push, 1)
    struct SuspendPacket
    {
        unsigned char suspend;   // 0x000
        unsigned char byName;    // 0x001
        char name[262];          // 0x002
        DWORD pid;               // 0x108
        DWORD status;            // 0x10C
        DWORD count;             // 0x110
    };
#pragma pack(pop)
    static_assert(sizeof(SuspendPacket) == 0x114, "PSSPNDSVC packet must be 276 bytes");

    void PrintRemoteError(const Options& o, const char* verbIng)
    {
        ClearLine();
        if (o.byName)
        {
            std::printf("\rError %s process(es) named %s on %s:\n", verbIng, o.name.c_str(),
                        o.computer.c_str());
        }
        else
        {
            std::printf("\rError %s process %d on %s:\n", verbIng, o.pid, o.computer.c_str());
        }
    }

    // Reproduce the PsSuspend client half of the remote protocol. Mirrors
    // sub_1400052B0: connect to the service's \pipe\psspndsvc, send the request,
    // read the response, and print the same progress and error messages. Returns
    // true with the count set on success.
    //
    // ponytail: the original first deploys the bundled PSSPNDSVC.EXE to the
    // remote ADMIN$ and starts it through the SCM (sub_1400036F0) before talking
    // to the pipe, and tears it down afterward (sub_140003D40). That helper
    // executable and its service lifecycle are out of scope, so this reproduces
    // the faithful client protocol and the exact message and error paths that run
    // when the service is reached, or not reached, on the target.
    [[nodiscard]] bool RemoteAct(const Options& o, int& count)
    {
        const bool suspend = !o.resume;
        bool connected = false;
        if (o.haveUser)
        {
            connected = pstools::RemoteConnect(o.computer.c_str(), o.user.c_str(),
                                               o.password.c_str());
        }

        ClearLine();
        std::printf("\rConnecting with PsSuspend service on %s...", o.computer.c_str());

        char pipe[MAX_PATH] = {};
        std::snprintf(pipe, std::size(pipe), "\\\\%s\\pipe\\psspndsvc", o.computer.c_str());
        const HANDLE h = ::CreateFileA(pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            const DWORD err = ::GetLastError();
            ClearLine();
            std::printf("\rError establishing communication with PsSuspend service on %s:\n",
                        o.computer.c_str());
            pstools::PrintSystemError(err);
            std::printf("\n");
            if (connected)
            {
                pstools::RemoteDisconnect(o.computer.c_str());
            }
            return false;
        }

        ClearLine();
        std::printf("\r%s process %d on %s...", suspend ? "Suspending" : "Resuming", o.pid,
                    o.computer.c_str());

        SuspendPacket packet = {};
        packet.suspend = suspend ? 1 : 0;
        packet.byName = o.byName ? 1 : 0;
        if (o.byName)
        {
            std::strncpy(packet.name, o.name.c_str(), sizeof(packet.name) - 1);
        }
        else
        {
            packet.pid = static_cast<DWORD>(o.pid);
        }

        bool ok = false;
        DWORD moved = 0;
        if (!::WriteFile(h, &packet, sizeof(packet), &moved, nullptr))
        {
            const DWORD err = ::GetLastError();
            ClearLine();
            std::printf("\rError communicating with PsSuspend service on %s:\n", o.computer.c_str());
            pstools::PrintSystemError(err);
            std::printf("\n");
        }
        else if (!::ReadFile(h, &packet, sizeof(packet), &moved, nullptr))
        {
            const DWORD err = ::GetLastError();
            ClearLine();
            std::printf("\rError communicating with PsSuspend service on %s. The process may\n"
                        "not have been %s:\n",
                        o.computer.c_str(), suspend ? "suspended" : "resumed");
            pstools::PrintSystemError(err);
            std::printf("\n");
        }
        else if (packet.status != 0)
        {
            PrintRemoteError(o, suspend ? "suspending" : "resuming");
            if (packet.status == kErrorInvalidParameter)
            {
                if (o.byName)
                {
                    std::printf("Process %s does not exist on %s.\n", o.name.c_str(),
                                o.computer.c_str());
                }
                else
                {
                    std::printf("Process %d does not exist on %s.\n", o.pid, o.computer.c_str());
                }
            }
            else
            {
                pstools::PrintSystemError(packet.status);
            }
            std::printf("\n");
        }
        else
        {
            count = static_cast<int>(packet.count);
            ok = true;
        }

        ::CloseHandle(h);
        if (connected)
        {
            pstools::RemoteDisconnect(o.computer.c_str());
        }
        return ok;
    }

    // --- Result reporting --------------------------------------------------

    void ReportLocalSuccess(const Options& o, int count)
    {
        const char* const word = o.resume ? "resumed" : "suspended";
        if (count > 1)
        {
            std::printf("%d processes named %s %s.\n\n", count, o.name.c_str(), word);
        }
        else if (o.byName)
        {
            std::printf("Process %s %s.\n\n", o.name.c_str(), word);
        }
        else
        {
            std::printf("Process %d %s.\n\n", o.pid, word);
        }
    }

    [[nodiscard]] int ReportLocalFailure(const Options& o, DWORD err)
    {
        const char* const verb = o.resume ? "resume" : "suspend";
        if (o.byName)
        {
            std::printf("Unable to %s process %s:\n", verb, o.name.c_str());
        }
        else
        {
            std::printf("Unable to %s process %d:\n", verb, o.pid);
        }
        if (err == kErrorInvalidParameter)
        {
            std::printf("Process does not exist.\n");
            return -1;
        }
        pstools::PrintSystemError(err);
        return -1;
    }

    void ReportRemoteSuccess(const Options& o, int count)
    {
        const char* const word = o.resume ? "resumed" : "suspended";
        if (count > 1)
        {
            std::printf("\r%d processes named %s %s on %s.\n\n", count, o.name.c_str(), word,
                        o.computer.c_str());
        }
        else if (o.byName)
        {
            std::printf("\rProcess %s %s on %s.\n\n", o.name.c_str(), word, o.computer.c_str());
        }
        else
        {
            std::printf("\rProcess %d on %s %s.\n\n", o.pid, o.computer.c_str(), word);
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

    Options o;
    if (!Parse(argc, argv, o))
    {
        PrintUsage();
        return -1;
    }

    // A remote target that is really the local machine runs the local path, the
    // same test sub_140005B30 makes before choosing the service worker.
    const std::string local = pstools::LocalComputerName();
    const bool isRemote = o.haveComputer && ::_stricmp(o.computer.c_str(), local.c_str()) != 0;

    // -u without -p prompts for a hidden password, as the original does.
    if (o.haveUser && !o.havePassword)
    {
        o.password = pstools::PromptHiddenPassword();
        o.havePassword = true;
    }

    if (isRemote)
    {
        int count = 0;
        if (!RemoteAct(o, count))
        {
            return -1;
        }
        ReportRemoteSuccess(o, count);
        return 0;
    }

    int count = 0;
    const DWORD err = LocalAct(o, count);
    if (err == 0)
    {
        ReportLocalSuccess(o, count);
        return 0;
    }
    return ReportLocalFailure(o, err);
}
