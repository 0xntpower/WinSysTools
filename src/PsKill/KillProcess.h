// PsKill terminate logic: the single-PID kill (sub_140004A10), the local driver
// with its optional tree walk (sub_1400046A0), and the remote named-pipe client
// (sub_140004E00). See analysis/PsKill.md for the sub_ map.
#pragma once

#include "ProcessSnapshot.h"

#include <cstdio>

namespace pskill
{
    // OpenProcess rights the original requests: PROCESS_TERMINATE | SYNCHRONIZE.
    constexpr DWORD kKillAccess = PROCESS_TERMINATE | SYNCHRONIZE;

    // The original's sentinel for "the process was already signalled, so there
    // was nothing to terminate" (WaitForSingleObject returned other than
    // WAIT_TIMEOUT). Code 758 is reported through the usual error path.
    constexpr DWORD kAlreadyExited = 758;

    // The named-pipe request and response packet is a fixed 0x118 bytes.
    constexpr DWORD kRemotePacketSize = 0x118;

    // The 0x118-byte request and response exchanged with the PSKLLSVC service.
    // The request sets isName, then either name or pid, plus the tree flag. The
    // service fills status and count on the way back.
#pragma pack(push, 1)
    struct RemotePacket
    {
        BYTE isName = 0;       // 0x000
        char name[263] = {};   // 0x001
        DWORD pid = 0;         // 0x108
        BYTE tree = 0;         // 0x10C
        BYTE reserved[3] = {}; // 0x10D
        DWORD status = 0;      // 0x110
        DWORD count = 0;       // 0x114
    };
#pragma pack(pop)

    static_assert(sizeof(RemotePacket) == kRemotePacketSize);

    // Terminate one process by PID. Mirrors sub_140004A10: open with terminate and
    // synchronize rights, confirm the process has not already signalled, then
    // terminate with exit code 1. Returns 0 on success, otherwise the error code.
    [[nodiscard]] inline DWORD KillProcessById(DWORD pid)
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

    // Drive the local kill. Mirrors sub_1400046A0: enable SeDebugPrivilege, then
    // terminate the target (and, with tree set, its descendants), counting the
    // processes actually killed. Returns 0 on any success, otherwise the last
    // error, or 87 (ERROR_INVALID_PARAMETER) when nothing matched.
    //
    // ponytail: the original re-snapshots in a loop to catch children spawned
    // mid-kill. This takes a single snapshot and computes the full tree up front,
    // which is correct for the ordinary case and skips the race rescan.
    [[nodiscard]] inline DWORD KillLocal(const char* name, DWORD pid, bool tree, DWORD& count)
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
    // ponytail: the original deploys a bundled PSKLLSVC.EXE to the target ADMIN$
    // and drives it through the SCM. That helper is out of scope, so this
    // reproduces the faithful client protocol only and leaves the auto-deploy
    // unreproduced. See analysis/PsKill.md. Validated locally only.
    [[nodiscard]] inline bool KillRemote(const char* computer, const char* name, DWORD pid, bool tree,
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
        const HANDLE h = ::CreateFileA(pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
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
                        "not have been killed:\n",
                        computer);
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
} // namespace pskill
