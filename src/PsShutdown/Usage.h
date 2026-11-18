// PsShutdown usage block with the live reason table (mirrors sub_140005B30).
#pragma once

#include "ShutdownCommon.h"

namespace psshutdown
{
    // User32 BuildReasonArray fills a pointer array of reason entries, each a DWORD
    // code followed by its title string. Recovered from sub_140005B30.
    struct ReasonList
    {
        int** reasons = nullptr;
        unsigned int count = 0;
    };
    using BuildReasonArrayFn = BOOL(__stdcall*)(ReasonList*, int, int);

    inline void PrintUsage()
    {
        std::printf("usage:\n");
        std::printf("psshutdown -s|-r|-h|-d|-k|-a|-l|-o|-x [-f] [-c] [-t [nn|h:m]] [-v nn] "
                    "[-e [u|p]:xx:yy] [-m \"message\"] [-u Username [-p password]] [-n s] "
                    "[\\\\computer[,computer[,...]|@file]\n");
        std::printf("   -a          Abort a shutdown (only possible while countdown is in progress)\n");
        std::printf("   -c          Allow the shutdown to be aborted by the interactive user\n");
        std::printf("   -d          Suspend the computer\n");
        std::printf("   -e          Shutdown reason code (available on Windows XP and higher).\n");
        std::printf("               Specify 'u' for unplanned and 'p' for planned\n");
        std::printf("               shutdown reason codes.\n");
        std::printf("               xx is the major reason code (must be less than 256)\n");
        std::printf("               yy is the minor reason code (must be less than 65536)\n");
        std::printf("   -f          Forces running applications to close\n");
        std::printf("   -h          Hibernate the computer\n");
        std::printf("   -k          Poweroff the computer (reboot if poweroff is not supported)\n");
        std::printf("   -l          Lock the computer\n");
        std::printf("   -m          Message to display to logged on users\n");
        std::printf("   -n          Specifies timeout in seconds connecting to remote computers\n");
        std::printf("   -o          Logoff the console user\n");
        std::printf("   -p          Specifies optional password for user name. If you omit this\n");
        std::printf("               you will be prompted to enter a hidden password.\n");
        std::printf("   -r          Reboot after shutdown\n");
        std::printf("   -s          Shutdown without poweroff\n");
        std::printf("   -t          Specifies countdown in seconds until shutdown (default is 20) or\n");
        std::printf("               the time of shutdown (in 24 hour notation)\n");
        std::printf("   -u          Specifies optional user name for login to remote\n               computer.\n");
        std::printf("   -v          Display message for the specified number of seconds before\n");
        std::printf("               the shutdown. If you omit this parameter the shutdown\n");
        std::printf("               notification dialog displays and specifying a value of 0\n");
        std::printf("               omits the dialog.\n");
        std::printf("   -x          Turn monitor off (system will initiate Modern Standby if supported)\n");
        std::printf("   -nobanner   Do not display the startup banner and copyright message.\n");
        std::printf("   computer    Shutdown the computer or computers specified\n");
        std::printf("   @file       Shutdown the computers listed in the file specified\n");
        std::printf("\n");

        const HMODULE user32 = ::GetModuleHandleA("User32.dll");
        const auto build =
            reinterpret_cast<BuildReasonArrayFn>(reinterpret_cast<void*>(::GetProcAddress(user32, "BuildReasonArray")));
        ReasonList list;
        if (build != nullptr && build(&list, 0, 0) != 0)
        {
            if (list.count == 0)
            {
                build(&list, 1, 1);
            }
            std::printf("\nReasons defined on this computer (U = unplanned, P = planned):\n");
            std::printf("Type   Major   Minor   Title\n");
            for (unsigned int i = 0; i < list.count; ++i)
            {
                const int* entry = list.reasons[i];
                if ((*entry & 0x8000000) == 0)
                {
                    // The title follows the code as a wide string, and the original
                    // prints it through a wide printf. Convert to the console code
                    // page so the bytes match the original's output.
                    const auto* title = reinterpret_cast<const wchar_t*>(entry + 1);
                    char titleA[512] = {};
                    ::WideCharToMultiByte(CP_ACP, 0, title, -1, titleA, static_cast<int>(std::size(titleA)), nullptr,
                                          nullptr);
                    std::printf("  %c     % 2d    % 4d     %s\n", ((*entry >> 31) & 0xFFFFFFFB) + 85,
                                static_cast<unsigned char>((*entry >> 16) & 0xFF), static_cast<unsigned short>(*entry),
                                titleA);
                }
            }
        }
        std::printf("\n");
    }
} // namespace psshutdown
