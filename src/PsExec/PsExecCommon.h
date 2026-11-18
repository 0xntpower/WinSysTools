// PsExec shared core: the Win32 and security includes, the recovered constants,
// the priority table, the parsed Options record, the RAII handle, and the small
// string and affinity helpers shared across the parse and launch paths. The sub_
// map lives in analysis/PsExec.md.
#pragma once

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

namespace psexec
{
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
    constexpr DWORD kBackgroundClass = 0x00100000; // PROCESS_MODE_BACKGROUND_BEGIN

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
        {"low", IDLE_PRIORITY_CLASS},                 // 0x40
        {"belownormal", BELOW_NORMAL_PRIORITY_CLASS}, // 0x4000
        {"normal", NORMAL_PRIORITY_CLASS},            // 0x20
        {"abovenormal", ABOVE_NORMAL_PRIORITY_CLASS}, // 0x8000
        {"high", HIGH_PRIORITY_CLASS},                // 0x80
        {"realtime", REALTIME_PRIORITY_CLASS},        // 0x100
        {"background", kBackgroundClass},             // 0x100000
    };

    // The parsed command line. One entry per PsExec switch, plus the computer list,
    // the command, and its argument tail.
    struct Options
    {
        std::string computerList; // \\computer, comma list, "@file", "*" or empty for local
        bool computerSpecified = false;
        std::string user;     // -u
        std::string password; // -p (or prompted)
        bool haveUser = false;
        bool havePassword = false;
        std::string app; // the command to run
        bool appSpecified = false;
        std::string args;    // the argument tail after the command
        std::string workDir; // -w
        bool haveWorkDir = false;
        std::string serviceName = kDefaultService; // -r
        DWORD priorityClass = 0;                   // -<priority>
        DWORD affinityMask = 0;                    // -a
        WORD group = 0;                            // -g
        bool groupSpecified = false;
        int timeoutSeconds = -1;    // -n
        int session = -1;           // -i <session>
        bool interactive = false;   // -i
        bool systemAccount = false; // -s
        bool elevated = false;      // -h
        bool limited = false;       // -l
        bool secureDesktop = false; // -x
        bool copyProgram = false;   // -c
        bool forceCopy = false;     // -f
        bool copyIfNewer = false;   // -v
        bool noProfile = false;     // -e (clears the load-profile default)
        bool dontWait = false;      // -d
        bool arm = false;           // -arm
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

    [[nodiscard]] inline std::wstring Widen(const std::string& s)
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

    [[nodiscard]] inline bool IsAllDigits(const char* s)
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
    inline void SplitUser(const std::string& full, std::string& account, std::string& domain)
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
    inline void ParseAffinity(const char* s, DWORD& mask)
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
} // namespace psexec
