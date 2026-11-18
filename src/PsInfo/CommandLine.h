// PsInfo tool-flag parse: strip -h -s -d -c -t from argv before the shared
// front end sees the rest (mirrors the tool-flag half of sub_140005E20).
#pragma once

#include "PsInfoUtil.h"

namespace psinfo
{
    // Remove the PsInfo-specific flags from argv, leaving the shared front-end
    // arguments for pstools::ParseCommandLine. Returns false to print usage.
    inline bool StripToolFlags(int& argc, char** argv)
    {
        int write = 1;
        for (int i = 1; i < argc; ++i)
        {
            char* a = argv[i];
            if ((a[0] == '-' || a[0] == '/') && a[1] != '\0' && a[2] == '\0')
            {
                const char c = static_cast<char>(::tolower(static_cast<unsigned char>(a[1])));
                if (c == 'h')
                {
                    g_opt.hotfixes = true;
                    continue;
                }
                if (c == 's')
                {
                    g_opt.software = true;
                    continue;
                }
                if (c == 'd')
                {
                    g_opt.disks = true;
                    continue;
                }
                if (c == 'c')
                {
                    g_opt.csv = true;
                    continue;
                }
                if (c == 't')
                {
                    if (!g_opt.csv || i + 1 >= argc)
                    {
                        return false;
                    }
                    const char* value = argv[++i];
                    g_opt.delimiter = (::strcmp(value, "\\t") == 0)
                                          ? L'\t'
                                          : static_cast<wchar_t>(static_cast<unsigned char>(value[0]));
                    continue;
                }
            }
            argv[write++] = a;
        }
        argc = write;
        return true;
    }
} // namespace psinfo
