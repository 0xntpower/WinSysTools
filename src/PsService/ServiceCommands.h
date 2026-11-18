// PsService command handlers and the dispatch table (mirrors sub_140005600).
#pragma once

#include "ServicePrint.h"
#include "Usage.h"

namespace psservice
{
    inline void CmdQuery(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
    {
        const wchar_t* serviceName = nullptr;
        std::wstring serviceStore;
        const wchar_t* group = nullptr;
        std::wstring groupStore;
        bool interactive = false;
        DWORD type = 0;
        DWORD state = SERVICE_STATE_ALL;

        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::wstring& a = args[i];
            if (IEq(a, L"-t") || IEq(a, L"/t") || IEq(a, L"type="))
            {
                if (++i >= args.size())
                {
                    PrintUsage(prog, 1);
                    return;
                }
                const std::wstring& value = args[i];
                if (IEq(value, L"driver"))
                {
                    type = kServiceDriverAndWin32 & SERVICE_DRIVER;
                }
                else if (IEq(value, L"service"))
                {
                    type = kServiceWin32;
                }
                else if (IEq(value, L"interactive"))
                {
                    interactive = true;
                }
                else if (IEq(value, L"all"))
                {
                    type = kServiceDriverAndWin32;
                }
                else
                {
                    PrintUsage(prog, 1);
                    return;
                }
            }
            else if (IEq(a, L"-s") || IEq(a, L"/s") || IEq(a, L"state="))
            {
                if (++i >= args.size())
                {
                    PrintUsage(prog, 1);
                    return;
                }
                const std::wstring& value = args[i];
                if (IEq(value, L"active"))
                {
                    state = SERVICE_ACTIVE;
                }
                else if (IEq(value, L"inactive"))
                {
                    state = SERVICE_INACTIVE;
                }
                else if (IEq(value, L"all"))
                {
                    state = SERVICE_STATE_ALL;
                }
            }
            else if (IEq(a, L"-g") || IEq(a, L"/g") || IEq(a, L"group="))
            {
                if (++i >= args.size())
                {
                    PrintUsage(prog, 1);
                    return;
                }
                groupStore = args[i];
                group = groupStore.c_str();
            }
            else if (IEq(a, L"-?") || IEq(a, L"/?"))
            {
                PrintUsage(prog, 1);
                return;
            }
            else
            {
                serviceStore = a;
                serviceName = serviceStore.c_str();
                if (type == 0)
                {
                    type = kServiceDriverAndWin32;
                }
            }
        }
        if (type == 0)
        {
            type = kServiceWin32;
        }

        std::vector<BYTE> buffer;
        DWORD count = 0;
        if (!EnumServices(scm, type, state, buffer, count))
        {
            return;
        }
        ENUM_SERVICE_STATUS_PROCESSW* list = Services(buffer);
        int shown = 0;
        for (DWORD i = 0; i < count; ++i)
        {
            if (serviceName != nullptr)
            {
                const size_t len = ::wcslen(serviceName);
                if (::_wcsnicmp(serviceName, list[i].lpServiceName, len) != 0 &&
                    ::_wcsnicmp(serviceName, list[i].lpDisplayName, len) != 0)
                {
                    continue;
                }
            }
            ++shown;
            ScHandle service(::OpenServiceW(scm, list[i].lpServiceName, kQueryStatusAccess));
            if (service)
            {
                PrintServiceStatus(service.Get(), list[i].lpServiceName, group, interactive);
            }
            else
            {
                std::wprintf(L"Error opening %s for query on %s\n", list[i].lpServiceName, g_machine.c_str());
                pstools::PrintSystemError(::GetLastError());
            }
        }
        if (shown == 0)
        {
            std::wprintf(L"No services matching %s found on %s.\n", serviceName, g_machine.c_str());
        }
    }

    inline void CmdConfig(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
    {
        for (const std::wstring& a : args)
        {
            if (IEq(a, L"-?") || IEq(a, L"/?"))
            {
                PrintUsage(prog, 7);
                return;
            }
        }
        const wchar_t* serviceName = args.empty() ? nullptr : args[0].c_str();
        const DWORD type = serviceName != nullptr ? kServiceDriverAndWin32 : kServiceWin32;

        std::vector<BYTE> buffer;
        DWORD count = 0;
        if (!EnumServices(scm, type, SERVICE_STATE_ALL, buffer, count))
        {
            return;
        }
        ENUM_SERVICE_STATUS_PROCESSW* list = Services(buffer);
        int shown = 0;
        for (DWORD i = 0; i < count; ++i)
        {
            if (serviceName != nullptr)
            {
                const size_t len = ::wcslen(serviceName);
                if (::_wcsnicmp(serviceName, list[i].lpServiceName, len) != 0 &&
                    ::_wcsnicmp(serviceName, list[i].lpDisplayName, len) != 0)
                {
                    continue;
                }
            }
            ++shown;
            ScHandle service(::OpenServiceW(scm, list[i].lpServiceName, kConfigAccess));
            if (service)
            {
                PrintServiceConfig(service.Get(), list[i].lpServiceName);
            }
            else
            {
                std::wprintf(L"Error opening %s for query on %s\n", list[i].lpServiceName, g_machine.c_str());
                pstools::PrintSystemError(::GetLastError());
            }
        }
        if (shown == 0)
        {
            std::wprintf(L"No services matching %s found on %s.\n", serviceName, g_machine.c_str());
        }
    }

    inline void CmdSecurity(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
    {
        for (const std::wstring& a : args)
        {
            if (IEq(a, L"-?") || IEq(a, L"/?"))
            {
                PrintUsage(prog, 12);
                return;
            }
        }
        const wchar_t* serviceName = args.empty() ? nullptr : args[0].c_str();
        const DWORD type = serviceName != nullptr ? kServiceDriverAndWin32 : kServiceWin32;

        std::vector<BYTE> buffer;
        DWORD count = 0;
        if (!EnumServices(scm, type, SERVICE_STATE_ALL, buffer, count))
        {
            return;
        }
        ENUM_SERVICE_STATUS_PROCESSW* list = Services(buffer);
        int shown = 0;
        for (DWORD i = 0; i < count; ++i)
        {
            if (serviceName != nullptr && ::_wcsicmp(serviceName, list[i].lpServiceName) != 0 &&
                ::_wcsicmp(serviceName, list[i].lpDisplayName) != 0)
            {
                continue;
            }
            ++shown;
            ScHandle service(::OpenServiceW(scm, list[i].lpServiceName, kSecurityAccess));
            if (service)
            {
                PrintServiceSecurity(service.Get(), list[i].lpServiceName);
            }
            else
            {
                std::wprintf(L"Error opening %s for security query on %s\n", list[i].lpServiceName, g_machine.c_str());
                pstools::PrintSystemError(::GetLastError());
            }
        }
        if (shown == 0)
        {
            std::wprintf(L"No services named %s found on %s.\n", serviceName, g_machine.c_str());
        }
    }

    inline void CmdDepend(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
    {
        if (args.empty() || IEq(args[0], L"-?") || IEq(args[0], L"/?"))
        {
            PrintUsage(prog, 8);
            return;
        }
        const wchar_t* serviceName = args[0].c_str();
        ScHandle service(OpenServiceByNameOrDisplay(scm, serviceName, SERVICE_ENUMERATE_DEPENDENTS));
        if (!service)
        {
            std::wprintf(L"Error opening %s on %s:\n", serviceName, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }

        DWORD needed = 0;
        DWORD returned = 0;
        if (::EnumDependentServicesW(service.Get(), SERVICE_STATE_ALL, nullptr, 0, &needed, &returned))
        {
            std::wprintf(L"%s has no dependent services.\n", serviceName);
            return;
        }
        if (::GetLastError() != ERROR_MORE_DATA)
        {
            std::wprintf(L"Error querying services on %s:\n", g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }
        std::vector<BYTE> buffer(needed);
        auto* list = reinterpret_cast<ENUM_SERVICE_STATUSW*>(buffer.data());
        if (!::EnumDependentServicesW(service.Get(), SERVICE_STATE_ALL, list, needed, &needed, &returned))
        {
            std::wprintf(L"Error querying services on %s:\n", g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }
        for (DWORD i = 0; i < returned; ++i)
        {
            ScHandle dependent(::OpenServiceW(scm, list[i].lpServiceName, kQueryStatusAccess));
            PrintServiceStatus(dependent.Get(), list[i].lpServiceName, nullptr, false);
        }
    }

    inline void CmdFind(SC_HANDLE, const std::vector<std::wstring>& args, const wchar_t* prog)
    {
        if (args.empty() || IEq(args[0], L"-?") || IEq(args[0], L"/?"))
        {
            PrintUsage(prog, 9);
            return;
        }
        const wchar_t* serviceName = args[0].c_str();
        bool includeInactive = false;
        if (args.size() >= 2)
        {
            if (IEq(args[1], L"all"))
            {
                includeInactive = true;
            }
            else
            {
                PrintUsage(prog, 9);
                return;
            }
        }

        LPBYTE buffer = nullptr;
        DWORD entries = 0;
        DWORD total = 0;
        DWORD resume = 0;
        const DWORD rc = ::NetServerEnum(nullptr, 100, &buffer, MAX_PREFERRED_LENGTH, &entries, &total, SV_TYPE_NT,
                                         nullptr, &resume);
        bool found = false;
        if (rc == NERR_Success || rc == ERROR_MORE_DATA)
        {
            auto* servers = reinterpret_cast<SERVER_INFO_100*>(buffer);
            for (DWORD i = 0; i < entries; ++i)
            {
                wchar_t destination[0x104] = {};
                ::wcscpy_s(destination, std::size(destination), servers[i].sv100_name);
                std::wprintf(L"\rChecking \\\\%s...", destination);
                std::fflush(stdout);

                wchar_t machine[264] = {};
                ::swprintf(machine, std::size(machine), L"\\\\%s", destination);
                SC_HANDLE scmRemote = ::OpenSCManagerW(machine, L"ServicesActive", kScmFindAccess);
                if (scmRemote != nullptr)
                {
                    SC_HANDLE remoteService = OpenServiceByNameOrDisplay(scmRemote, serviceName, kQueryStatusAccess);
                    if (remoteService != nullptr)
                    {
                        SERVICE_STATUS status = {};
                        bool matches = includeInactive;
                        if (!matches)
                        {
                            ::QueryServiceStatus(remoteService, &status);
                            matches = status.dwCurrentState == SERVICE_RUNNING;
                        }
                        if (matches)
                        {
                            ClearLine();
                            if (!found)
                            {
                                std::wprintf(L"\rFound %s on:\n", serviceName);
                                found = true;
                            }
                            std::wprintf(L"\r%s\n", machine);
                        }
                        else
                        {
                            ClearLine();
                        }
                        ::CloseServiceHandle(remoteService);
                    }
                    else
                    {
                        ClearLine();
                    }
                    ::CloseServiceHandle(scmRemote);
                }
                else
                {
                    ClearLine();
                    std::wprintf(L"Unable to open Service Control Manager database on \\%s:\n", machine);
                    pstools::PrintSystemError(::GetLastError());
                }
            }
        }
        else
        {
            std::wprintf(L"Error browsing network:\n");
            pstools::PrintSystemError(rc);
        }
        if (buffer != nullptr)
        {
            ::NetApiBufferFree(buffer);
        }
        if (!found)
        {
            ClearLine();
            std::wprintf(includeInactive ? L"No service named %s found.\n" : L"No active service named %s found.\n",
                         serviceName);
        }
        std::wprintf(L"\n");
    }

    inline void CmdStart(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
    {
        if (args.empty() || IEq(args[0], L"-?") || IEq(args[0], L"/?"))
        {
            PrintUsage(prog, 4);
            return;
        }
        const wchar_t* serviceName = args[0].c_str();
        ScHandle service(OpenServiceByNameOrDisplay(scm, serviceName, kQueryStatusAccess | SERVICE_START));
        if (!service)
        {
            std::wprintf(L"Error opening %s on %s:\n", serviceName, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return;
        }
        std::vector<const wchar_t*> serviceArgs;
        for (size_t i = 1; i < args.size(); ++i)
        {
            serviceArgs.push_back(args[i].c_str());
        }
        if (::StartServiceW(service.Get(), static_cast<DWORD>(serviceArgs.size()),
                            serviceArgs.empty() ? nullptr : serviceArgs.data()))
        {
            PrintServiceStatus(service.Get(), serviceName, nullptr, false);
        }
        else
        {
            std::wprintf(L"Error starting %s on %s:\n", serviceName, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
        }
    }

    // Returns true when the control succeeded, so restart can proceed to start.
    [[nodiscard]] inline bool CmdControl(SC_HANDLE scm, int code, const std::vector<std::wstring>& args,
                                         const wchar_t* prog)
    {
        if (args.empty() || IEq(args[0], L"-?") || IEq(args[0], L"/?"))
        {
            PrintUsage(prog, code);
            return false;
        }
        const wchar_t* serviceName = args[0].c_str();

        DWORD control = SERVICE_CONTROL_STOP;
        DWORD extraAccess = SERVICE_STOP;
        if (code == 5)
        {
            control = SERVICE_CONTROL_PAUSE;
            extraAccess = SERVICE_PAUSE_CONTINUE;
        }
        else if (code == 6)
        {
            control = SERVICE_CONTROL_CONTINUE;
            extraAccess = SERVICE_PAUSE_CONTINUE;
        }

        ScHandle service(OpenServiceByNameOrDisplay(scm, serviceName, extraAccess | kQueryStatusAccess));
        if (!service)
        {
            std::wprintf(L"Error opening %s on %s:\n", serviceName, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return false;
        }

        SERVICE_STATUS status = {};
        if (::ControlService(service.Get(), control, &status))
        {
            if (code == 10)
            {
                if (::QueryServiceStatus(service.Get(), &status))
                {
                    while (status.dwCurrentState != SERVICE_STOPPED)
                    {
                        ::Sleep(500);
                        if (!::QueryServiceStatus(service.Get(), &status))
                        {
                            std::wprintf(L"Error querying service status:\n");
                            pstools::PrintSystemError(::GetLastError());
                            break;
                        }
                    }
                }
                else
                {
                    std::wprintf(L"Error querying service status:\n");
                    pstools::PrintSystemError(::GetLastError());
                }
            }
            else
            {
                PrintServiceStatus(service.Get(), serviceName, nullptr, false);
            }
        }
        else if (control != SERVICE_CONTROL_STOP || ::GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
        {
            std::wprintf(L"Error controlling %s on %s:\n", serviceName, g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return false;
        }
        return true;
    }

    inline void CmdSetConfig(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
    {
        const wchar_t* serviceName = nullptr;
        const wchar_t* startTypeArg = nullptr;
        bool tooMany = false;
        for (const std::wstring& a : args)
        {
            const wchar_t first = a.empty() ? 0 : a[0];
            if (first != L'-' && first != L'\\')
            {
                if (serviceName == nullptr)
                {
                    serviceName = a.c_str();
                }
                else if (startTypeArg == nullptr)
                {
                    startTypeArg = a.c_str();
                }
                else
                {
                    tooMany = true;
                }
            }
        }

        if (!tooMany && serviceName != nullptr && startTypeArg != nullptr)
        {
            DWORD startType = 0;
            bool valid = true;
            if (IEq(startTypeArg, L"demand"))
            {
                startType = SERVICE_DEMAND_START;
            }
            else if (IEq(startTypeArg, L"disabled"))
            {
                startType = SERVICE_DISABLED;
            }
            else if (IEq(startTypeArg, L"auto"))
            {
                startType = SERVICE_AUTO_START;
            }
            else
            {
                valid = false;
            }

            if (valid)
            {
                ScHandle service(OpenServiceByNameOrDisplay(scm, serviceName, SERVICE_CHANGE_CONFIG));
                if (service)
                {
                    if (::ChangeServiceConfigW(service.Get(), SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE, nullptr,
                                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
                    {
                        std::wprintf(L"Start type of %s on %s set to %s.\n", serviceName, g_machine.c_str(),
                                     startTypeArg);
                    }
                    else
                    {
                        std::wprintf(L"Error changing configuration for %s on %s\n", serviceName, g_machine.c_str());
                        pstools::PrintSystemError(::GetLastError());
                    }
                }
                else
                {
                    std::wprintf(L"Error opening %s for configuration change on %s\n", serviceName, g_machine.c_str());
                    pstools::PrintSystemError(::GetLastError());
                }
                return;
            }
        }
        PrintUsage(prog, 11);
    }

    inline void Dispatch(SC_HANDLE scm, int code, const std::vector<std::wstring>& args, const wchar_t* prog)
    {
        switch (code)
        {
        case 1:
        case 2:
            CmdQuery(scm, args, prog);
            break;
        case 3:
        case 5:
        case 6:
            static_cast<void>(CmdControl(scm, code, args, prog));
            break;
        case 4:
            CmdStart(scm, args, prog);
            break;
        case 7:
            CmdConfig(scm, args, prog);
            break;
        case 8:
            CmdDepend(scm, args, prog);
            break;
        case 9:
            CmdFind(scm, args, prog);
            break;
        case 10:
            if (CmdControl(scm, 10, args, prog))
            {
                CmdStart(scm, args, prog);
            }
            break;
        case 11:
            CmdSetConfig(scm, args, prog);
            break;
        case 12:
            CmdSecurity(scm, args, prog);
            break;
        default:
            break;
        }
    }
} // namespace psservice
