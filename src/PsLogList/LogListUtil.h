// PsLogList shared base: the Win32 and CRT includes, the tool constants, the RAII
// guards for the event-log and registry handles, the parsed Options struct, and the
// generic string helpers. See analysis/PsLogList.md for the sub_ map.
#pragma once

#include "../common/PsTools.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace psloglist
{
    constexpr DWORD kInitialReadBuffer = 0x4000;
    constexpr int kMaxIdList = 10;       // -i / -e accept up to ten ids
    constexpr DWORD kFormatBuffer = 260; // GetDateFormat / GetTimeFormat buffer
    constexpr int kHexBytesPerLine = 16; // -x data dump width

    // Seconds between the 1601 FILETIME epoch and the 1970 Unix epoch, and the
    // 100ns ticks per second, used to turn an event record's time_t into a FILETIME.
    constexpr unsigned long long kUnixEpochInFiletimeSeconds = 11644473600ULL;
    constexpr unsigned long long kTicksPerSecond = 10000000ULL;
    constexpr long long kTicksPerDay = 864000000000LL;
    constexpr long long kTicksPerHour = 36000000000LL;
    constexpr long long kTicksPerMinute = 600000000LL;

    class EventLogGuard
    {
      public:
        explicit EventLogGuard(HANDLE h)
            : handle_(h)
        {
        }

        ~EventLogGuard()
        {
            if (handle_ != nullptr)
            {
                ::CloseEventLog(handle_);
            }
        }

        EventLogGuard(const EventLogGuard&) = delete;
        EventLogGuard& operator=(const EventLogGuard&) = delete;

        [[nodiscard]] HANDLE Get() const
        {
            return handle_;
        }

      private:
        HANDLE handle_ = nullptr;
    };

    class RegKeyGuard
    {
      public:
        RegKeyGuard() = default;

        explicit RegKeyGuard(HKEY k)
            : key_(k)
        {
        }

        ~RegKeyGuard()
        {
            if (key_ != nullptr)
            {
                ::RegCloseKey(key_);
            }
        }

        RegKeyGuard(const RegKeyGuard&) = delete;
        RegKeyGuard& operator=(const RegKeyGuard&) = delete;

        [[nodiscard]] HKEY Get() const
        {
            return key_;
        }

        [[nodiscard]] PHKEY Receive()
        {
            return &key_;
        }

      private:
        HKEY key_ = nullptr;
    };

    struct Options
    {
        std::string logName = "System"; // the account argument, default System
        std::string computerList;       // \\computer, comma list, or @file
        std::string user;               // -u value
        std::string password;           // -p value, entered at the prompt when omitted
        bool haveUser = false;
        bool havePassword = false;
        bool computerSpecified = false;

        bool oneLine = false; // -s
        char delimiter = ','; // -t
        bool haveDelim = false;

        int maxRecords = -1;      // -n
        bool oldestFirst = false; // -r

        bool extended = false; // -x
        bool clear = false;    // -c

        std::vector<int> includeIds; // -i
        std::vector<int> excludeIds; // -e

        std::string sourceInclude; // -o
        bool haveSourceInclude = false;
        std::string sourceExclude; // -q
        bool haveSourceExclude = false;

        std::string typeFilter; // -f, uppercased

        bool cutoffSet = false; // -d / -h / -m produce a single cutoff
        FILETIME cutoff = {};
        bool afterSet = false; // -a
        FILETIME after = {};
        bool beforeSet = false; // -b
        FILETIME before = {};
    };

    [[nodiscard]] inline std::wstring Widen(const std::string& s)
    {
        if (s.empty())
        {
            return std::wstring();
        }
        const int needed = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        if (needed <= 0)
        {
            return std::wstring();
        }
        std::wstring result(static_cast<size_t>(needed), L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, result.data(), needed);
        if (!result.empty() && result.back() == L'\0')
        {
            result.pop_back();
        }
        return result;
    }

    // Case-insensitive substring test, standing in for the original's sub_1400017C0.
    [[nodiscard]] inline bool ContainsNoCase(const char* haystack, const std::string& needle)
    {
        if (needle.empty())
        {
            return true;
        }
        const size_t nlen = needle.size();
        for (const char* p = haystack; *p != '\0'; ++p)
        {
            if (::_strnicmp(p, needle.c_str(), nlen) == 0)
            {
                return true;
            }
        }
        return false;
    }
} // namespace psloglist
