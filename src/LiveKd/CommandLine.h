// LiveKd argument parsing (main): the switch grammar that fills Config, the -mp
// target-process resolution (sub_14000B9D0), and the pass-through of every other
// token to the debugger command line. See analysis/LiveKd.md.
#pragma once

#include "LiveKdCommon.h"

namespace livekd
{
    // Process resolution for -mp (sub_14000B9D0). Accepts a PID or an image name.
    [[nodiscard]] inline bool AllDigits(const std::string& s) noexcept
    {
        if (s.empty())
        {
            return false;
        }
        for (const char c : s)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] inline HANDLE OpenTargetProcess(const std::string& token)
    {
        DWORD pid = 0;
        if (AllDigits(token))
        {
            pid = static_cast<DWORD>(std::strtoul(token.c_str(), nullptr, 10));
        }
        else
        {
            ScopedHandle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
            if (snap.Valid())
            {
                PROCESSENTRY32W entry{};
                entry.dwSize = sizeof(entry);
                if (Process32FirstW(snap.Get(), &entry))
                {
                    const std::string wantExe = token + ".exe";
                    do
                    {
                        const std::string name = Narrow(entry.szExeFile);
                        if (_stricmp(name.c_str(), token.c_str()) == 0 || _stricmp(name.c_str(), wantExe.c_str()) == 0)
                        {
                            pid = entry.th32ProcessID;
                            break;
                        }
                    } while (Process32NextW(snap.Get(), &entry));
                }
            }
        }
        if (pid == 0)
        {
            return nullptr;
        }
        return OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    }

    // Argument parsing (main). Switch values follow their switch, unknown tokens
    // pass through to the debugger.
    [[nodiscard]] inline bool ParseArgs(int argc, wchar_t** argv, Config& cfg, HANDLE& processHandle, bool& wantUsage)
    {
        for (int i = 1; i < argc; ++i)
        {
            const wchar_t* a = argv[i];
            if (ArgEquals(a, L"-?"))
            {
                wantUsage = true;
                return true;
            }
            else if (ArgEquals(a, L"-accepteula"))
            {
                // Accepted before parsing. No effect here.
                continue;
            }
            else if (ArgEquals(a, L"-w"))
            {
                cfg.useWindbg = true;
                cfg.debuggerBase = "windbg";
            }
            else if (ArgEquals(a, L"-b"))
            {
                cfg.batch = true;
            }
            else if (ArgEquals(a, L"-vsym"))
            {
                cfg.verboseSymbols = true;
            }
            else if (ArgEquals(a, L"-ml"))
            {
                cfg.nativeLive = true;
            }
            else if (ArgEquals(a, L"-hvd"))
            {
                cfg.hypervisorPages = true;
            }
            else if (ArgEquals(a, L"-md"))
            {
                cfg.dumpStorageStack = true;
            }
            else if (ArgEquals(a, L"-o"))
            {
                if (++i >= argc)
                {
                    wantUsage = true;
                    return true;
                }
                char full[MAX_PATH] = {};
                const std::string arg = Narrow(argv[i]);
                if (GetFullPathNameA(arg.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr) != 0)
                {
                    cfg.dumpFile = full;
                }
                else
                {
                    cfg.dumpFile = arg;
                }
                cfg.dumpToFile = true;
            }
            else if (ArgEquals(a, L"-k"))
            {
                if (++i >= argc)
                {
                    wantUsage = true;
                    return true;
                }
                cfg.debuggerPath = Narrow(argv[i]);
                const std::string::size_type slash = cfg.debuggerPath.find_last_of("\\/");
                cfg.debuggerBase = (slash == std::string::npos) ? cfg.debuggerPath : cfg.debuggerPath.substr(slash + 1);
            }
            else if (ArgEquals(a, L"-mp"))
            {
                if (++i >= argc)
                {
                    wantUsage = true;
                    return true;
                }
                const std::string token = Narrow(argv[i]);
                processHandle = OpenTargetProcess(token);
                if (processHandle == nullptr)
                {
                    std::fprintf(stdout, "Unable to open process '%s'.\n", token.c_str());
                    return false;
                }
            }
            else if (ArgEquals(a, L"-y"))
            {
                if (++i >= argc)
                {
                    wantUsage = true;
                    return true;
                }
                SetEnvironmentVariableA("_NT_SYMBOL_PATH", Narrow(argv[i]).c_str());
            }
            else
            {
                // Pass every other token through to the debugger command line.
                if (!cfg.passthrough.empty())
                {
                    cfg.passthrough += ' ';
                }
                cfg.passthrough += Narrow(a);
            }
        }
        return true;
    }
} // namespace livekd
