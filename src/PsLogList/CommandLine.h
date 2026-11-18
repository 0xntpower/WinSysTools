// PsLogList command-line parse: the digit and id-list predicates, the log-name
// canonicalizer, the -d/-h/-m cutoff and -a/-b date parsing, and the switch grammar
// (mirrors sub_1400077B0).
#pragma once

#include "LogListUtil.h"

namespace psloglist
{
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

    [[nodiscard]] inline bool IsDigitsAndCommas(const char* s)
    {
        for (const char* p = s; *p != '\0'; ++p)
        {
            if (std::isdigit(static_cast<unsigned char>(*p)) == 0 && *p != ',')
            {
                return false;
            }
        }
        return true;
    }

    // Parse a comma-separated id list into out, capped at kMaxIdList entries, which
    // mirrors the original's ten-field sscanf.
    inline void ParseIdList(const char* s, std::vector<int>& out)
    {
        const char* p = s;
        while (*p != '\0' && static_cast<int>(out.size()) < kMaxIdList)
        {
            char* end = nullptr;
            const long value = std::strtol(p, &end, 10);
            if (end == p)
            {
                break;
            }
            out.push_back(static_cast<int>(value));
            p = end;
            if (*p == ',')
            {
                ++p;
            }
        }
    }

    // Canonicalize the log name the way the parser does: a case-insensitive prefix
    // of application, system, or security maps to the proper-cased built-in name,
    // otherwise the argument is kept as given.
    [[nodiscard]] inline std::string CanonicalLogName(const char* a)
    {
        const size_t n = std::strlen(a);
        if (n != 0 && ::_strnicmp(a, "application", n) == 0)
        {
            return "Application";
        }
        if (n != 0 && ::_strnicmp(a, "system", n) == 0)
        {
            return "System";
        }
        if (n != 0 && ::_strnicmp(a, "security", n) == 0)
        {
            return "Security";
        }
        return a;
    }

    // Compute the -d / -h / -m cutoff FILETIME from "now", matching the parser.
    [[nodiscard]] inline FILETIME ComputeCutoff(long long ticksBack, bool truncateToMidnight)
    {
        FILETIME now = {};
        ::GetSystemTimeAsFileTime(&now);
        FILETIME local = {};
        ::FileTimeToLocalFileTime(&now, &local);
        unsigned long long value = (static_cast<unsigned long long>(local.dwHighDateTime) << 32) | local.dwLowDateTime;
        value -= static_cast<unsigned long long>(ticksBack);
        FILETIME cutoff = {};
        cutoff.dwLowDateTime = static_cast<DWORD>(value);
        cutoff.dwHighDateTime = static_cast<DWORD>(value >> 32);
        if (truncateToMidnight)
        {
            SYSTEMTIME systemTime = {};
            ::FileTimeToSystemTime(&cutoff, &systemTime);
            systemTime.wHour = 0;
            systemTime.wMinute = 0;
            systemTime.wSecond = 0;
            systemTime.wMilliseconds = 0;
            ::SystemTimeToFileTime(&systemTime, &cutoff);
        }
        return cutoff;
    }

    // Parse an mm/dd/yy date into a local FILETIME, matching the parser's year rule.
    [[nodiscard]] inline bool ParseDate(const char* text, FILETIME& out)
    {
        int month = 0;
        int day = 0;
        int year = 0;
        if (std::sscanf(text, "%d/%d/%d", &month, &day, &year) != 3)
        {
            return false;
        }
        if (year >= 90)
        {
            if (static_cast<unsigned>(year - 91) <= 8)
            {
                year += 1900;
            }
        }
        else
        {
            year += 2000;
        }
        SYSTEMTIME systemTime = {};
        systemTime.wMonth = static_cast<WORD>(month);
        systemTime.wDay = static_cast<WORD>(day);
        systemTime.wYear = static_cast<WORD>(year);
        return ::SystemTimeToFileTime(&systemTime, &out) != 0;
    }

    // Parse the PsLogList command line. Mirrors sub_1400077B0 for the record-read,
    // message-format, and record-selection switches. The event-file, wait, and
    // list-logs modes (-g, -l, -w, -z) are out of scope and reported through usage.
    [[nodiscard]] inline bool ParseCommandLine(int argc, char** argv, Options& o)
    {
        bool haveComputer = false;
        bool haveLog = false;

        for (int i = 1; i < argc; ++i)
        {
            const char* arg = argv[i];
            if (arg[0] == '-' || arg[0] == '/')
            {
                if (arg[1] == '\0')
                {
                    continue;
                }
                bool consumedNext = false;
                for (int j = 1; arg[j] != '\0' && !consumedNext; ++j)
                {
                    const int c = std::toupper(static_cast<unsigned char>(arg[j]));
                    switch (c)
                    {
                    case 'S':
                        if (o.oneLine)
                        {
                            return false;
                        }
                        o.oneLine = true;
                        break;
                    case 'R':
                        o.oldestFirst = true;
                        break;
                    case 'C':
                        o.clear = true;
                        break;
                    case 'X':
                        o.extended = true;
                        break;
                    case 'T':
                        if (!o.oneLine || o.haveDelim || i + 1 >= argc)
                        {
                            return false;
                        }
                        o.haveDelim = true;
                        if (::_stricmp(argv[i + 1], "\\t") == 0)
                        {
                            o.delimiter = '\t';
                        }
                        else
                        {
                            o.delimiter = argv[i + 1][0];
                        }
                        ++i;
                        consumedNext = true;
                        break;
                    case 'N':
                        if (i + 1 >= argc || o.maxRecords != -1 || !IsAllDigits(argv[i + 1]))
                        {
                            return false;
                        }
                        o.maxRecords = std::atoi(argv[i + 1]);
                        ++i;
                        consumedNext = true;
                        break;
                    case 'D':
                        if (i + 1 >= argc || o.cutoffSet || !IsAllDigits(argv[i + 1]))
                        {
                            return false;
                        }
                        o.cutoff = ComputeCutoff(kTicksPerDay * std::atoi(argv[i + 1]), true);
                        o.cutoffSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'H':
                        if (i + 1 >= argc || o.cutoffSet || !IsAllDigits(argv[i + 1]))
                        {
                            return false;
                        }
                        o.cutoff = ComputeCutoff(kTicksPerHour * std::atoi(argv[i + 1]), false);
                        o.cutoffSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'M':
                        if (i + 1 >= argc || o.cutoffSet || !IsAllDigits(argv[i + 1]))
                        {
                            return false;
                        }
                        o.cutoff = ComputeCutoff(kTicksPerMinute * std::atoi(argv[i + 1]), false);
                        o.cutoffSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'I':
                        if (i + 1 >= argc || !o.includeIds.empty() || !o.excludeIds.empty() ||
                            !IsDigitsAndCommas(argv[i + 1]))
                        {
                            return false;
                        }
                        ParseIdList(argv[i + 1], o.includeIds);
                        ++i;
                        consumedNext = true;
                        break;
                    case 'E':
                        if (i + 1 >= argc || !o.includeIds.empty() || !o.excludeIds.empty() ||
                            !IsDigitsAndCommas(argv[i + 1]))
                        {
                            return false;
                        }
                        ParseIdList(argv[i + 1], o.excludeIds);
                        ++i;
                        consumedNext = true;
                        break;
                    case 'O':
                        if (i + 1 >= argc || o.haveSourceInclude)
                        {
                            return false;
                        }
                        o.sourceInclude = argv[i + 1];
                        o.haveSourceInclude = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'Q':
                        if (i + 1 >= argc || o.haveSourceExclude)
                        {
                            return false;
                        }
                        o.sourceExclude = argv[i + 1];
                        o.haveSourceExclude = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'F':
                        if (i + 1 >= argc || !o.typeFilter.empty())
                        {
                            return false;
                        }
                        o.typeFilter = argv[i + 1];
                        std::transform(o.typeFilter.begin(), o.typeFilter.end(), o.typeFilter.begin(),
                                       [](unsigned char ch)
                                       {
                                           return static_cast<char>(std::toupper(ch));
                                       });
                        ++i;
                        consumedNext = true;
                        break;
                    case 'A':
                        if (i + 1 >= argc || o.afterSet || !ParseDate(argv[i + 1], o.after))
                        {
                            return false;
                        }
                        o.afterSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'B':
                        if (i + 1 >= argc || o.beforeSet || !ParseDate(argv[i + 1], o.before))
                        {
                            return false;
                        }
                        o.beforeSet = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'U':
                        if (i + 1 >= argc || o.haveUser)
                        {
                            return false;
                        }
                        o.user = argv[i + 1];
                        o.haveUser = true;
                        ++i;
                        consumedNext = true;
                        break;
                    case 'P':
                        if (i + 1 >= argc || o.havePassword)
                        {
                            return false;
                        }
                        o.password = argv[i + 1];
                        o.havePassword = true;
                        ++i;
                        consumedNext = true;
                        break;
                    default:
                        return false;
                    }
                }
            }
            else if (arg[0] == '@')
            {
                if (haveComputer)
                {
                    return false;
                }
                o.computerList = arg;
                haveComputer = true;
                o.computerSpecified = true;
            }
            else if (arg[0] == '\\' && arg[1] == '\\')
            {
                if (haveComputer)
                {
                    return false;
                }
                o.computerList = arg + 2;
                haveComputer = true;
                o.computerSpecified = true;
            }
            else
            {
                if (haveLog)
                {
                    return false;
                }
                o.logName = CanonicalLogName(arg);
                haveLog = true;
            }
        }

        if (o.haveUser && !o.computerSpecified)
        {
            return false;
        }
        if (o.havePassword && !o.haveUser)
        {
            return false;
        }
        if (o.afterSet && o.beforeSet && ::CompareFileTime(&o.after, &o.before) > 0)
        {
            return false;
        }
        return true;
    }
} // namespace psloglist
