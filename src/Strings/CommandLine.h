// Strings argument parse: the switch matcher, the argv strip for -nobanner and
// -accepteula, and the EULA registry gate (mirrors sub_140002160, sub_140002400).
#pragma once

#include "StringsUtil.h"

#include <cstdio>
#include <cstring>

namespace strings
{
    [[nodiscard]] inline bool ArgEquals(const char* arg, const char* dash, const char* slash) noexcept
    {
        return _stricmp(arg, dash) == 0 || _stricmp(arg, slash) == 0;
    }

    // Remove the first argument that matches either spelling of a switch, closing
    // the gap in argv and shrinking argc, the way the original strips -nobanner
    // and -accepteula before the main parse.
    [[nodiscard]] inline bool StripSwitch(int& argc, char** argv, const char* dash, const char* slash)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (ArgEquals(argv[i], dash, slash))
            {
                for (int j = i; j < argc - 1; ++j)
                {
                    argv[j] = argv[j + 1];
                }
                --argc;
                return true;
            }
        }
        return false;
    }

    // Confirm EULA acceptance. -accepteula or a prior run both let the tool run
    // unattended and write HKCU\Software\Sysinternals\Strings\EulaAccepted. A
    // first run with neither prints the accept notice and stops.
    [[nodiscard]] inline bool EnsureEula(bool acceptedOnCommandLine)
    {
        bool accepted = acceptedOnCommandLine;

        if (!accepted)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Sysinternals\\Strings", 0, KEY_READ, &key) == ERROR_SUCCESS)
            {
                DWORD value = 0;
                DWORD size = sizeof(value);
                DWORD type = 0;
                if (RegQueryValueExA(key, "EulaAccepted", nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size) ==
                        ERROR_SUCCESS &&
                    type == REG_DWORD && value != 0)
                {
                    accepted = true;
                }
                RegCloseKey(key);
            }
        }

        if (!accepted)
        {
            std::fprintf(stderr, "Strings License Agreement\n");
            std::fprintf(stderr, "This is the first run of this program. You must accept EULA to continue.\n");
            std::fprintf(stderr, "Use -accepteula to accept EULA.\n\n");
            return false;
        }

        HKEY key = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Sysinternals\\Strings", 0, nullptr, 0, KEY_WRITE, nullptr,
                            &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            RegSetValueExA(key, "EulaAccepted", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
            RegCloseKey(key);
        }
        return true;
    }
} // namespace strings
