// PsKill shared helpers: the RAII process handle, the console-line eraser, and
// the privilege enabler. See analysis/PsKill.md for the sub_ map. The shared
// PsTools front end (banner, EULA gate, alternate credentials, error reporting,
// local identity, hidden password prompt) lives in ../common/PsTools.h.
#pragma once

#include "../common/PsTools.h"

#include <windows.h>

#include <cstdio>

namespace pskill
{
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

    // Erase the current console line on stderr, mirroring sub_140003100. Progress
    // output for the remote path is written with a leading carriage return, so on a
    // redirected stdout this leaves the tool's own output clean.
    inline void ClearLine()
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
            TOKEN_PRIVILEGES privileges = {};
            privileges.PrivilegeCount = 1;
            privileges.Privileges[0].Luid = luid;
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            ok = ::AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr) != 0 &&
                 ::GetLastError() == ERROR_SUCCESS;
        }
        ::CloseHandle(token);
        return ok;
    }
} // namespace pskill
