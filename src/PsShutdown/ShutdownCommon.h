// PsShutdown shared pieces: the action and modifier bit constants, the reason
// folding masks, the console line eraser, and the privilege helper. See
// analysis/PsShutdown.md for the sub_ map.
#pragma once

#include "../common/PsTools.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace psshutdown
{
    // Action and modifier bits in the parser's flag word (dword_140049F98). The
    // nine action bits are mutually exclusive, force and allow-abort are modifiers.
    constexpr DWORD kLock = 0x001;      // -l  Lock the computer
    constexpr DWORD kReboot = 0x002;    // -r  Reboot after shutdown
    constexpr DWORD kLogoff = 0x004;    // -o  Logoff the console user
    constexpr DWORD kAbort = 0x008;     // -a  Abort a shutdown in progress
    constexpr DWORD kPoweroff = 0x010;  // -k  Poweroff the computer
    constexpr DWORD kShutdown = 0x020;  // -s  Shutdown without poweroff
    constexpr DWORD kForce = 0x040;     // -f  Force running applications to close
    constexpr DWORD kHibernate = 0x080; // -h  Hibernate the computer
    constexpr DWORD kSuspend = 0x100;   // -d  Suspend the computer
    constexpr DWORD kMonitor = 0x200;   // -x  Turn monitor off

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

    // Blank the current console line so a \r-prefixed message overwrites the
    // progress output. Mirrors sub_140003100: the width is queried once and cached,
    // the padding goes to stderr, and a redirected run leaves the width at zero.
    // The query also overwrites GetLastError, which the service-connect paths read.
    inline void ClearLine()
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

    // Enable one privilege on the current process token. Mirrors sub_140003180.
    [[nodiscard]] inline bool EnablePrivilege(const char* name)
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

    [[nodiscard]] inline std::string Upper(const std::string& s)
    {
        std::string out = s;
        ::_strupr(out.data());
        return out;
    }
} // namespace psshutdown
