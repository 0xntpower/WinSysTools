// PsList command-line parse: the switch set, the name or PID filter, and the keep
// predicate. See analysis/PsList.md for the switch reference.
#pragma once

#include "ProcessEnum.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace pslist
{
    [[nodiscard]] inline bool IsAllDigits(const std::string& s)
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

    [[nodiscard]] inline bool NameMatches(const std::string& name, const std::string& filter, bool exact)
    {
        if (exact)
        {
            return ::_stricmp(name.c_str(), filter.c_str()) == 0;
        }
        return ::_strnicmp(name.c_str(), filter.c_str(), filter.size()) == 0;
    }

    struct Options
    {
        bool threadDetail = false; // -d
        bool memoryDetail = false; // -m
        bool everything = false;   // -x
        bool tree = false;         // -t
        bool taskMode = false;     // -s
        bool exact = false;        // -e
        int taskSeconds = 0;       // -s n
        int refreshRate = 1;       // -r n
        bool hasFilter = false;
        bool filterIsPid = false;
        DWORD filterPid = 0;
        std::string filterName;
        std::string computer; // without the leading backslashes
        std::string user;
        std::string password;
        bool haveUser = false;
        bool havePassword = false;
    };

    // Parse PsList's own switches. Returns false when the caller should print usage. The order follows the
    // original: switches start with '-' or '/', a '\\' prefix names a computer, a trailing token is the
    // name or pid filter.
    [[nodiscard]] inline bool ParseArgs(int argc, char** argv, Options& o)
    {
        for (int i = 1; i < argc; ++i)
        {
            const char* a = argv[i];
            if ((a[0] == '-' || a[0] == '/') && a[1] != '\0')
            {
                const char c = static_cast<char>(::tolower(static_cast<unsigned char>(a[1])));
                if (a[2] != '\0' && c != 'u' && c != 'p' && c != 'r' && c != 's')
                {
                    return false;
                }
                switch (c)
                {
                case 'd':
                    o.threadDetail = true;
                    break;
                case 'm':
                    o.memoryDetail = true;
                    break;
                case 'x':
                    o.everything = true;
                    break;
                case 't':
                    o.tree = true;
                    break;
                case 'e':
                    o.exact = true;
                    break;
                case 's':
                    o.taskMode = true;
                    if (i + 1 < argc && IsAllDigits(argv[i + 1]))
                    {
                        o.taskSeconds = std::atoi(argv[++i]);
                    }
                    break;
                case 'r':
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    o.refreshRate = std::atoi(argv[++i]);
                    break;
                case 'u':
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    o.user = argv[++i];
                    o.haveUser = true;
                    break;
                case 'p':
                    if (i + 1 >= argc)
                    {
                        return false;
                    }
                    o.password = argv[++i];
                    o.havePassword = true;
                    break;
                default:
                    return false;
                }
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                o.computer = a + 2;
            }
            else
            {
                if (o.hasFilter)
                {
                    return false;
                }
                o.hasFilter = true;
                o.filterName = a;
                if (IsAllDigits(o.filterName))
                {
                    o.filterIsPid = true;
                    o.filterPid = static_cast<DWORD>(std::strtoul(o.filterName.c_str(), nullptr, 10));
                }
            }
        }
        if (o.everything)
        {
            o.memoryDetail = true;
            o.threadDetail = true;
        }
        return true;
    }

    [[nodiscard]] inline bool Keep(const Process& p, const Options& o)
    {
        if (!o.hasFilter)
        {
            return true;
        }
        if (o.filterIsPid)
        {
            return p.pid == o.filterPid;
        }
        return NameMatches(p.name, o.filterName, o.exact);
    }
} // namespace pslist
