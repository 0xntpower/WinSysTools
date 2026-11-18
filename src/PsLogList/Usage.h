// PsLogList usage, the full original switch list (mirrors sub_140009A00).
#pragma once

#include "LogListUtil.h"

namespace psloglist
{
    inline void PrintUsage()
    {
        std::printf("PsLogList dumps event logs on a local or remote NT system.\n");
        std::printf("\nUsage: psloglist [\\\\computer[,computer2[,...] | @file] [-u username [-p password]]]"
                    " [-s [-t delimiter]] [-m #|-n #|-d #|-h #|-w][-c][-x][-r][-a mm/dd/yy][-b mm/dd/yy]"
                    " [-f filter] [-i ID,[ID,...]] | -e ID,[ID,...]] [-o event source[,event source[,...]]]"
                    " [-q event source[,event source[,...]]] [[-g|-l] event log file] <event log>\n");
        std::printf("     @file     Psloglist will execute the command on each of the computers\n");
        std::printf("               listed in the file.\n");
        std::printf("     -a        Dump records timestamped after specified date.\n");
        std::printf("     -b        Dump records timestamped before specified date.\n");
        std::printf("     -c        Clear event log after displaying.\n");
        std::printf("     -d        Only display records from previous n days.\n");
        std::printf("     -e        Exclude events with the specified ID or IDs (up to 10).\n");
        std::printf("     -f        Filter event types, using starting letter\n");
        std::printf("               (e.g. \"-f we\" to filter warnings and errors).\n");
        std::printf("     -g        Export an event log as an evt file.\n");
        std::printf("     -h        Only display records from previous n hours.\n");
        std::printf("     -i        Show only events with the specified ID or IDs (up to 10).\n");
        std::printf("     -l        Dump the contents of the specified saved event log file.\n");
        std::printf("     -m        Only display records from previous n minutes.\n");
        std::printf("     -n        Only display n most recent records.\n");
        std::printf("     -o        Show only records from the specified event source or sources\n");
        std::printf("               (e.g. \"-o cdrom\"). Append '*' to specify substring match.\n");
        std::printf("     -p        Specifies password for user name.\n");
        std::printf("     -q        Omit records from the specified event source or sources\n");
        std::printf("               (e.g. \"-q cdrom\").\n");
        std::printf("               Append '*' to specify substring match.\n");
        std::printf("     -r        Dump log from least recent to most recent.\n");
        std::printf("     -s        Records are listed on one line each with delimited\n"
                    "               fields, which is convenient for string searches.\n");
        std::printf("     -t        The default delimiter for the -s option is a comma,\n"
                    "               but can be overriden with the specified character. Use \"\\t\"\n"
                    "               to specify tab.\n");
        std::printf("     -u        Specifies optional user name for login to\n               remote computer.\n");
        std::printf("     -w        Wait for new events, dumping them as they generate (local system\n");
        std::printf("               only.)\n");
        std::printf("     -x        Dump extended data.\n");
        std::printf("     -z        List event logs registered on specified system.\n");
        std::printf("     -nobanner Do not display the startup banner and copyright message.\n");
        std::printf("     eventlog  Specifies event log to dump. Default is system. If the\n"
                    "               -l switch is present then the event log name specifies\n"
                    "               how to interpret the event log file.\n");
        std::printf("\n");
    }
} // namespace psloglist
