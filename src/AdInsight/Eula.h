// Eula.h -- the Sysinternals license gate for AdInsight.
//
// The original forms its per-tool key as Software\Sysinternals\%s with the
// app name "ADInsight", stores EulaAccepted as a REG_DWORD in HKCU, and also
// honours a suite-level HKCU\Software\Sysinternals\EulaAccepted. The
// -accepteula and /accepteula switches record consent without prompting. The
// registry is case insensitive, so the original's mixed AdInsight and
// ADInsight spellings reference the same key.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Eula
{
    inline constexpr const wchar_t* kSuiteKey = L"Software\\Sysinternals";
    inline constexpr const wchar_t* kAppKey = L"Software\\Sysinternals\\ADInsight";
    inline constexpr const wchar_t* kValue = L"EulaAccepted";

    [[nodiscard]] inline bool FlagSet(HKEY root, const wchar_t* sub)
    {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0;
        DWORD cb = sizeof(data);
        DWORD type = 0;
        const LSTATUS st = ::RegQueryValueExW(key, kValue, nullptr, &type, reinterpret_cast<LPBYTE>(&data), &cb);
        ::RegCloseKey(key);
        return st == ERROR_SUCCESS && data != 0;
    }

    [[nodiscard]] inline bool IsAccepted()
    {
        return FlagSet(HKEY_CURRENT_USER, kSuiteKey) || FlagSet(HKEY_CURRENT_USER, kAppKey);
    }

    inline void RecordAccepted()
    {
        HKEY key = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, kAppKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
            ERROR_SUCCESS)
        {
            DWORD one = 1;
            ::RegSetValueExW(key, kValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
            ::RegCloseKey(key);
        }
    }

    // Returns true when the program may continue. acceptSwitch reflects a
    // -accepteula or /accepteula argument on the command line.
    [[nodiscard]] inline bool EnsureAccepted(HWND owner, bool acceptSwitch)
    {
        if (acceptSwitch || IsAccepted())
        {
            RecordAccepted();
            return true;
        }
        const wchar_t* body = L"AdInsight captures the LDAP (Active Directory) client API calls that "
                              L"processes make, showing each operation, its parameters, result and duration.\r\n\r\n"
                              L"This is an independent source-accurate recreation of the Sysinternals AdInsight "
                              L"utility. The Sysinternals Software License Terms apply to the original.\r\n\r\n"
                              L"You can also use the /accepteula command-line switch to accept the license.\r\n\r\n"
                              L"Do you accept the license terms?";
        const int r = ::MessageBoxW(owner, body, L"AdInsight License Agreement", MB_YESNO | MB_ICONINFORMATION);
        if (r == IDYES)
        {
            RecordAccepted();
            return true;
        }
        return false;
    }
} // namespace Eula
