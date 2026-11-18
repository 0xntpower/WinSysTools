// PsService status, config, and security printers. Each reproduces the exact
// field layout of the original. See analysis/PsService.md for the sub_ map.
#pragma once

#include "ServiceUtil.h"

namespace psservice
{
    // Failure-action labels indexed by SC_ACTION_TYPE (table at 0x140049190).
    const wchar_t* const kActionLabels[] = {L"None", L"Restart", L"Reboot", L"Run command"};

    inline void PrintServiceType(DWORD type)
    {
        std::wprintf(L"\tTYPE\t\t  : %-2x ", type);
        if ((type & SERVICE_WIN32_OWN_PROCESS) != 0)
        {
            std::wprintf(L"WIN32_OWN_PROCESS ");
        }
        if ((type & SERVICE_WIN32_SHARE_PROCESS) != 0)
        {
            std::wprintf(L"WIN32_SHARE_PROCESS ");
        }
        if ((type & SERVICE_KERNEL_DRIVER) != 0)
        {
            std::wprintf(L"KERNEL_DRIVER ");
        }
        if ((type & SERVICE_FILE_SYSTEM_DRIVER) != 0)
        {
            std::wprintf(L"FILE_SYSTEM_DRIVER ");
        }
        if ((type & SERVICE_INTERACTIVE_PROCESS) != 0)
        {
            std::wprintf(L"INTERACTIVE_PROCESS  ");
        }
    }

    // Print the description, stripping a single trailing CR/LF like the original.
    inline void PrintDescriptionStripped(SC_HANDLE service)
    {
        if (g_queryServiceConfig2W == nullptr)
        {
            return;
        }
        DWORD needed = 0;
        g_queryServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &needed);
        if (needed == 0)
        {
            return;
        }
        std::vector<BYTE> buffer(needed);
        g_queryServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, buffer.data(), needed, &needed);
        auto* info = reinterpret_cast<SERVICE_DESCRIPTIONW*>(buffer.data());
        if (info->lpDescription == nullptr)
        {
            return;
        }
        std::wstring text = info->lpDescription;
        if (text.size() > 3 && !text.empty() && text.back() == L'\n')
        {
            text.pop_back();
            if (!text.empty() && text.back() == L'\r')
            {
                text.pop_back();
            }
        }
        std::wprintf(L"%s\n", text.c_str());
    }

    inline void PrintServiceStatus(SC_HANDLE service, const wchar_t* name, const wchar_t* groupFilter,
                                   bool interactiveOnly)
    {
        DWORD needed = 0;
        if (::QueryServiceConfigW(service, nullptr, 0, &needed) || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            std::wprintf(L"Error querying status of %s on %s:\n", name, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }
        std::vector<BYTE> buffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (!::QueryServiceConfigW(service, config, needed, &needed))
        {
            std::wprintf(L"Error querying status of %s on %s:\n", name, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }

        if (groupFilter != nullptr && ::_wcsicmp(config->lpLoadOrderGroup, groupFilter) != 0)
        {
            return;
        }
        if (interactiveOnly && (config->dwServiceType & SERVICE_INTERACTIVE_PROCESS) == 0)
        {
            return;
        }

        SERVICE_STATUS status = {};
        ::QueryServiceStatus(service, &status);

        std::wprintf(L"SERVICE_NAME: %s\n", name);
        std::wprintf(L"DISPLAY_NAME: %s\n", config->lpDisplayName);
        PrintDescriptionStripped(service);

        if (*config->lpLoadOrderGroup != 0)
        {
            std::wprintf(L"\tGROUP\t\t  : %s\n", config->lpLoadOrderGroup);
        }
        PrintServiceType(config->dwServiceType);

        std::wprintf(L"\n\tSTATE\t\t  : %-2x ", status.dwCurrentState);
        switch (status.dwCurrentState)
        {
        case SERVICE_STOPPED:
            std::wprintf(L"STOPPED\n");
            break;
        case SERVICE_START_PENDING:
            std::wprintf(L"START_PENDING\n");
            break;
        case SERVICE_STOP_PENDING:
            std::wprintf(L"STOP_PENDING\n");
            break;
        case SERVICE_RUNNING:
            std::wprintf(L"RUNNING\n");
            break;
        case SERVICE_CONTINUE_PENDING:
            std::wprintf(L"CONTINUE_PENDING\n");
            break;
        case SERVICE_PAUSE_PENDING:
            std::wprintf(L"PAUSE_PENDING\n");
            break;
        case SERVICE_PAUSED:
            std::wprintf(L"PAUSED\n");
            break;
        default:
            break;
        }

        std::wprintf(L"\t\t\t       ");
        std::wprintf((status.dwControlsAccepted & SERVICE_ACCEPT_STOP) != 0 ? L"(STOPPABLE," : L"(NOT_STOPPABLE,");
        std::wprintf((status.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) != 0 ? L"PAUSABLE,"
                                                                                      : L"NOT_PAUSABLE,");
        if ((status.dwControlsAccepted & SERVICE_ACCEPT_PRESHUTDOWN) != 0)
        {
            std::wprintf(L"ACCEPTS_PRESHUTDOWN)\n");
        }
        else
        {
            std::wprintf((status.dwControlsAccepted & SERVICE_ACCEPT_SHUTDOWN) != 0 ? L"ACCEPTS_SHUTDOWN)\n"
                                                                                    : L"IGNORES_SHUTDOWN)\n");
        }

        std::wprintf(L"\tWIN32_EXIT_CODE\t  : %-2d (0x%x)\n", status.dwWin32ExitCode, status.dwWin32ExitCode);
        std::wprintf(L"\tSERVICE_EXIT_CODE : %-2d (0x%x)\n", status.dwServiceSpecificExitCode,
                     status.dwServiceSpecificExitCode);
        std::wprintf(L"\tCHECKPOINT\t  : 0x%x\n", status.dwCheckPoint);
        std::wprintf(L"\tWAIT_HINT\t  : %d ms\n", status.dwWaitHint);
        std::wprintf(L"\n");
    }

    inline void PrintStartType(DWORD startType, bool delayed)
    {
        std::wprintf(L"\n\tSTART_TYPE\t  : %-2x ", startType);
        switch (startType)
        {
        case SERVICE_BOOT_START:
            std::wprintf(L"BOOT_START\n");
            break;
        case SERVICE_SYSTEM_START:
            std::wprintf(L"SYSTEM_START\n");
            break;
        case SERVICE_AUTO_START:
            std::wprintf(L"AUTO_START");
            if (delayed)
            {
                std::wprintf(L" (DELAYED)");
            }
            std::wprintf(L"\n");
            break;
        case SERVICE_DEMAND_START:
            std::wprintf(L"DEMAND_START\n");
            break;
        case SERVICE_DISABLED:
            std::wprintf(L"DISABLED\n");
            break;
        default:
            break;
        }
    }

    inline void PrintErrorControl(DWORD errorControl)
    {
        std::wprintf(L"\tERROR_CONTROL\t  : %-2x ", errorControl);
        switch (errorControl)
        {
        case SERVICE_ERROR_IGNORE:
            std::wprintf(L"IGNORE\n");
            break;
        case SERVICE_ERROR_NORMAL:
            std::wprintf(L"NORMAL\n");
            break;
        case SERVICE_ERROR_SEVERE:
            std::wprintf(L"SEVERE\n");
            break;
        case SERVICE_ERROR_CRITICAL:
            std::wprintf(L"CRITICAL\n");
            break;
        default:
            break;
        }
    }

    inline void PrintFailureActions(SC_HANDLE service)
    {
        if (g_queryServiceConfig2W == nullptr)
        {
            return;
        }
        DWORD needed = 0;
        g_queryServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, nullptr, 0, &needed);
        if (needed == 0)
        {
            return;
        }
        std::vector<BYTE> buffer(needed);
        g_queryServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, buffer.data(), needed, &needed);
        auto* actions = reinterpret_cast<SERVICE_FAILURE_ACTIONSW*>(buffer.data());
        if (actions->cActions == 0)
        {
            return;
        }
        if (actions->lpRebootMsg != nullptr)
        {
            std::wprintf(L"\tREBOOT_MESSAGE\t  : %s\n", actions->lpRebootMsg);
        }
        if (actions->lpCommand != nullptr)
        {
            std::wprintf(L"\tCOMMAND\t  : %s\n", actions->lpCommand);
        }
        std::wprintf(L"\tFAIL_RESET_PERIOD : %d seconds\n", actions->dwResetPeriod);
        for (DWORD i = 0; i < actions->cActions; ++i)
        {
            const int type = actions->lpsaActions[i].Type;
            const wchar_t* label = (type >= 0 && type <= 3) ? kActionLabels[type] : L"";
            // The original prints the raw SC_ACTION.Delay without converting its ms unit.
            const DWORD delay = actions->lpsaActions[i].Delay;
            if (i == 0)
            {
                std::wprintf(L"\tFAILURE_ACTIONS\t  : %s\tDELAY: %d seconds\n", label, delay);
            }
            else
            {
                std::wprintf(L"\t\t\t  : %s\tDELAY: %d seconds\n", label, delay);
            }
        }
    }

    inline void PrintServiceConfig(SC_HANDLE service, const wchar_t* name)
    {
        DWORD needed = 0;
        ::QueryServiceConfigW(service, nullptr, 0, &needed);
        std::vector<BYTE> buffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (!::QueryServiceConfigW(service, config, needed, &needed))
        {
            std::wprintf(L"Error querying status of %s on %s:\n", name, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }

        std::wprintf(L"SERVICE_NAME: %s\n", name);
        std::wprintf(L"DISPLAY_NAME: %s\n", config->lpDisplayName);

        bool delayed = false;
        if (g_queryServiceConfig2W != nullptr)
        {
            DWORD descNeeded = 0;
            g_queryServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &descNeeded);
            if (descNeeded != 0)
            {
                std::vector<BYTE> descBuffer(descNeeded);
                g_queryServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, descBuffer.data(), descNeeded, &descNeeded);
                auto* desc = reinterpret_cast<SERVICE_DESCRIPTIONW*>(descBuffer.data());
                if (desc->lpDescription != nullptr)
                {
                    std::wprintf(L"%s\n", desc->lpDescription);
                }
            }
            SERVICE_DELAYED_AUTO_START_INFO delayedInfo = {};
            DWORD delayedNeeded = 0;
            if (g_queryServiceConfig2W(service, SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                                       reinterpret_cast<LPBYTE>(&delayedInfo), sizeof(delayedInfo), &delayedNeeded))
            {
                delayed = delayedInfo.fDelayedAutostart != FALSE;
            }
        }

        PrintServiceType(config->dwServiceType);
        PrintStartType(config->dwStartType, delayed);
        PrintErrorControl(config->dwErrorControl);
        std::wprintf(L"\tBINARY_PATH_NAME  : %s\n", config->lpBinaryPathName);
        std::wprintf(L"\tLOAD_ORDER_GROUP  : %s\n", config->lpLoadOrderGroup);
        std::wprintf(L"\tTAG\t\t  : %d\n", config->dwTagId);
        std::wprintf(L"\tDEPENDENCIES\t  : %s\n", config->lpDependencies);
        if (*config->lpDependencies != 0)
        {
            const wchar_t* dep = config->lpDependencies + ::wcslen(config->lpDependencies) + 1;
            while (*dep != 0)
            {
                std::wprintf(L"\t\t\t  : %s\n", dep);
                dep += ::wcslen(dep) + 1;
            }
        }
        std::wprintf(L"\tSERVICE_START_NAME: %s\n", config->lpServiceStartName);

        PrintFailureActions(service);
        std::wprintf(L"\n");
    }

    [[nodiscard]] inline std::wstring AceAccountName(PSID sid)
    {
        wchar_t name[260] = {};
        wchar_t domain[260] = {};
        DWORD cchName = static_cast<DWORD>(std::size(name));
        DWORD cchDomain = static_cast<DWORD>(std::size(domain));
        SID_NAME_USE use = SidTypeUnknown;
        const wchar_t* system = g_machine.empty() ? nullptr : g_machine.c_str();
        if (::LookupAccountSidW(system, sid, name, &cchName, domain, &cchDomain, &use))
        {
            if (domain[0] != 0)
            {
                return std::wstring(domain) + L"\\" + name;
            }
            return std::wstring(name);
        }
        LPWSTR text = nullptr;
        std::wstring result;
        if (::ConvertSidToStringSidW(sid, &text) && text != nullptr)
        {
            result = text;
            ::LocalFree(text);
        }
        return result;
    }

    inline void PrintAceAccess(DWORD mask)
    {
        if (mask == SERVICE_ALL_ACCESS)
        {
            std::wprintf(L"\t        All\n");
            return;
        }
        GENERIC_MAPPING mapping = {0x0002008Du, 0x00020002u, 0x00020170u, 0x000F01FFu};
        ::MapGenericMask(&mask, &mapping);
        if ((mask & SERVICE_QUERY_STATUS) != 0)
        {
            std::wprintf(L"\t        Query status\n");
        }
        if ((mask & SERVICE_QUERY_CONFIG) != 0)
        {
            std::wprintf(L"\t        Query Config\n");
        }
        if ((mask & SERVICE_CHANGE_CONFIG) != 0)
        {
            std::wprintf(L"\t        Change Config\n");
        }
        if ((mask & SERVICE_INTERROGATE) != 0)
        {
            std::wprintf(L"\t        Interrogate\n");
        }
        if ((mask & SERVICE_ENUMERATE_DEPENDENTS) != 0)
        {
            std::wprintf(L"\t        Enumerate Dependents\n");
        }
        if ((mask & SERVICE_PAUSE_CONTINUE) != 0)
        {
            std::wprintf(L"\t        Pause/Resume\n");
        }
        if ((mask & SERVICE_START) != 0)
        {
            std::wprintf(L"\t        Start\n");
        }
        if ((mask & SERVICE_STOP) != 0)
        {
            std::wprintf(L"\t        Stop\n");
        }
        if ((mask & SERVICE_USER_DEFINED_CONTROL) != 0)
        {
            std::wprintf(L"\t        User-Defined Control\n");
        }
        if ((mask & READ_CONTROL) != 0)
        {
            std::wprintf(L"\t        Read Permissions\n");
        }
        if ((mask & WRITE_DAC) != 0)
        {
            std::wprintf(L"\t        Change Permissions\n");
        }
        if ((mask & WRITE_OWNER) != 0)
        {
            std::wprintf(L"\t        Change Owner\n");
        }
    }

    inline void PrintServiceSecurity(SC_HANDLE service, const wchar_t* name)
    {
        DWORD needed = 0;
        ::QueryServiceConfigW(service, nullptr, 0, &needed);
        std::vector<BYTE> configBuffer(needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(configBuffer.data());
        if (!::QueryServiceConfigW(service, config, needed, &needed))
        {
            std::wprintf(L"Error querying configuration of %s on %s:\n", name, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }

        constexpr SECURITY_INFORMATION kInfo = OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
        DWORD sdNeeded = 0;
        ::QueryServiceObjectSecurity(service, kInfo, nullptr, 0, &sdNeeded);
        if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            std::wprintf(L"Error querying security of %s on %s:\n", name, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }
        std::vector<BYTE> sdBuffer(sdNeeded);
        auto* descriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(sdBuffer.data());
        if (!::QueryServiceObjectSecurity(service, kInfo, descriptor, sdNeeded, &sdNeeded))
        {
            std::wprintf(L"Error querying security of %s on %s:\n", name, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }

        BOOL daclPresent = FALSE;
        BOOL daclDefaulted = FALSE;
        PACL dacl = nullptr;
        if (!::GetSecurityDescriptorDacl(descriptor, &daclPresent, &dacl, &daclDefaulted))
        {
            std::wprintf(L"Error querying security of %s on %s:\n", name, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }

        std::wprintf(L"SERVICE_NAME: %s\n", name);
        std::wprintf(L"DISPLAY_NAME: %s\n", config->lpDisplayName);
        std::wprintf(L"\tACCOUNT: %s\n", config->lpServiceStartName);
        std::wprintf(L"\tSECURITY:\n");

        if (dacl != nullptr)
        {
            for (WORD i = 0; i < dacl->AceCount; ++i)
            {
                void* acePtr = nullptr;
                if (!::GetAce(dacl, i, &acePtr))
                {
                    continue;
                }
                auto* header = static_cast<ACE_HEADER*>(acePtr);
                std::wprintf(header->AceType == ACCESS_ALLOWED_ACE_TYPE ? L"\t[ALLOW] " : L"\t[DENY]  ");
                auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(acePtr);
                std::wprintf(L"%s\n", AceAccountName(&ace->SidStart).c_str());
                PrintAceAccess(ace->Mask);
            }
        }
        else
        {
            std::wprintf(L"\t[ALLOW] Builtin\\Everyone: ALL\n");
        }
        std::wprintf(L"\n");
    }
} // namespace psservice
