// PsShutdown action functions and the per-target worker: the immediate local
// calls (mirrors sub_140004840, sub_140004820, the lock branch of sub_140004A90)
// and the worker that routes each target to them or to the service client.
#pragma once

#include "ServiceClient.h"

namespace psshutdown
{
    // SAFETY: the immediate local actions run only when the target is the local
    // machine, there is no countdown, and the action is not abort. They are the
    // destructive calls the original makes directly, reproduced from sub_140004840,
    // sub_140004820, and the lock branch of sub_140004A90. They are never driven
    // during validation.

    using SetSuspendStateFn = BOOLEAN(__stdcall*)(BOOLEAN, BOOLEAN, BOOLEAN);
    using LockWorkStationFn = BOOL(__stdcall*)();

    // Perform the immediate shutdown, reboot, poweroff, hibernate, or suspend.
    // Mirrors sub_140004840: ExitWindowsEx carries the force bit shifted down from
    // the flag word, and hibernate or suspend go through Powrprof SetSuspendState.
    [[nodiscard]] inline bool ImmediateShutdown(DWORD flags, DWORD reason)
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
        const auto setSuspendState =
            reinterpret_cast<SetSuspendStateFn>(reinterpret_cast<void*>(::GetProcAddress(powrprof, "SetSuspendState")));
        if (setSuspendState != nullptr)
        {
            return setSuspendState(static_cast<BOOLEAN>(flags & kHibernate), TRUE, FALSE) != 0;
        }
        return false;
    }

    // Turn the monitor off. Mirrors sub_140004820: broadcast WM_SYSCOMMAND with
    // SC_MONITORPOWER and the power-off argument.
    inline void MonitorOff()
    {
        ::PostMessageA(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
    }

    // Lock the console session. Mirrors the lock branch of sub_140004A90, which
    // resolves LockWorkStation dynamically so the message matches old systems.
    // Returns the worker result (true when the action completed or reported).
    [[nodiscard]] inline bool LocalLock()
    {
        const HMODULE user32 = ::GetModuleHandleA("User32.dll");
        const auto lockWorkStation =
            reinterpret_cast<LockWorkStationFn>(reinterpret_cast<void*>(::GetProcAddress(user32, "LockWorkStation")));
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

    // Per-target worker. Mirrors sub_140004A90: a local target with no countdown
    // and no abort runs the immediate action directly, anything else (a remote
    // target, an alternate user, a countdown, or an abort) goes through the
    // service client. Returns the original's worker result, zero on success.
    [[nodiscard]] inline int Worker(const Options& o, const std::string& localName, const char* computer)
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
} // namespace psshutdown
