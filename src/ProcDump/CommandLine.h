// ProcDump command-line handling: the EULA gate and the switch/argument parse.
// See analysis/ProcDump.md for the sub_ map.
#pragma once

#include "ProcDumpCommon.h"

namespace procdump
{
    // EULA gate. HKCU\Software\Sysinternals\ProcDump\EulaAccepted mirrors the
    // standalone Sysinternals pattern, and the suite-wide flag is also honoured.
    [[nodiscard]] inline bool EulaFlagSet(HKEY root, const wchar_t* sub)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0;
        DWORD cb = sizeof(data);
        const LSTATUS st =
            RegQueryValueExW(key, L"EulaAccepted", nullptr, nullptr, reinterpret_cast<LPBYTE>(&data), &cb);
        RegCloseKey(key);
        return st == ERROR_SUCCESS && data != 0;
    }

    inline void WriteEulaAccepted()
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Sysinternals\\ProcDump", 0, nullptr, 0, KEY_SET_VALUE,
                            nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            RegSetValueExW(key, L"EulaAccepted", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
            RegCloseKey(key);
        }
    }

    [[nodiscard]] inline bool EnsureEula(bool acceptFlag)
    {
        if (acceptFlag || EulaFlagSet(HKEY_LOCAL_MACHINE, L"Software\\Sysinternals") ||
            EulaFlagSet(HKEY_CURRENT_USER, L"Software\\Sysinternals") ||
            EulaFlagSet(HKEY_CURRENT_USER, L"Software\\Sysinternals\\ProcDump"))
        {
            WriteEulaAccepted();
            return true;
        }
        std::fputs("\nProcDump License Agreement\n", stderr);
        std::fputs("This is the first run of this program. "
                   "You must accept EULA to continue.\n",
                   stderr);
        std::fputs("Use -accepteula to accept EULA.\n\n", stderr);
        return false;
    }

    // Parse one integer argument, returning false on a non-numeric token.
    [[nodiscard]] inline bool ParseInt(const wchar_t* s, int& out)
    {
        wchar_t* end = nullptr;
        const long value = std::wcstol(s, &end, 10);
        if (end == s || (end != nullptr && *end != L'\0'))
        {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    }

    [[nodiscard]] inline bool ParseArgs(int argc, wchar_t** argv, Config& cfg)
    {
        for (int i = 1; i < argc; ++i)
        {
            const wchar_t* a = argv[i];
            if (a[0] == L'-' || a[0] == L'/')
            {
                if (ArgEquals(a, L"-mm"))
                {
                    cfg.type = DumpType::Mini;
                    cfg.typeChosen = true;
                }
                else if (ArgEquals(a, L"-ma"))
                {
                    cfg.type = DumpType::Full;
                    cfg.typeChosen = true;
                }
                else if (ArgEquals(a, L"-mt"))
                {
                    cfg.type = DumpType::Triage;
                    cfg.typeChosen = true;
                }
                else if (ArgEquals(a, L"-mp"))
                {
                    cfg.type = DumpType::MiniPlus;
                    cfg.typeChosen = true;
                }
                else if (ArgEquals(a, L"-mc"))
                {
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    cfg.customMask = static_cast<DWORD>(std::wcstoul(argv[++i], nullptr, 16));
                    cfg.type = DumpType::Custom;
                    cfg.typeChosen = true;
                }
                else if (ArgEquals(a, L"-md"))
                {
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    cfg.callbackDll = argv[++i];
                    cfg.type = DumpType::Callback;
                    cfg.typeChosen = true;
                }
                else if (ArgEquals(a, L"-n"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.count) || cfg.count < 1)
                    {
                        return false;
                    }
                }
                else if (ArgEquals(a, L"-s"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.seconds) || cfg.seconds < 1)
                    {
                        return false;
                    }
                }
                else if (ArgEquals(a, L"-c"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.cpuThreshold))
                    {
                        return false;
                    }
                    cfg.cpuAbove = true;
                }
                else if (ArgEquals(a, L"-cl"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.cpuThreshold))
                    {
                        return false;
                    }
                    cfg.cpuBelow = true;
                }
                else if (ArgEquals(a, L"-m"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.commitThresholdMb))
                    {
                        return false;
                    }
                    cfg.memAbove = true;
                }
                else if (ArgEquals(a, L"-ml"))
                {
                    if (i + 1 >= argc || !ParseInt(argv[++i], cfg.commitThresholdMb))
                    {
                        return false;
                    }
                    cfg.memBelow = true;
                }
                else if (ArgEquals(a, L"-e"))
                {
                    cfg.onException = true;
                }
                else if (ArgEquals(a, L"-b"))
                {
                    cfg.breakpoints = true;
                }
                else if (ArgEquals(a, L"-h"))
                {
                    cfg.hung = true;
                }
                else if (ArgEquals(a, L"-t"))
                {
                    cfg.terminate = true;
                }
                else if (ArgEquals(a, L"-u"))
                {
                    cfg.singleCore = true;
                }
                else if (ArgEquals(a, L"-o"))
                {
                    cfg.overwrite = true;
                }
                else if (ArgEquals(a, L"-w"))
                {
                    cfg.wait = true;
                }
                else
                {
                    return false;
                }
            }
            else if (a[0] == L'1' && a[1] == L'\0' && cfg.onException)
            {
                cfg.firstChance = true;
            }
            else
            {
                if (cfg.target.empty())
                {
                    cfg.target = a;
                }
                else if (!cfg.hasDumpArg)
                {
                    cfg.dumpArg = a;
                    cfg.hasDumpArg = true;
                }
                else
                {
                    return false;
                }
            }
        }
        return !cfg.target.empty();
    }
} // namespace procdump
