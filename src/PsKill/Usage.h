// PsKill usage text (sub_140005220). See analysis/PsKill.md for the sub_ map.
#pragma once

#include <cstdio>

namespace pskill
{
    inline void PrintUsage()
    {
        std::printf("Usage: pskill [-t] [\\\\computer [-u username [-p password]]] <process ID | name>\n");
        std::printf("     -t    Kill the process and its descendants.\n");
        std::printf("     -u    Specifies optional user name for login to\n           remote computer.\n");
        std::printf("     -p    Specifies optional password for user name. If you omit this\n");
        std::printf("           you will be prompted to enter a hidden password.\n");
        std::printf("     -nobanner Do not display the startup banner and copyright message.\n");
        std::printf("\n");
    }
} // namespace pskill
