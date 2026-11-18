// PsService usage text, one block per command code (mirrors sub_140006D00).
#pragma once

#include "ServiceUtil.h"

namespace psservice
{
    inline void PrintUsage(const wchar_t* prog, int code)
    {
        const wchar_t* p = BaseName(prog);
        switch (code)
        {
        case 0:
            std::wprintf(L"PsService lists or controls services on a local or remote system.\n");
            std::wprintf(L"\nUsage: %s [\\\\Computer [-u Username [-p Password]]] <cmd> <optns>\n", p);
            std::wprintf(L"Cmd is one of the following:\n");
            std::wprintf(L"   query      Queries the status of a service\n");
            std::wprintf(L"   config     Queries the configuration\n");
            std::wprintf(L"   setconfig  Sets the configuration\n");
            std::wprintf(L"   start      Starts a service\n");
            std::wprintf(L"   stop       Stops a service\n");
            std::wprintf(L"   restart    Stops and then restarts a service\n");
            std::wprintf(L"   pause      Pauses a service\n");
            std::wprintf(L"   cont       Continues a paused service\n");
            std::wprintf(L"   depend     Enumerates the services that depend on the one specified\n");
            std::wprintf(L"   find       Searches for an instance of a service on the network\n");
            std::wprintf(L"   security   Reports the security permissions assigned to a service\n");
            std::wprintf(L"Use the username and password to log into the remote computer in cases where\n");
            std::wprintf(L"your account does not have permissions to perform the action you specify.\n");
            std::wprintf(L"\nOmitting a command queries the active services on the specified computer.\n");
            std::wprintf(L"Enter -? for help on a particular command.\n");
            std::wprintf(L"Use option -nobanner to supress the startup banner and copyright message.\n");
            break;
        case 1:
        case 2:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] query <optns>\n", p);
            std::wprintf(L"Queries the status of the specified services.\n");
            std::wprintf(L"Options include:\n");
            std::wprintf(L"   service    Dumps information on the service\n");
            std::wprintf(L"   -g group   Dumps services that belong to the group\n");
            std::wprintf(L"   -t type    Dumps the specified type of service:\n");
            std::wprintf(L"                driver      Device drivers\n");
            std::wprintf(L"                service     Win32 services (default)\n");
            std::wprintf(L"                interactive Win32 interactive services\n");
            std::wprintf(L"                all         Both\n");
            std::wprintf(L"   -s state   Dumps services in the specified state:\n");
            std::wprintf(L"                active      Running services\n");
            std::wprintf(L"                inactive    Nonrunning services\n");
            std::wprintf(L"                all         Both (default)\n");
            std::wprintf(L"\nThe switches can be used together.\n");
            break;
        case 3:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] stop <svc>\n", p);
            std::wprintf(L"Stops the specified service.\n");
            break;
        case 4:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] start <svc>\n", p);
            std::wprintf(L"Starts the specified service.\n");
            break;
        case 5:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] pause <svc>\n", p);
            std::wprintf(L"Pauses the specified service, which must be running.\n");
            break;
        case 6:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] cont <svc>\n", p);
            std::wprintf(L"Continues the specified service, which must be paused.\n");
            break;
        case 7:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] config [svc]\n", p);
            std::wprintf(L"Prints the configuration of the specified service.\n");
            break;
        case 8:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] depend <svc>\n", p);
            std::wprintf(L"Lists the services that depend on the one specified.\n");
            break;
        case 9:
            std::wprintf(L"Usage: %s find <svc> [all]\n", p);
            std::wprintf(L"Searches the network for the specified service.\n");
            std::wprintf(L"By default, the search is for active instances of the service, but the\n");
            std::wprintf(L"\"all\" option has the search include inactive instances.\n");
            break;
        case 10:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] restart <svc>\n", p);
            std::wprintf(L"Stops and then restarts the specified service.\n");
            break;
        case 11:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] setconfig <svc> <start-type>\n", p);
            std::wprintf(L"Sets the start type of the specified service where start-type\nis one of auto, demand and "
                         L"disabled.\n");
            break;
        case 12:
            std::wprintf(L"Usage: %s [\\\\Computer [-u Username [-p Password]]] security [svc]\n", p);
            std::wprintf(L"Reports access permissions for the specified service.\n");
            break;
        default:
            break;
        }
        std::wprintf(L"\n");
    }
} // namespace psservice
