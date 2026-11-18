// PsKill -- terminates a process by PID or by name, on the local machine or on
// a remote computer. Source-accurate C++20 console reconstruction of the
// Sysinternals pskill64.exe v1.17.
//
// Behaviour recovered with IDA (idalib) from pskill64.exe:
//   main                 sub_1400053E0
//   banner + EULA driver sub_140003490  (PrintBanner + EULA gate, run first)
//   command-line parse   sub_140004B20
//   usage                sub_140005220
//   local kill driver    sub_1400046A0  (by name or by PID, optional tree walk)
//   kill one PID         sub_140004A10  (OpenProcess + TerminateProcess)
//   process snapshot     sub_1400045F0  (NtQuerySystemInformation, class 5)
//   enable a privilege   sub_140003180  (SeDebugPrivilege)
//   remote kill          sub_140004E00  (PSKLLSVC service over a named pipe)
//   clear console line   sub_140003100
//   print system error   sub_1400035C0  (FormatMessage, netmsg.dll for net codes)
//
// The shared PsTools front end (banner, EULA gate, alternate credentials, error
// reporting, local identity, hidden password prompt) lives in ../common/PsTools.h.
// PsKill parses its own command line rather than the shared target-list parser,
// because its grammar is a single optional computer plus one PID-or-name target,
// not the comma or response-file lists the rest of the suite accept.
//
// The local snapshot uses NtQuerySystemInformation(SystemProcessInformation),
// the same source the original reads, so the PID, parent PID, creation time, and
// image name all carry the fields the tree walk needs, including the creation
// time guard that keeps a recycled PID from being mistaken for a descendant.

#include "../common/PsTools.h"

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace
{
    // Version-resource fields of pskill64.exe v1.17, used for the banner and the
    // EULA registry key. The original reads these from its own version resource
    // at run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsKill",
        "1.17",
        "Terminates processes on local or remote systems",
        "Independent source-accurate recreation of PsKill by Sysinternals, through reverse engineering and rewritten in modern C++20.",
        "Author: 0xntpower",
    };

    // The EULA app name is the version-resource InternalName. The registry key
    // is case-insensitive, so this matches HKCU\Software\Sysinternals\PsKill.
    constexpr char kApp[] = "PsKill";

    // OpenProcess rights the original requests: PROCESS_TERMINATE | SYNCHRONIZE.
    constexpr DWORD kKillAccess = PROCESS_TERMINATE | SYNCHRONIZE;

    // The original's sentinel for "the process was already signalled, so there
    // was nothing to terminate" (WaitForSingleObject returned other than
    // WAIT_TIMEOUT). Code 758 is reported through the usual error path.
    constexpr DWORD kAlreadyExited = 758;

    // NtQuerySystemInformation information class for the process list.
    constexpr ULONG kSystemProcessInformation = 5;

    // The named-pipe request and response packet is a fixed 0x118 bytes.
    constexpr DWORD kRemotePacketSize = 0x118;

    using NtQuerySystemInformationFn = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);

    // windows.h does not declare UNICODE_STRING, and winternl.h would collide with
    // the struct below, so the counted string is spelled out here. On x64 the
    // 4-byte gap after MaximumLength puts Buffer at offset 8, matching the OS.
    struct CountedUnicodeString
    {
        USHORT Length = 0;
        USHORT MaximumLength = 0;
        PWSTR Buffer = nullptr;
    };

    // Layout of SYSTEM_PROCESS_INFORMATION on x64. Only the fields the tool reads
    // are named, the rest are padding chosen so the named offsets land exactly
    // where the original indexes them (PID at 0x50, parent at 0x58, creation time
    // at 0x20, image-name UNICODE_STRING at 0x38).
    struct SystemProcessInformation
    {
        ULONG nextEntryOffset;              // 0x00
        ULONG numberOfThreads;              // 0x04
        LARGE_INTEGER workingSetPrivate;    // 0x08
        ULONG hardFaultCount;               // 0x10
        ULONG numberOfThreadsHighWater;     // 0x14
        ULONGLONG cycleTime;                // 0x18
        LARGE_INTEGER createTime;           // 0x20
        LARGE_INTEGER userTime;             // 0x28
        LARGE_INTEGER kernelTime;           // 0x30
        CountedUnicodeString imageName;     // 0x38
        LONG basePriority;                  // 0x48
        ULONG_PTR uniqueProcessId;          // 0x50
        ULONG_PTR inheritedFromProcessId;   // 0x58
    };

    static_assert(offsetof(SystemProcessInformation, createTime) == 0x20);
    static_assert(offsetof(SystemProcessInformation, imageName) == 0x38);
    static_assert(offsetof(SystemProcessInformation, uniqueProcessId) == 0x50);
    static_assert(offsetof(SystemProcessInformation, inheritedFromProcessId) == 0x58);

    // One row of the process snapshot in the form the tool actually uses.
    struct ProcessRecord
    {
        DWORD pid = 0;
        DWORD parentPid = 0;
        long long createTime = 0;
        std::string name;
    };

    // The 0x118-byte request and response exchanged with the PSKLLSVC service.
    // The request sets isName, then either name or pid, plus the tree flag. The
    // service fills status and count on the way back.
#pragma pack(push, 1)
    struct RemotePacket
    {
        BYTE isName = 0;        // 0x000
        char name[263] = {};    // 0x001
        DWORD pid = 0;          // 0x108
        BYTE tree = 0;          // 0x10C
        BYTE reserved[3] = {};  // 0x10D
        DWORD status = 0;       // 0x110
        DWORD count = 0;        // 0x114
    };
#pragma pack(pop)

    static_assert(sizeof(RemotePacket) == kRemotePacketSize);

    // A process handle that always closes. OpenProcess returns nullptr, not
    // INVALID_HANDLE_VALUE, on failure.
    class ProcessHandle
    {
    public:
        explicit ProcessHandle(HANDLE h)
            : handle_(h)
        {
        }

        ~ProcessHandle()
        {
            if (handle_ != nullptr)
            {
                ::CloseHandle(handle_);
            }
        }

        ProcessHandle(const ProcessHandle&) = delete;
        ProcessHandle& operator=(const ProcessHandle&) = delete;

        [[nodiscard]] HANDLE Get() const
        {
            return handle_;
        }

        [[nodiscard]] bool Valid() const
        {
            return handle_ != nullptr;
        }

    private:
        HANDLE handle_ = nullptr;
    };

    void PrintUsage()
    {
        std::printf("Usage: pskill [-t] [\\\\computer [-u username [-p password]]] <process ID | name>\n");
        std::printf("     -t    Kill the process and its descendants.\n");
        std::printf("     -u    Specifies optional user name for login to\n           remote computer.\n");
        std::printf("     -p    Specifies optional password for user name. If you omit this\n");
        std::printf("           you will be prompted to enter a hidden password.\n");
        std::printf("     -nobanner Do not display the startup banner and copyright message.\n");
        std::printf("\n");
    }

    // Erase the current console line on stderr, mirroring sub_140003100. Progress
    // output for the remote path is written with a leading carriage return, so on a
    // redirected stdout this leaves the tool's own output clean.
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

    // Enable a named privilege on the current process token. Mirrors sub_140003180.
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
            TOKEN_PRIVILEGES privileges = {};
            privileges.PrivilegeCount = 1;
            privileges.Privileges[0].Luid = luid;
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            ok = ::AdjustTokenPrivileges(token, FALSE, &privileges,
                                         sizeof(privileges), nullptr, nullptr) != 0
                 && ::GetLastError() == ERROR_SUCCESS;
        }
        ::CloseHandle(token);
        return ok;
    }

    // Build the process snapshot through NtQuerySystemInformation. Mirrors
    // sub_1400045F0, which grows the buffer until the query fits.
    [[nodiscard]] std::vector<ProcessRecord> SnapshotProcesses()
    {
        std::vector<ProcessRecord> records;
        const HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
        if (ntdll == nullptr)
        {
            return records;
        }
        const auto query = reinterpret_cast<NtQuerySystemInformationFn>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtQuerySystemInformation")));
        if (query == nullptr)
        {
            return records;
        }

        ULONG size = 1000;
        std::vector<BYTE> buffer(size);
        ULONG needed = 0;
        while (query(kSystemProcessInformation, buffer.data(), size, &needed) != 0)
        {
            size += 10000;
            buffer.assign(size, 0);
        }

        for (ULONG offset = 0;;)
        {
            const auto* entry =
                reinterpret_cast<const SystemProcessInformation*>(buffer.data() + offset);
            ProcessRecord record;
            record.pid = static_cast<DWORD>(entry->uniqueProcessId);
            record.parentPid = static_cast<DWORD>(entry->inheritedFromProcessId);
            record.createTime = entry->createTime.QuadPart;
            if (entry->imageName.Buffer != nullptr && entry->imageName.Length != 0)
            {
                const int chars = entry->imageName.Length / static_cast<USHORT>(sizeof(WCHAR));
                const int bytes = ::WideCharToMultiByte(CP_ACP, 0, entry->imageName.Buffer, chars,
                                                        nullptr, 0, nullptr, nullptr);
                if (bytes > 0)
                {
                    record.name.resize(static_cast<size_t>(bytes));
                    ::WideCharToMultiByte(CP_ACP, 0, entry->imageName.Buffer, chars,
                                          record.name.data(), bytes, nullptr, nullptr);
                }
            }
            records.push_back(std::move(record));

            if (entry->nextEntryOffset == 0)
            {
                break;
            }
            offset += entry->nextEntryOffset;
        }
        return records;
    }

    // Terminate one process by PID. Mirrors sub_140004A10: open with terminate and
    // synchronize rights, confirm the process has not already signalled, then
    // terminate with exit code 1. Returns 0 on success, otherwise the error code.
    [[nodiscard]] DWORD KillProcessById(DWORD pid)
    {
        const ProcessHandle process(::OpenProcess(kKillAccess, FALSE, pid));
        if (!process.Valid())
        {
            return ::GetLastError();
        }
        if (::WaitForSingleObject(process.Get(), 0) != WAIT_TIMEOUT)
        {
            return kAlreadyExited;
        }
        if (!::TerminateProcess(process.Get(), 1))
        {
            return ::GetLastError();
        }
        return 0;
    }

    // A process name matches the target when the target is a case-insensitive
    // prefix and the next character is a dot or the end of the name, so "notepad"
    // matches "notepad.exe". Mirrors the strnicmp plus trailing '.' or '\0' test.
    [[nodiscard]] bool NameMatches(const std::string& procName, const char* target)
    {
        const size_t len = std::strlen(target);
        if (::_strnicmp(procName.c_str(), target, len) != 0)
        {
            return false;
        }
        const char next = procName.c_str()[len];
        return next == '.' || next == '\0';
    }

    [[nodiscard]] const ProcessRecord* FindByPid(const std::vector<ProcessRecord>& procs, DWORD pid)
    {
        for (const auto& p : procs)
        {
            if (p.pid == pid)
            {
                return &p;
            }
        }
        return nullptr;
    }

    // Is candidate the root itself, or a descendant of it? The parent chain of
    // the candidate is climbed until it reaches the root (descendant) or runs out
    // (unrelated). Mirrors the ancestry walk in sub_1400046A0.
    [[nodiscard]] bool IsInTree(const std::vector<ProcessRecord>& procs, DWORD candidate, DWORD root)
    {
        DWORD current = candidate;
        for (size_t guard = 0; guard <= procs.size(); ++guard)
        {
            if (current == root)
            {
                return true;
            }
            const ProcessRecord* p = FindByPid(procs, current);
            if (p == nullptr || p->parentPid == 0)
            {
                return false;
            }
            current = p->parentPid;
        }
        return false;
    }

    // Collect the PIDs to terminate for a root, applying the creation-time guard
    // from the original: only a process created at or after the root can be part
    // of the root's tree. The root itself is always included.
    void CollectTree(const std::vector<ProcessRecord>& procs, DWORD rootPid,
                     long long rootTime, std::set<DWORD>& out)
    {
        for (const auto& p : procs)
        {
            if (p.createTime >= rootTime && IsInTree(procs, p.pid, rootPid))
            {
                out.insert(p.pid);
            }
        }
    }

    // Drive the local kill. Mirrors sub_1400046A0: enable SeDebugPrivilege, then
    // terminate the target (and, with tree set, its descendants), counting the
    // processes actually killed. Returns 0 on any success, otherwise the last
    // error, or 87 (ERROR_INVALID_PARAMETER) when nothing matched.
    //
    // ponytail: the original re-snapshots in a loop to catch children spawned
    // mid-kill. This takes a single snapshot and computes the full tree up front,
    // which is correct for the ordinary case and skips the race rescan.
    [[nodiscard]] DWORD KillLocal(const char* name, DWORD pid, bool tree, DWORD& count)
    {
        EnablePrivilege("SeDebugPrivilege");
        count = 0;

        if (name == nullptr && !tree)
        {
            const DWORD err = KillProcessById(pid);
            if (err == 0)
            {
                count = 1;
            }
            return err;
        }

        const std::vector<ProcessRecord> procs = SnapshotProcesses();
        if (procs.empty())
        {
            return ERROR_ACCESS_DENIED;
        }

        std::set<DWORD> targets;
        if (name != nullptr)
        {
            for (const auto& p : procs)
            {
                if (NameMatches(p.name, name))
                {
                    if (tree)
                    {
                        CollectTree(procs, p.pid, p.createTime, targets);
                    }
                    else
                    {
                        targets.insert(p.pid);
                    }
                }
            }
        }
        else
        {
            const ProcessRecord* root = FindByPid(procs, pid);
            const long long rootTime = root != nullptr ? root->createTime : 0;
            CollectTree(procs, pid, rootTime, targets);
        }

        DWORD lastError = ERROR_INVALID_PARAMETER;
        for (const DWORD target : targets)
        {
            const DWORD err = KillProcessById(target);
            if (err == 0)
            {
                ++count;
            }
            else
            {
                lastError = err;
            }
        }
        return count != 0 ? 0 : lastError;
    }

    // Terminate on a remote computer through the PSKLLSVC named-pipe service.
    // Mirrors the client half of sub_140004E00. Returns true on success and fills
    // count with the number of processes the service reported killed.
    //
    // ponytail: the original first deploys a bundled PSKLLSVC.EXE helper to the
    // target's ADMIN$ and starts it through the SCM, then talks to its
    // \pipe\pskllsvc named pipe and removes the service afterward. That helper
    // executable is out of scope, so this reproduces the faithful client protocol
    // (the 0x118-byte request and response, the status and count fields, and the
    // exact message and error paths) and leaves the service auto-deploy
    // unreproduced. Validated locally only.
    [[nodiscard]] bool KillRemote(const char* computer, const char* name, DWORD pid, bool tree,
                                  const pstools::Targets& creds, DWORD& count)
    {
        bool connected = false;
        if (creds.haveUser)
        {
            connected = pstools::RemoteConnect(computer, creds.user.c_str(), creds.password.c_str());
        }

        std::printf("\rConnecting with pskill service on %s...", computer);

        char pipe[MAX_PATH] = {};
        std::snprintf(pipe, std::size(pipe), "\\\\%s\\pipe\\pskllsvc", computer);
        const HANDLE h = ::CreateFileA(pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                       OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            ClearLine();
            std::printf("\rError establishing communication with pskill service on %s:\n", computer);
            pstools::PrintSystemError(::GetLastError());
            std::printf("\n");
            if (connected)
            {
                pstools::RemoteDisconnect(computer);
            }
            return false;
        }

        RemotePacket packet;
        if (name != nullptr)
        {
            packet.isName = 1;
            std::snprintf(packet.name, std::size(packet.name), "%s", name);
        }
        else
        {
            packet.isName = 0;
            packet.pid = pid;
        }
        packet.tree = tree ? 1 : 0;

        std::printf("\rKilling process%s %d on %s...", tree ? " tree" : "", pid, computer);

        DWORD moved = 0;
        if (!::WriteFile(h, &packet, kRemotePacketSize, &moved, nullptr))
        {
            ClearLine();
            std::printf("\rError communicating with pskill service on %s:\n", computer);
            pstools::PrintSystemError(::GetLastError());
            ::CloseHandle(h);
            std::printf("\n");
            if (connected)
            {
                pstools::RemoteDisconnect(computer);
            }
            return false;
        }
        if (!::ReadFile(h, &packet, kRemotePacketSize, &moved, nullptr))
        {
            ClearLine();
            std::printf("\rError communicating with pskill service on %s. The process may\n"
                        "not have been killed:\n", computer);
            pstools::PrintSystemError(::GetLastError());
            ::CloseHandle(h);
            std::printf("\n");
            if (connected)
            {
                pstools::RemoteDisconnect(computer);
            }
            return false;
        }
        ::CloseHandle(h);

        if (packet.status != 0)
        {
            ClearLine();
            if (name != nullptr)
            {
                std::printf("\rError killing process(es) named %s on %s:\n", name, computer);
            }
            else
            {
                std::printf("\rError killing process %d on %s:\n", pid, computer);
            }
            if (packet.status == ERROR_INVALID_PARAMETER)
            {
                if (name != nullptr)
                {
                    std::printf("Process %s does not exist on %s.\n", name, computer);
                }
                else
                {
                    std::printf("Process %d does not exist on %s.\n", pid, computer);
                }
            }
            else
            {
                pstools::PrintSystemError(packet.status);
            }
            std::printf("\n");
            if (connected)
            {
                pstools::RemoteDisconnect(computer);
            }
            return false;
        }

        count = packet.count;
        if (connected)
        {
            pstools::RemoteDisconnect(computer);
        }
        return true;
    }

    // Parsed command line. A single optional computer, one PID-or-name target,
    // the tree flag, and the shared alternate-credential fields.
    struct Options
    {
        std::string computer;   // the \\computer argument without the backslashes
        const char* name = nullptr;  // the process-name target, or null for a PID
        DWORD pid = 0;          // the PID target when name is null
        bool tree = false;      // -t
        bool computerSet = false;
        bool targetSet = false;
        std::string user;       // -u value
        std::string password;   // -p value (or prompted)
        bool haveUser = false;
        bool havePassword = false;
    };

    [[nodiscard]] bool IsAllDigits(const char* s)
    {
        if (s[0] == '\0')
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

    // Parse the PsKill command line. Mirrors sub_140004B20. Returns false when the
    // caller should print usage and fail.
    [[nodiscard]] bool ParseCommandLine(int argc, char** argv, Options& o)
    {
        if (argc <= 1)
        {
            return false;
        }
        for (int i = 1; i < argc; ++i)
        {
            char* arg = argv[i];
            if (arg[0] == '-' || arg[0] == '/')
            {
                if (arg[1] == '\0')
                {
                    continue;  // a bare '-' is ignored, as in the original
                }
                for (int j = 1; arg[j] != '\0'; ++j)
                {
                    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(arg[j])));
                    if (c == 'T')
                    {
                        o.tree = true;
                    }
                    else if (c == 'U')
                    {
                        if (i + 1 >= argc || o.haveUser)
                        {
                            return false;
                        }
                        o.user = argv[++i];
                        o.haveUser = true;
                        break;
                    }
                    else if (c == 'P')
                    {
                        if (i + 1 >= argc || o.havePassword)
                        {
                            return false;
                        }
                        o.password = argv[++i];
                        o.havePassword = true;
                        break;
                    }
                    else
                    {
                        return false;
                    }
                }
            }
            else if (IsAllDigits(arg))
            {
                if (o.targetSet)
                {
                    return false;
                }
                o.pid = static_cast<DWORD>(std::strtoul(arg, nullptr, 10));
                o.name = nullptr;
                o.targetSet = true;
            }
            else if (arg[0] == '\\' && arg[1] == '\\')
            {
                if (o.computerSet)
                {
                    return false;
                }
                o.computer = arg + 2;
                o.computerSet = true;
            }
            else
            {
                if (o.targetSet)
                {
                    return false;
                }
                o.name = arg;
                o.targetSet = true;
            }
        }

        if (!o.targetSet)
        {
            return false;
        }
        if ((o.haveUser && !o.computerSet) || (o.havePassword && !o.haveUser))
        {
            return false;
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

    const std::string local = pstools::LocalComputerName();
    const bool remote = o.computerSet && ::_stricmp(o.computer.c_str(), local.c_str()) != 0;

    DWORD count = 0;

    if (remote)
    {
        pstools::Targets creds;
        creds.user = o.user;
        creds.password = o.password;
        creds.haveUser = o.haveUser;
        creds.havePassword = o.havePassword;
        if (!KillRemote(o.computer.c_str(), o.name, o.pid, o.tree, creds, count))
        {
            return -1;
        }
        if (count > 1)
        {
            std::printf("\r%d processes named %s killed on %s.\n\n", count, o.name, o.computer.c_str());
        }
        else if (o.name != nullptr)
        {
            std::printf("\rProcess %s killed on %s.\n\n", o.name, o.computer.c_str());
        }
        else
        {
            std::printf("\rProcess %d on %s killed.\n\n", o.pid, o.computer.c_str());
        }
        return 0;
    }

    const DWORD err = KillLocal(o.name, o.pid, o.tree, count);
    if (err == 0)
    {
        if (count <= 1)
        {
            if (o.name != nullptr)
            {
                std::printf("Process %s killed.\n\n", o.name);
            }
            else
            {
                std::printf("Process %d killed.\n\n", o.pid);
            }
        }
        else if (o.tree)
        {
            if (o.name != nullptr)
            {
                std::printf("%d processes named %s and their descendants killed.\n\n", count, o.name);
            }
            else
            {
                std::printf("%d processes descended from and including %d killed.\n\n", count, o.pid);
            }
        }
        else
        {
            std::printf("%d processes named %s killed.\n\n", count, o.name);
        }
        return 0;
    }

    if (o.name != nullptr)
    {
        std::printf("Unable to kill process %s:\n", o.name);
    }
    else
    {
        std::printf("Unable to kill process %d:\n", o.pid);
    }
    if (err == ERROR_INVALID_PARAMETER)
    {
        std::printf("Process does not exist.\n");
        return -1;
    }
    pstools::PrintSystemError(err);
    return -1;
}
