// PsService command-line parse: the command name to code map and the
// machine/credential/argument split (mirrors sub_140004E10).
#pragma once

#include "ServiceUtil.h"

namespace psservice
{
    [[nodiscard]] inline int CommandCode(const wchar_t* a)
    {
        if (IEq(a, L"query"))
        {
            return 1;
        }
        if (IEq(a, L"queryeex") || IEq(a, L"interrogate"))
        {
            return 2;
        }
        if (IEq(a, L"restart"))
        {
            return 10;
        }
        if (IEq(a, L"start"))
        {
            return 4;
        }
        if (IEq(a, L"stop"))
        {
            return 3;
        }
        if (IEq(a, L"pause"))
        {
            return 5;
        }
        if (IEq(a, L"setconfig"))
        {
            return 11;
        }
        if (IEq(a, L"cont") || IEq(a, L"continue"))
        {
            return 6;
        }
        if (IEq(a, L"sec") || IEq(a, L"security"))
        {
            return 12;
        }
        if (IEq(a, L"config"))
        {
            return 7;
        }
        if (IEq(a, L"depend") || IEq(a, L"enumdepend"))
        {
            return 8;
        }
        if (IEq(a, L"find"))
        {
            return 9;
        }
        return 0;
    }

    struct CommandLine
    {
        std::wstring machine;
        std::wstring user;
        std::wstring password;
        bool haveMachine = false;
        bool haveUser = false;
        bool havePassword = false;
        int code = 1;
        std::vector<std::wstring> args;
    };

    [[nodiscard]] inline bool ParseCommandLine(int argc, wchar_t** argv, CommandLine& cmd)
    {
        for (int i = 1; i < argc; ++i)
        {
            const wchar_t* a = argv[i];
            const int code = CommandCode(a);
            if (code != 0)
            {
                cmd.code = code;
                for (int j = i + 1; j < argc; ++j)
                {
                    cmd.args.emplace_back(argv[j]);
                }
                return !(cmd.havePassword && !cmd.haveUser);
            }
            if (IEq(a, L"-u"))
            {
                if (i + 1 >= argc || cmd.haveUser)
                {
                    return false;
                }
                cmd.user = argv[++i];
                cmd.haveUser = true;
            }
            else if (IEq(a, L"-p"))
            {
                if (i + 1 >= argc || cmd.havePassword || !cmd.haveUser)
                {
                    return false;
                }
                cmd.password = argv[++i];
                cmd.havePassword = true;
            }
            else if (a[0] == L'\\' && a[1] == L'\\' && !cmd.haveMachine)
            {
                cmd.machine = a;
                cmd.haveMachine = true;
            }
            else if ((a[0] == L'-' || a[0] == L'/') && a[1] == L'\0')
            {
                continue;
            }
            else
            {
                return false;
            }
        }
        return !(cmd.havePassword && !cmd.haveUser);
    }
} // namespace psservice
