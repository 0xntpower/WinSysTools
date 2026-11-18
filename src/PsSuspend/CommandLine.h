// PsSuspend command-line parse: the Options the grammar fills and the switch,
// computer, PID, and name rules that mirror sub_140004FD0. kErrorInvalidParameter
// is shared with the engine and the reporting. See analysis/PsSuspend.md.
#pragma once

#include "../common/PsTools.h"

#include <charconv>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace pssuspend
{
    constexpr DWORD kErrorInvalidParameter = 87;

    struct Options
    {
        bool resume = false;  // -r, otherwise suspend
        bool byName = false;  // a process name was given rather than a PID
        std::string name;     // process name when byName
        int pid = 0;          // process id when not byName
        std::string computer; // \\computer, empty for local
        std::string user;     // -u value
        std::string password; // -p value or prompted
        bool haveProcess = false;
        bool haveComputer = false;
        bool haveUser = false;
        bool havePassword = false;
    };

    inline void PrintUsage()
    {
        std::printf("PsSuspend suspends or resumes processes on a local or remote NT system.\n");
        std::printf("\nUsage: pssuspend [-r] [\\\\RemoteComputer [-u Username [-p Password]]] <process Id or name>\n");
        std::printf("     -r    Resume.\n");
        std::printf("     -u    Specifies optional user name for login to\n           remote computer.\n");
        std::printf("     -p    Specifies optional password for user name. If you omit this\n");
        std::printf("           you will be prompted to enter a hidden password.\n");
        std::printf("     -nobanner Do not display the startup banner and copyright message.\n");
        std::printf("\n");
    }

    [[nodiscard]] inline bool AllDigits(const char* s)
    {
        if (*s == '\0')
        {
            return false;
        }
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (::isdigit(static_cast<unsigned char>(*p)) == 0)
            {
                return false;
            }
        }
        return true;
    }

    // Parse PsSuspend's own grammar, after -nobanner and -accepteula have been
    // stripped. Returns false when the caller should print usage. Mirrors
    // sub_140004FD0: a leading '-' or '/' is a switch (-r, -u value, -p value), a
    // '\\' prefixes a computer, an all-digit token is a PID, anything else is a
    // process name. -u needs a computer target and -p needs -u.
    [[nodiscard]] inline bool Parse(int argc, char** argv, Options& o)
    {
        for (int i = 1; i < argc; ++i)
        {
            const char* a = argv[i];
            if (a[0] == '-' || a[0] == '/')
            {
                if (a[1] == '\0')
                {
                    continue; // a bare '-' is ignored, as in the original
                }
                for (int j = 1; a[j] != '\0'; ++j)
                {
                    const char c = static_cast<char>(::tolower(static_cast<unsigned char>(a[j])));
                    if (c == 'r')
                    {
                        o.resume = true;
                        continue;
                    }
                    if (c == 'u')
                    {
                        if (i + 1 >= argc || o.haveUser)
                        {
                            return false;
                        }
                        o.user = argv[++i];
                        o.haveUser = true;
                        break;
                    }
                    if (c == 'p')
                    {
                        if (i + 1 >= argc || o.havePassword)
                        {
                            return false;
                        }
                        o.password = argv[++i];
                        o.havePassword = true;
                        break;
                    }
                    return false;
                }
            }
            else if (a[0] == '\\' && a[1] == '\\')
            {
                if (o.haveComputer)
                {
                    return false;
                }
                o.computer = a + 2;
                o.haveComputer = true;
            }
            else if (AllDigits(a))
            {
                if (o.haveProcess)
                {
                    return false;
                }
                int value = 0;
                std::from_chars(a, a + std::strlen(a), value);
                o.pid = value;
                o.byName = false;
                o.haveProcess = true;
            }
            else
            {
                if (o.haveProcess)
                {
                    return false;
                }
                o.name = a;
                o.byName = true;
                o.haveProcess = true;
            }
        }

        if (!o.haveProcess || (o.haveUser && !o.haveComputer) || (o.havePassword && !o.haveUser))
        {
            return false;
        }
        return true;
    }
} // namespace pssuspend
