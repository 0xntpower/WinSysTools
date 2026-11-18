// Eula.h -- the Sysinternals license gate.
//
// The original records acceptance as a REG_DWORD named EulaAccepted under
// Software\Sysinternals\Autoruns (see sub_140062810, which writes the value to
// HKCU after the prompt). It checks HKLM\Software\Sysinternals,
// HKCU\Software\Sysinternals and the per-tool key, and the -accepteula or
// /accepteula switch skips the prompt and records consent.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Eula
{
    inline constexpr const wchar_t* kSuiteKey = L"Software\\Sysinternals";
    inline constexpr const wchar_t* kAppKey = L"Software\\Sysinternals\\Autoruns";
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
    // -accepteula or /accepteula argument.
    [[nodiscard]] inline bool EnsureAccepted(HWND owner, bool acceptSwitch)
    {
        if (acceptSwitch || IsAccepted())
        {
            RecordAccepted();
            return true;
        }
        const wchar_t* body = L"Autoruns shows what programs are configured to run during system "
                              L"boot or login.\r\n\r\n"
                              L"This is an independent source-accurate recreation of the Sysinternals "
                              L"Autoruns utility. The Sysinternals Software License Terms apply to the "
                              L"original.\r\n\r\n"
                              L"You can also use the /accepteula command-line switch to accept the "
                              L"license.\r\n\r\nDo you accept the license terms?";
        const int r = ::MessageBoxW(owner, body, L"Autoruns License Agreement", MB_YESNO | MB_ICONINFORMATION);
        if (r == IDYES)
        {
            RecordAccepted();
            return true;
        }
        return false;
    }
} // namespace Eula
