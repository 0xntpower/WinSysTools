// PsInfo shared base: the tool options, the ANSI to wide helper, the RAII
// registry key with its string and dword readers, and the collected-data
// structs. See analysis/PsInfo.md for the sub_ map.
#pragma once

#include "../common/PsTools.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "pdh.lib")

namespace psinfo
{
    constexpr REGSAM kRead = KEY_READ;

    // Tool flags, matching the parser globals byte_14004F0E7 and friends.
    struct Options
    {
        bool hotfixes = false;    // -h
        bool software = false;    // -s
        bool disks = false;       // -d
        bool csv = false;         // -c
        wchar_t delimiter = L','; // -t value, default comma (word_14004E190)
        std::wstring filter;      // positional field filter (qword_14004F0E8)
        bool hasFilter = false;
        std::wstring user;     // -u
        std::wstring password; // -p
        bool haveUser = false;
    };

    inline Options g_opt;

    [[nodiscard]] inline std::wstring Widen(const char* s)
    {
        if (s == nullptr || s[0] == '\0')
        {
            return {};
        }
        const int n = ::MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        std::wstring w(n > 0 ? n - 1 : 0, L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
        return w;
    }

    struct RegKey
    {
        HKEY key = nullptr;

        RegKey() = default;
        RegKey(const RegKey&) = delete;
        RegKey& operator=(const RegKey&) = delete;

        ~RegKey()
        {
            if (key != nullptr)
            {
                ::RegCloseKey(key);
            }
        }

        [[nodiscard]] bool Open(HKEY root, const wchar_t* sub, REGSAM access)
        {
            return ::RegOpenKeyExW(root, sub, 0, access, &key) == ERROR_SUCCESS;
        }
    };

    [[nodiscard]] inline std::wstring ReadString(HKEY key, const wchar_t* name)
    {
        DWORD type = 0;
        DWORD cb = 0;
        if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS || cb == 0)
        {
            return {};
        }
        std::wstring value(cb / sizeof(wchar_t) + 1, L'\0');
        if (::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(value.data()), &cb) != ERROR_SUCCESS)
        {
            return {};
        }
        value.resize(::wcsnlen(value.c_str(), value.size()));
        return value;
    }

    [[nodiscard]] inline DWORD ReadDword(HKEY key, const wchar_t* name)
    {
        DWORD data = 0;
        DWORD cb = sizeof(data);
        DWORD type = 0;
        ::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&data), &cb);
        return data;
    }

    struct Hotfix
    {
        std::wstring name;
        FILETIME written{};
    };

    struct Application
    {
        std::wstring name;
        std::wstring version;
    };

    struct Volume
    {
        wchar_t letter = L'A';
        UINT type = 0;
        std::wstring label;
        std::wstring format;
        unsigned long long total = 0;
        unsigned long long free = 0;
    };

    struct SystemData
    {
        std::wstring productName;
        std::wstring currentType;
        std::wstring currentVersion;
        std::wstring buildNumber;
        std::wstring csdVersion = L"0";
        std::wstring subVersion;
        std::wstring registeredOrg;
        std::wstring registeredOwner;
        std::wstring systemRoot;
        std::wstring videoDriver;
        std::wstring ieVersion;
        std::wstring numProcessors;
        std::wstring processorId;
        std::wstring processorName;
        std::wstring productType;
        std::wstring productSuite;
        DWORD mhz = 0;
        unsigned long long totalPhys = 0;
        int verMajor = 0;
        int verMinor = 0;

        bool uptimeValid = false;
        long long days = 0;
        long long hours = 0;
        long long minutes = 0;
        long long seconds = 0;

        std::vector<Hotfix> hotfixList;
        std::vector<Application> appList;
        std::vector<Volume> volumeList;
    };
} // namespace psinfo
