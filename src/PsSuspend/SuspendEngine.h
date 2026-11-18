// PsSuspend local and remote engine: enable the privilege, resolve the ntdll
// suspend and resume calls, act on one process or every name match, and the
// PSSPNDSVC client protocol. Mirrors sub_1400045F0, sub_1400048D0, sub_140003180,
// and sub_1400052B0. See analysis/PsSuspend.md.
#pragma once

#include "CommandLine.h"

#include <tlhelp32.h>

namespace pssuspend
{
    // PROCESS_SUSPEND_RESUME, the only access the Nt suspend and resume calls need.
    constexpr DWORD kProcessSuspendResume = 0x0800;

    // Blank the current console line so a \r-prefixed message overwrites the
    // progress line cleanly. Mirrors sub_140003100.
    inline void ClearLine()
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

    // Enable one privilege on the current process token. Mirrors sub_140003180.
    inline bool EnablePrivilege(const char* name)
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
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
            ok = ::AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) != 0 &&
                 ::GetLastError() == ERROR_SUCCESS;
        }
        ::CloseHandle(token);
        return ok;
    }

    using NtProcessFn = LONG(NTAPI*)(HANDLE);

    struct NtProc
    {
        NtProcessFn suspend = nullptr;
        NtProcessFn resume = nullptr;
    };

    [[nodiscard]] inline NtProc ResolveNtProc()
    {
        NtProc fns;
        const HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
        if (ntdll != nullptr)
        {
            fns.suspend =
                reinterpret_cast<NtProcessFn>(reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtSuspendProcess")));
            fns.resume =
                reinterpret_cast<NtProcessFn>(reinterpret_cast<void*>(::GetProcAddress(ntdll, "NtResumeProcess")));
        }
        return fns;
    }

    // Open one process and apply the suspend or resume. Mirrors the modern branch
    // of sub_1400048D0. Returns 0 on success, otherwise a Windows error (from
    // OpenProcess) or the Nt status the call returned.
    [[nodiscard]] inline DWORD ActOnPid(const NtProc& fns, bool suspend, DWORD pid)
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
    [[nodiscard]] inline bool NameMatches(const char* image, const char* target)
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
    [[nodiscard]] inline DWORD LocalAct(const Options& o, int& count)
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
                ::WideCharToMultiByte(CP_ACP, 0, entry.szExeFile, -1, image, static_cast<int>(std::size(image)),
                                      nullptr, nullptr);
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
            } while (::Process32NextW(snapshot, &entry));
        }
        ::CloseHandle(snapshot);
        return count > 0 ? 0 : last;
    }

    // The 276-byte request and response the client exchanges with the remote
    // PSSPNDSVC helper over its named pipe. Recovered from sub_1400052B0: the
    // suspend flag and a "by name" flag, then either the name or the PID, and on
    // the way back a status code and the affected-process count.
#pragma pack(push, 1)
    struct SuspendPacket
    {
        unsigned char suspend; // 0x000
        unsigned char byName;  // 0x001
        char name[262];        // 0x002
        DWORD pid;             // 0x108
        DWORD status;          // 0x10C
        DWORD count;           // 0x110
    };
#pragma pack(pop)
    static_assert(sizeof(SuspendPacket) == 0x114, "PSSPNDSVC packet must be 276 bytes");

    inline void PrintRemoteError(const Options& o, const char* verbIng)
    {
        ClearLine();
        if (o.byName)
        {
            std::printf("\rError %s process(es) named %s on %s:\n", verbIng, o.name.c_str(), o.computer.c_str());
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
    // ponytail: the original first deploys the bundled PSSPNDSVC.EXE to the remote
    // ADMIN$ and starts it through the SCM (sub_1400036F0) before talking to the
    // pipe, and tears it down afterward (sub_140003D40). That helper executable and
    // its service lifecycle are out of scope, so this reproduces the faithful
    // client protocol and the exact message and error paths that run when the
    // service is reached, or not reached, on the target.
    [[nodiscard]] inline bool RemoteAct(const Options& o, int& count)
    {
        const bool suspend = !o.resume;
        bool connected = false;
        if (o.haveUser)
        {
            connected = pstools::RemoteConnect(o.computer.c_str(), o.user.c_str(), o.password.c_str());
        }

        ClearLine();
        std::printf("\rConnecting with PsSuspend service on %s...", o.computer.c_str());

        char pipe[MAX_PATH] = {};
        std::snprintf(pipe, std::size(pipe), "\\\\%s\\pipe\\psspndsvc", o.computer.c_str());
        const HANDLE h = ::CreateFileA(pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            const DWORD err = ::GetLastError();
            ClearLine();
            std::printf("\rError establishing communication with PsSuspend service on %s:\n", o.computer.c_str());
            pstools::PrintSystemError(err);
            std::printf("\n");
            if (connected)
            {
                pstools::RemoteDisconnect(o.computer.c_str());
            }
            return false;
        }

        ClearLine();
        std::printf("\r%s process %d on %s...", suspend ? "Suspending" : "Resuming", o.pid, o.computer.c_str());

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
                    std::printf("Process %s does not exist on %s.\n", o.name.c_str(), o.computer.c_str());
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
} // namespace pssuspend
