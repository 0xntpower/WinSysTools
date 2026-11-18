// Eula.h -- the Sysinternals license gate.
//
// Mirrors the original flow in sub_140004128: acceptance is recorded as a
// REG_DWORD named EulaAccepted. The original checks, in order,
// HKLM\Software\Sysinternals, HKCU\Software\Sysinternals and
// HKCU\Software\Sysinternals\VMMap. A set flag or the -accepteula / /accepteula
// switch skips the prompt and records consent under the per-tool key.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Eula
{
    inline constexpr const wchar_t* kSuiteKey = L"Software\\Sysinternals";
    inline constexpr const wchar_t* kAppKey = L"Software\\Sysinternals\\VMMap";
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
        return FlagSet(HKEY_LOCAL_MACHINE, kSuiteKey) || FlagSet(HKEY_CURRENT_USER, kSuiteKey) ||
               FlagSet(HKEY_CURRENT_USER, kAppKey);
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
        const wchar_t* body = L"VMMap analyzes the virtual and physical memory of a process.\r\n\r\n"
                              L"This is an independent source-accurate recreation of the Sysinternals "
                              L"VMMap utility. The Sysinternals Software License Terms apply to the "
                              L"original.\r\n\r\n"
                              L"You can also use the /accepteula command-line switch to accept the "
                              L"license.\r\n\r\nDo you accept the license terms?";
        const int r = ::MessageBoxW(owner, body, L"VMMap License Agreement", MB_YESNO | MB_ICONINFORMATION);
        if (r == IDYES)
        {
            RecordAccepted();
            return true;
        }
        return false;
    }
} // namespace Eula
