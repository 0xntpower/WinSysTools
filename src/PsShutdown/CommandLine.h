// PsShutdown command-line parse: the parsed Options and the switch grammar
// (mirrors sub_140005500, including the -e reason and -t clock sub-parses).
#pragma once

#include "ShutdownCommon.h"

namespace psshutdown
{
    struct Options
    {
        DWORD flags = 0;            // action and modifier bits
        DWORD reason = 0;           // -e folded reason code
        int countdown = 0;          // -t seconds, negative encodes an absolute time
        int displaySeconds = -1;    // -v notification seconds, -1 shows the dialog
        int connectTimeout = -1;    // -n remote connect timeout
        bool allowAbort = false;    // -c lets the interactive user abort
        bool haveCountdown = false; // -t was given
        bool haveMessage = false;   // -m was given
        std::string message;        // -m value
        std::string computer;       // target list, empty means the local machine
        std::string user;           // -u value
        std::string password;       // -p value or prompted
        bool haveComputer = false;
        bool haveUser = false;
        bool havePassword = false;
    };

    // Fold a validated -e token (already uppercased, form "U:major:minor" or
    // "P:major:minor") into the reason word. Mirrors sub_140005500 case 'E'.
    [[nodiscard]] inline bool ParseReason(const char* token, DWORD& reason)
    {
        char type = 0;
        int major = 0;
        int minor = 0;
        if (std::sscanf(token, "%c:%d:%d", &type, &major, &minor) != 3)
        {
            return false;
        }
        const DWORD planned = (type == 'U') ? 0 : kReasonPlanned;
        reason = static_cast<DWORD>(minor) | planned | ((static_cast<DWORD>(major) | kReasonMajorBias) << 16);
        return true;
    }

    // Convert a -t "h:m" token to the negative seconds encoding the original uses
    // for an absolute shutdown time. Mirrors the colon branch of case 'T'.
    [[nodiscard]] inline bool ParseClockCountdown(const char* token, int& countdown)
    {
        int hour = 0;
        int minute = 0;
        if (std::sscanf(token, "%d:%d", &hour, &minute) != 2)
        {
            return false;
        }
        SYSTEMTIME now = {};
        ::GetLocalTime(&now);
        if (hour < now.wHour || (hour == now.wHour && minute < now.wMinute))
        {
            hour += 24;
        }
        countdown = 60 * (-minute - 60 * hour);
        return true;
    }

    // Parse PsShutdown's own argument grammar, after -nobanner and -accepteula have
    // been stripped. Returns false when the caller should print usage. Mirrors
    // sub_140005500: a leading '-' or '/' introduces a run of switches, a single
    // '\\' prefixes a computer target, '@' names a response file, and value
    // switches (-e, -m, -n, -p, -t, -u, -v) consume the next argument.
    [[nodiscard]] inline bool Parse(int argc, char** argv, Options& o)
    {
        if (argc <= 1)
        {
            return false;
        }

        for (int i = 1; i < argc; ++i)
        {
            char* a = argv[i];
            if (a[0] != '-' && a[0] != '/')
            {
                if (a[0] == '\\')
                {
                    if (a[1] != '\\' || o.haveComputer)
                    {
                        return false;
                    }
                    o.computer = a + 2;
                }
                else if (a[0] == '@')
                {
                    if (o.haveComputer)
                    {
                        return false;
                    }
                    o.computer = a; // keep the '@' so ForEachComputer sees it
                }
                else
                {
                    return false;
                }
                o.haveComputer = true;
                continue;
            }

            if (a[1] == '\0')
            {
                continue; // a bare '-' is ignored, as in the original
            }

            bool consumedNext = false;
            for (int j = 1; a[j] != '\0' && !consumedNext; ++j)
            {
                const char c = static_cast<char>(::toupper(static_cast<unsigned char>(a[j])));
                const bool haveNext = (i + 1) < argc;
                switch (c)
                {
                case 'A':
                    if (o.flags & kAbort)
                    {
                        return false;
                    }
                    o.flags |= kAbort;
                    break;
                case 'C':
                    o.allowAbort = true;
                    break;
                case 'D':
                    if (o.flags & kSuspend)
                    {
                        return false;
                    }
                    o.flags |= kSuspend;
                    break;
                case 'F':
                    if (o.flags & kForce)
                    {
                        return false;
                    }
                    o.flags |= kForce;
                    break;
                case 'H':
                    if (o.flags & kHibernate)
                    {
                        return false;
                    }
                    o.flags |= kHibernate;
                    break;
                case 'K':
                    if (o.flags & kPoweroff)
                    {
                        return false;
                    }
                    o.flags |= kPoweroff;
                    break;
                case 'L':
                    if (o.flags & kLock)
                    {
                        return false;
                    }
                    o.flags |= kLock;
                    break;
                case 'O':
                    if (o.flags & kLogoff)
                    {
                        return false;
                    }
                    o.flags |= kLogoff;
                    break;
                case 'R':
                    if (o.flags & kReboot)
                    {
                        return false;
                    }
                    o.flags |= kReboot;
                    break;
                case 'S':
                    if (o.flags & kShutdown)
                    {
                        return false;
                    }
                    o.flags |= kShutdown;
                    break;
                case 'X':
                    if (o.flags & kMonitor)
                    {
                        return false;
                    }
                    o.flags |= kMonitor;
                    break;
                case 'E':
                {
                    if (!haveNext || o.reason != 0)
                    {
                        return false;
                    }
                    char* token = argv[++i];
                    consumedNext = true;
                    ::_strupr(token);
                    if (token[0] != 'U' && token[0] != 'P')
                    {
                        return false;
                    }
                    if (!ParseReason(token, o.reason))
                    {
                        return false;
                    }
                    break;
                }
                case 'M':
                    if (!haveNext || o.haveMessage)
                    {
                        return false;
                    }
                    o.message = argv[++i];
                    o.haveMessage = true;
                    consumedNext = true;
                    break;
                case 'N':
                {
                    if (!haveNext || o.connectTimeout != -1)
                    {
                        return false;
                    }
                    if (std::sscanf(argv[++i], "%d", &o.connectTimeout) == 0)
                    {
                        return false;
                    }
                    consumedNext = true;
                    break;
                }
                case 'V':
                {
                    if (!haveNext || o.displaySeconds != -1)
                    {
                        return false;
                    }
                    if (std::sscanf(argv[++i], "%d", &o.displaySeconds) == 0)
                    {
                        return false;
                    }
                    consumedNext = true;
                    break;
                }
                case 'P':
                    if (!haveNext || o.havePassword)
                    {
                        return false;
                    }
                    o.password = argv[++i];
                    o.havePassword = true;
                    consumedNext = true;
                    break;
                case 'U':
                    if (!haveNext || o.haveUser)
                    {
                        return false;
                    }
                    o.user = argv[++i];
                    o.haveUser = true;
                    consumedNext = true;
                    break;
                case 'T':
                {
                    if (!haveNext)
                    {
                        return false;
                    }
                    const char* token = argv[++i];
                    consumedNext = true;
                    if (std::strchr(token, ':') != nullptr)
                    {
                        if (!ParseClockCountdown(token, o.countdown))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        o.countdown = std::atoi(token);
                    }
                    o.haveCountdown = true;
                    break;
                }
                default:
                    return false;
                }
            }
        }

        int actionCount = 0;
        for (DWORD bit = 1; bit <= kMonitor; bit <<= 1)
        {
            if ((bit & kActionMask) != 0 && (o.flags & bit) != 0)
            {
                ++actionCount;
            }
        }
        if (actionCount != 1)
        {
            if (actionCount != 0)
            {
                return false;
            }
            o.flags |= kPoweroff; // no action named defaults to poweroff
        }

        if ((o.flags & kNoServiceMask) != 0)
        {
            if (o.reason != 0 || o.countdown != 0 || o.haveMessage || (o.flags & (kForce | kLogoff)) == kForce)
            {
                return false; // force is only allowed with logoff in this group
            }
        }
        else if (!o.haveCountdown)
        {
            o.countdown = kDefaultCountdown;
        }
        return true;
    }
} // namespace psshutdown
