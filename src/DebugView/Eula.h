// Eula.h -- the Sysinternals license gate.
//
// Matches the original flow in sub_140001EC0: the acceptance flag lives at
// HKCU\Software\Sysinternals\DebugView\EulaAccepted as a REG_DWORD. A set flag
// or the -accepteula / /accepteula switch skips the prompt and records consent.
// Otherwise a modal agreement asks the user, and a decline stops the launch.
#pragma once
#include <windows.h>
#include <string>
#include "RegKey.h"

namespace Eula
{
    inline constexpr const wchar_t* kKey   = L"Software\\Sysinternals\\DebugView";
    inline constexpr const wchar_t* kValue = L"EulaAccepted";

    inline bool IsAccepted()
    {
        RegKey k;
        if (!k.open(HKEY_CURRENT_USER, kKey, KEY_READ))
            return false;
        auto v = k.queryDword(kValue);
        return v && *v != 0;
    }

    inline void RecordAccepted()
    {
        RegKey k;
        if (k.create(HKEY_CURRENT_USER, kKey, KEY_READ | KEY_WRITE))
            k.setDword(kValue, 1);
    }

    // Returns true when the program may continue. acceptSwitch reflects a
    // -accepteula or /accepteula argument on the command line.
    inline bool EnsureAccepted(HWND owner, bool acceptSwitch)
    {
        if (IsAccepted())
            return true;
        if (acceptSwitch)
        {
            RecordAccepted();
            return true;
        }
        const wchar_t* body =
            L"DebugView captures debug output from the local system.\n\n"
            L"This is an independent source-accurate recreation of the "
            L"Sysinternals DebugView utility. The Sysinternals Software License "
            L"Terms apply to the original.\n\n"
            L"You can also use the /accepteula command-line switch to accept the "
            L"license.\n\nDo you accept the license terms?";
        int r = ::MessageBoxW(owner, body, L"DebugView License Agreement",
                              MB_YESNO | MB_ICONINFORMATION);
        if (r == IDYES)
        {
            RecordAccepted();
            return true;
        }
        return false;
    }
}
