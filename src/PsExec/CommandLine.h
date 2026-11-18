// PsExec command-line parse into the Options record (mirrors sub_140014030). All
// diagnostics are printed here, matching the original where some errors print a
// dedicated message rather than the usage.
#pragma once

#include "PsExecCommon.h"
#include "Usage.h"

namespace psexec
{
    // Parse the PsExec command line into o. Returns false on any error. The
    // needsService output is set when a switch forces the PSEXESVC service path.
    [[nodiscard]] inline bool ParseCommandLine(int argc, char** argv, Options& o, bool& needsService)
    {
        if (argc <= 1)
        {
            PrintUsage();
            return false;
        }

        for (int i = 1; i < argc; ++i)
        {
            char* a = argv[i];
            if (a[0] == '-' || a[0] == '/')
            {
                const char* sw = a + 1;
                if (sw[0] == '\0')
                {
                    continue; // a bare dash is ignored, as in the original
                }
                if (::_stricmp(sw, "accepteula") == 0 || ::_stricmp(sw, "nobanner") == 0)
                {
                    continue; // already stripped before parsing, skipped defensively
                }

                // A whole-word priority name claims the argument.
                if (o.priorityClass == 0)
                {
                    bool matched = false;
                    for (const PriorityName& p : kPriorities)
                    {
                        if (::_stricmp(sw, p.name) == 0)
                        {
                            o.priorityClass = p.cls;
                            matched = true;
                            break;
                        }
                    }
                    if (matched)
                    {
                        continue;
                    }
                }

                if (::_stricmp(sw, "arm") == 0)
                {
                    o.arm = true;
                    continue;
                }

                // Per-character scan so combined flags such as -cf work. Flags that
                // take a value consume the next argument and end the scan.
                bool consumedNext = false;
                for (int j = 0; sw[j] != '\0' && !consumedNext; ++j)
                {
                    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(sw[j])));
                    switch (c)
                    {
                    case 'C':
                        o.copyProgram = true;
                        needsService = true; // copy deploys through the service
                        break;
                    case 'F':
                        o.forceCopy = true;
                        break;
                    case 'V':
                        o.copyIfNewer = true;
                        break;
                    case 'D':
                        o.dontWait = true;
                        break;
                    case 'E':
                        o.noProfile = true;
                        break;
                    case 'H':
                        o.elevated = true;
                        break;
                    case 'L':
                        o.limited = true;
                        break;
                    case 'S':
                        o.systemAccount = true;
                        needsService = true;
                        break;
                    case 'X':
                        o.secureDesktop = true;
                        o.interactive = true;
                        needsService = true;
                        break;
                    case 'U':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.user = argv[++i];
                        o.haveUser = true;
                        consumedNext = true;
                        break;
                    case 'P':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.password = argv[++i];
                        o.havePassword = true;
                        consumedNext = true;
                        break;
                    case 'R':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.serviceName = argv[++i];
                        consumedNext = true;
                        break;
                    case 'W':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.workDir = argv[++i];
                        o.haveWorkDir = true;
                        consumedNext = true;
                        break;
                    case 'N':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.timeoutSeconds = std::atoi(argv[++i]);
                        consumedNext = true;
                        break;
                    case 'G':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        o.group = static_cast<WORD>(std::atoi(argv[++i]));
                        o.groupSpecified = true;
                        consumedNext = true;
                        break;
                    case 'A':
                        if (i + 1 >= argc)
                        {
                            PrintUsage();
                            return false;
                        }
                        ParseAffinity(argv[++i], o.affinityMask);
                        consumedNext = true;
                        break;
                    case 'I':
                        o.interactive = true;
                        needsService = true;
                        if (i + 1 < argc && IsAllDigits(argv[i + 1]))
                        {
                            o.session = std::atoi(argv[++i]);
                            consumedNext = true;
                        }
                        break;
                    default:
                        PrintUsage();
                        return false;
                    }
                }
            }
            else if (a[0] == '@')
            {
                if (o.computerSpecified)
                {
                    PrintUsage();
                    return false;
                }
                o.computerList = a; // keep the '@' for ForEachComputer
                o.computerSpecified = true;
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                if (o.computerSpecified)
                {
                    PrintUsage();
                    return false;
                }
                o.computerList = a + 2; // the bare name, or "*" for the whole domain
                o.computerSpecified = true;
            }
            else
            {
                // The first bare token is the command. Everything after it is the
                // argument tail that belongs to the child, not to PsExec.
                o.app = a;
                o.appSpecified = true;
                std::string tail;
                for (int k = i + 1; k < argc; ++k)
                {
                    if (!tail.empty())
                    {
                        tail.push_back(' ');
                    }
                    tail += argv[k];
                }
                o.args = tail;
                break;
            }
        }

        if (o.limited && o.elevated)
        {
            std::fprintf(stderr, "Run elevated and limited user options are not compatible.\n\n");
            return false;
        }
        if (!o.appSpecified)
        {
            PrintUsage();
            return false;
        }
        if (o.havePassword && !o.haveUser)
        {
            PrintUsage();
            return false;
        }
        // -f and -v each require -c. The original accepts them together.
        if ((o.forceCopy || o.copyIfNewer) && !o.copyProgram)
        {
            PrintUsage();
            return false;
        }
        // -s forces the profile off, as the original does at the end of parsing.
        if (o.systemAccount)
        {
            o.noProfile = true;
        }
        return true;
    }
} // namespace psexec
