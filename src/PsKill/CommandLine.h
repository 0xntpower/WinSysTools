// PsKill command-line parse (sub_140004B20): one optional computer, one
// PID-or-name target, the tree flag, and the alternate-credential fields. PsKill
// parses its own grammar rather than the shared target-list parser, because it
// accepts a single computer plus one target, not the comma or response-file lists
// the rest of the suite take. See analysis/PsKill.md for the sub_ map.
#pragma once

#include "PsKillCommon.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace pskill
{
    // Parsed command line. A single optional computer, one PID-or-name target,
    // the tree flag, and the shared alternate-credential fields.
    struct Options
    {
        std::string computer;       // the \\computer argument without the backslashes
        const char* name = nullptr; // the process-name target, or null for a PID
        DWORD pid = 0;              // the PID target when name is null
        bool tree = false;          // -t
        bool computerSet = false;
        bool targetSet = false;
        std::string user;     // -u value
        std::string password; // -p value (or prompted)
        bool haveUser = false;
        bool havePassword = false;
    };

    [[nodiscard]] inline bool IsAllDigits(const char* s)
    {
        if (s[0] == '\0')
        {
            return false;
        }
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (std::isdigit(static_cast<unsigned char>(*p)) == 0)
            {
                return false;
            }
        }
        return true;
    }

    // Parse the PsKill command line. Mirrors sub_140004B20. Returns false when the
    // caller should print usage and fail.
    [[nodiscard]] inline bool ParseCommandLine(int argc, char** argv, Options& o)
    {
        if (argc <= 1)
        {
            return false;
        }
        for (int i = 1; i < argc; ++i)
        {
            char* arg = argv[i];
            if (arg[0] == '-' || arg[0] == '/')
            {
                if (arg[1] == '\0')
                {
                    continue; // a bare '-' is ignored, as in the original
                }
                for (int j = 1; arg[j] != '\0'; ++j)
                {
                    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(arg[j])));
                    if (c == 'T')
                    {
                        o.tree = true;
                    }
                    else if (c == 'U')
                    {
                        if (i + 1 >= argc || o.haveUser)
                        {
                            return false;
                        }
                        o.user = argv[++i];
                        o.haveUser = true;
                        break;
                    }
                    else if (c == 'P')
                    {
                        if (i + 1 >= argc || o.havePassword)
                        {
                            return false;
                        }
                        o.password = argv[++i];
                        o.havePassword = true;
                        break;
                    }
                    else
                    {
                        return false;
                    }
                }
            }
            else if (IsAllDigits(arg))
            {
                if (o.targetSet)
                {
                    return false;
                }
                o.pid = static_cast<DWORD>(std::strtoul(arg, nullptr, 10));
                o.name = nullptr;
                o.targetSet = true;
            }
            else if (arg[0] == '\\' && arg[1] == '\\')
            {
                if (o.computerSet)
                {
                    return false;
                }
                o.computer = arg + 2;
                o.computerSet = true;
            }
            else
            {
                if (o.targetSet)
                {
                    return false;
                }
                o.name = arg;
                o.targetSet = true;
            }
        }

        if (!o.targetSet)
        {
            return false;
        }
        if ((o.haveUser && !o.computerSet) || (o.havePassword && !o.haveUser))
        {
            return false;
        }
        return true;
    }
} // namespace pskill
