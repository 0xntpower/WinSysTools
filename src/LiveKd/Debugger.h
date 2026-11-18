// LiveKd debugger discovery and launch: the install-location search
// (sub_14000B7D0 / sub_14000B570), the _NT_SYMBOL_PATH setup (the symbol half of
// sub_14000AAD0), and the debugger launch and re-run loop (main). See
// analysis/LiveKd.md.
#pragma once

#include "LiveDump.h"

namespace livekd
{
    // Debugger discovery (sub_14000B7D0 / sub_14000B570). Walk the known install
    // locations, setting each as the current directory, then SearchPath for the
    // debugger image. The registry root under Windows Kits is the modern location.
    [[nodiscard]] inline bool SearchDebugger(const std::string& exeName, std::string& fullPath)
    {
        char buffer[MAX_PATH] = {};
        LPSTR filePart = nullptr;
        const DWORD n =
            SearchPathA(nullptr, exeName.c_str(), nullptr, static_cast<DWORD>(std::size(buffer)), buffer, &filePart);
        if (n == 0 || n >= std::size(buffer))
        {
            return false;
        }
        fullPath = buffer;
        return true;
    }

    inline void SetDirFromEnv(const char* expand)
    {
        char dst[MAX_PATH] = {};
        const DWORD n = ExpandEnvironmentStringsA(expand, dst, static_cast<DWORD>(std::size(dst)));
        if (n != 0 && n <= std::size(dst))
        {
            SetCurrentDirectoryA(dst);
        }
    }

    [[nodiscard]] inline bool DiscoverDebugger(const std::string& exeName, std::string& fullPath)
    {
        SetDirFromEnv("%ProgramFiles%\\Debugging Tools for Windows (x64)");
        if (SearchDebugger(exeName, fullPath))
        {
            return true;
        }
        SetDirFromEnv("%ProgramFiles%\\Debugging Tools for Windows 64-bit");
        if (SearchDebugger(exeName, fullPath))
        {
            return true;
        }
        SetCurrentDirectoryA("C:\\debuggers");
        if (SearchDebugger(exeName, fullPath))
        {
            return true;
        }

        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", 0,
                          KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS)
        {
            char root[MAX_PATH] = {};
            DWORD cb = sizeof(root);
            DWORD type = 0;
            const LSTATUS st =
                RegQueryValueExA(key, "WindowsDebuggersRoot", nullptr, &type, reinterpret_cast<LPBYTE>(root), &cb);
            RegCloseKey(key);
            if (st == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ))
            {
                std::string dir = root;
                dir += "\\x64";
                SetCurrentDirectoryA(dir.c_str());
                if (SearchDebugger(exeName, fullPath))
                {
                    return true;
                }
            }
        }
        return false;
    }

    // Symbol path setup (the symbol-configuration half of sub_14000AAD0). Verify
    // dbghelp is reachable, then, when _NT_SYMBOL_PATH is not configured, offer to
    // point it at the Microsoft symbol server so the launched debugger can resolve
    // symbols.
    [[nodiscard]] inline bool DbghelpPresent()
    {
        const HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dbghelp == nullptr)
        {
            return false;
        }
        const bool ok = GetProcAddress(dbghelp, "SymInitialize") != nullptr;
        FreeLibrary(dbghelp);
        return ok;
    }

    [[nodiscard]] inline bool EnsureSymbolPath()
    {
        if (!DbghelpPresent())
        {
            std::fputs("LiveKd requires Dbghelp.dll. Download the Debugging Tools package from\n", stdout);
            std::fputs("www.microsoft.com/whdc/ddk/debugging.\n\n", stdout);
            return false;
        }

        char existing[MAX_PATH] = {};
        if (GetEnvironmentVariableA("_NT_SYMBOL_PATH", existing, static_cast<DWORD>(std::size(existing))) != 0)
        {
            return true;
        }

        std::fputs("\nSymbols are not configured. Would you like LiveKd to set the _NT_SYMBOL_PATH\n", stdout);
        std::fputs("directory to reference the Microsoft symbol server so that symbols can be\n", stdout);
        std::fputs("obtained automatically? (y/n) ", stdout);
        std::fflush(stdout);
        const int answer = std::getchar();
        if ((answer & ~0x20) == 'N')
        {
            std::fputs("\nLivekd will now exit.\n\n", stdout);
            return false;
        }

        std::fputs("\nEnter the folder to which symbols download (default is c:\\symbols): ", stdout);
        std::fflush(stdout);
        char folder[MAX_PATH] = "c:\\Symbols";
        char line[MAX_PATH] = {};
        if (std::fgets(line, static_cast<int>(std::size(line)), stdin) != nullptr)
        {
            line[std::strcspn(line, "\r\n")] = '\0';
            if (line[0] != '\0')
            {
                std::snprintf(folder, std::size(folder), "%s", line);
            }
        }

        char sympath[MAX_PATH * 2] = {};
        std::snprintf(sympath, std::size(sympath), "srv*%s*http://msdl.microsoft.com/download/symbols", folder);
        SetEnvironmentVariableA("_NT_SYMBOL_PATH", sympath);

        HKEY env = nullptr;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_SET_VALUE, &env) == ERROR_SUCCESS)
        {
            RegSetValueExA(env, "_NT_SYMBOL_PATH", 0, REG_SZ, reinterpret_cast<const BYTE*>(sympath),
                           static_cast<DWORD>(std::strlen(sympath) + 1));
            RegCloseKey(env);
        }
        return true;
    }

    // Launch the debugger against a freshly written live dump, reproducing the
    // original's re-run loop (main). The live view is approximated by a consistent
    // kernel snapshot, which is the safe equivalent of the legacy driver's live
    // memory window.
    [[nodiscard]] inline int LaunchDebugger(const Config& cfg, const std::string& debuggerFull, HANDLE processHandle)
    {
        char windir[MAX_PATH] = {};
        GetWindowsDirectoryA(windir, static_cast<DWORD>(std::size(windir)));
        const std::string dumpPath = std::string(windir) + "\\" + kDefaultDumpName;

        // "Dumpchk" wants -e, every other debugger wants -z to open the dump.
        const char* openSwitch = _stricmp(cfg.debuggerBase.c_str(), "Dumpchk") == 0 ? " -e " : " -z ";
        const std::string exeToken = cfg.useWindbg ? "windbg.exe " : "kd.exe ";
        const std::string commandLine = exeToken + cfg.passthrough + openSwitch + dumpPath;

        for (;;)
        {
            if (!WriteLiveKernelDump(dumpPath, processHandle, false, cfg.hypervisorPages, cfg.dumpStorageStack))
            {
                return kExitError;
            }

            std::fprintf(stdout, "Launching %s:\n", debuggerFull.c_str());
            std::string mutableCmd = commandLine;
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (!CreateProcessA(debuggerFull.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                                &si, &pi))
            {
                const DWORD err = GetLastError();
                std::fprintf(stdout, "Error launching %s:\n", cfg.debuggerBase.c_str());
                std::fprintf(stdout, "Error %lu\n", err);
                DeleteFileA(dumpPath.c_str());
                return kExitError;
            }
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            DeleteFileA(dumpPath.c_str());

            if (cfg.batch)
            {
                break;
            }
            std::fprintf(stdout, "\nExecute %s again? (y/n) ", cfg.debuggerBase.c_str());
            std::fflush(stdout);
            char line[MAX_PATH] = {};
            if (std::fgets(line, static_cast<int>(std::size(line)), stdin) == nullptr)
            {
                break;
            }
            if (std::strchr(line, 'y') == nullptr && std::strchr(line, 'Y') == nullptr)
            {
                break;
            }
        }
        std::fputs("\nExiting LiveKd.\n\n", stdout);
        return 0;
    }
} // namespace livekd
