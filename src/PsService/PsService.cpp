// PsService -- a service viewer and controller over the Service Control
// Manager. Source-accurate C++20 console reconstruction of the Sysinternals
// PsService64.exe v2.26.
//
// Behaviour recovered with IDA (idalib) from PsService64.exe:
//   wmain / dispatch driver      main 0x1400070E0
//   command-line parser          sub_140004E10
//   usage printer                sub_140006D00
//   command dispatcher           sub_140005600
//   open SCM (local or remote)   sub_140005490 / OpenSCManagerW
//   enumerate services           sub_140005170 (EnumServicesStatusExW)
//   open one service by name     sub_1400054D0 (OpenServiceW, display-name fallback)
//   status printer (query)       sub_140004000 (QueryServiceConfigW + QueryServiceStatus)
//   config printer               sub_140003BA0 (QueryServiceConfigW + QueryServiceConfig2W)
//   security printer             sub_140004450 (QueryServiceObjectSecurity + DACL)
//   query handler                sub_140006080
//   config handler               sub_140006480
//   security handler             sub_1400066C0
//   depend handler               sub_1400059D0 (EnumDependentServicesW)
//   find handler                 sub_140005C90 (NetServerEnum + per-server query)
//   start handler                sub_140006B10 (StartServiceW)
//   stop / pause / cont handler  sub_1400057A0 (ControlService)
//   setconfig handler            sub_1400068D0 (ChangeServiceConfigW)
//   SID to string fallback       sub_1400052C0 (manual, matches ConvertSidToStringSid)
//   clear-line progress eraser   sub_140002F30 / sub_140003310
//   print system error           sub_140003240 (FormatMessage, netmsg.dll for net codes)
//
// The shared PsTools front end (startup banner, EULA gate, and the system
// error printer) comes from ../common/PsTools.h, reused read-only. PsService
// is a fully Unicode tool while that header is ANSI-first, so the wide pieces
// it does not cover (argument parsing, the wide local name, remote credential
// connect, and the hidden password prompt) are implemented locally here. Each
// such local piece is marked below.
//
// The SID-to-string fallback uses ConvertSidToStringSidW, whose output matches
// the original's hand-built format byte for byte (decimal identifier authority,
// or a 12-digit 0x form when the top word is set), so the code stays idiomatic.

#include "../common/PsTools.h"

#include <windows.h>
#include <winsvc.h>
#include <sddl.h>
#include <lm.h>

#include <clocale>
#include <conio.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace
{
    // Version-resource fields of PsService64.exe v2.26, used for the banner and
    // the EULA registry key. The original reads these from its own version
    // resource at run time. Here they are fixed, which does not change output.
    constexpr pstools::Banner kBanner{
        "PsService",
        "2.26",
        "Service information and configuration utility",
        "Copyright (C) 2001-2023 Mark Russinovich",
        "Sysinternals - www.sysinternals.com",
    };

    constexpr char kApp[] = "PsService";

    // The machine string used in the "... on %s" messages. Empty on the local
    // path until main sets it to L"\\COMPUTERNAME", matching lpMachineName.
    std::wstring g_machine;

    // Bound dynamically from advapi32 like the original, present on every
    // supported system. Supplies the description, delayed-autostart, and
    // failure-action config that QueryServiceConfig cannot return.
    using QueryServiceConfig2WFn = BOOL(__stdcall*)(SC_HANDLE, DWORD, LPBYTE, DWORD, LPDWORD);
    QueryServiceConfig2WFn g_queryServiceConfig2W = nullptr;

    // Service-access combination the status and config printers open with.
    constexpr DWORD kQueryStatusAccess =
        SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_INTERROGATE;  // 0x85
    constexpr DWORD kConfigAccess = SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS;  // 0x05
    constexpr DWORD kSecurityAccess = READ_CONTROL | SERVICE_QUERY_CONFIG;        // 0x20001
    constexpr DWORD kScmAccess = MAXIMUM_ALLOWED | SC_MANAGER_CONNECT;            // 0x2000001
    constexpr DWORD kScmFindAccess = SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE;  // 0x05

    constexpr DWORD kServiceWin32 = SERVICE_WIN32;                 // 0x30
    constexpr DWORD kServiceDriverAndWin32 = SERVICE_DRIVER | SERVICE_WIN32;  // 0x3B

    // Failure-action labels, indexed by SC_ACTION_TYPE. Recovered as a
    // WCHAR[4][256] table at 0x140049190.
    const wchar_t* const kActionLabels[] = { L"None", L"Restart", L"Reboot", L"Run command" };

    // --- small utilities ---------------------------------------------------

    [[nodiscard]] bool IEq(const wchar_t* a, const wchar_t* b)
    {
        return ::_wcsicmp(a, b) == 0;
    }

    [[nodiscard]] bool IEq(const std::wstring& a, const wchar_t* b)
    {
        return ::_wcsicmp(a.c_str(), b) == 0;
    }

    [[nodiscard]] const wchar_t* BaseName(const wchar_t* path)
    {
        const wchar_t* slash = ::wcsrchr(path, L'\\');
        return slash != nullptr ? slash + 1 : path;
    }

    // RAII wrapper for an SC_HANDLE.
    class ScHandle
    {
    public:
        ScHandle() = default;

        explicit ScHandle(SC_HANDLE h)
            : handle_(h)
        {
        }

        ~ScHandle()
        {
            if (handle_ != nullptr)
            {
                ::CloseServiceHandle(handle_);
            }
        }

        ScHandle(const ScHandle&) = delete;
        ScHandle& operator=(const ScHandle&) = delete;

        ScHandle(ScHandle&& other) noexcept
            : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }

        ScHandle& operator=(ScHandle&& other) noexcept
        {
            if (this != &other)
            {
                if (handle_ != nullptr)
                {
                    ::CloseServiceHandle(handle_);
                }
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        [[nodiscard]] SC_HANDLE Get() const
        {
            return handle_;
        }

        explicit operator bool() const
        {
            return handle_ != nullptr;
        }

    private:
        SC_HANDLE handle_ = nullptr;
    };

    // Wide equivalent of pstools::StripFlag. Removes the first argv entry that
    // equals either spelling, shifting the rest down (local, PsService is wide).
    bool StripFlagW(int& argc, wchar_t** argv, const wchar_t* dashForm, const wchar_t* slashForm)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (IEq(argv[i], dashForm) || IEq(argv[i], slashForm))
            {
                for (int j = i; j < argc - 1; ++j)
                {
                    argv[j] = argv[j + 1];
                }
                --argc;
                return true;
            }
        }
        return false;
    }

    // Wide local computer name (local, the header's helper is ANSI).
    [[nodiscard]] std::wstring LocalComputerNameW()
    {
        wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD cch = static_cast<DWORD>(std::size(name));
        if (!::GetComputerNameW(name, &cch))
        {
            return {};
        }
        return std::wstring(name, cch);
    }

    // Hidden password prompt (local wide form, mirrors sub_140001520).
    [[nodiscard]] std::wstring PromptHiddenPasswordW()
    {
        std::wprintf(L"Password: ");
        std::fflush(stdout);
        std::wstring pw;
        for (wint_t ch = ::_getwch(); ch != L'\r'; ch = ::_getwch())
        {
            pw.push_back(static_cast<wchar_t>(ch));
        }
        std::wprintf(L"\n");
        return pw;
    }

    // Wide connect and disconnect to \\computer\IPC$ (local, mirrors
    // sub_140003370 / sub_140003430). The computer is the bare name.
    bool g_connected = false;

    [[nodiscard]] bool RemoteConnectW(const std::wstring& computer, const std::wstring& user, const std::wstring& password)
    {
        std::wstring remote = L"\\\\" + computer + L"\\IPC$";
        NETRESOURCEW nr = {};
        nr.dwType = RESOURCETYPE_ANY;
        nr.lpRemoteName = remote.data();
        const DWORD rc = ::WNetAddConnection2W(&nr, password.c_str(), user.c_str(), 0);
        if (rc == NO_ERROR)
        {
            g_connected = true;
            return true;
        }
        ::SetLastError(rc);
        return false;
    }

    void RemoteDisconnectW(const std::wstring& computer)
    {
        if (!g_connected)
        {
            return;
        }
        std::wstring remote = L"\\\\" + computer + L"\\IPC$";
        ::WNetCancelConnection2W(remote.c_str(), 0, TRUE);
    }

    // Erase the current console line on stderr (mirrors sub_140002F30). The
    // width is read once from the output buffer. On a redirected stream the
    // width stays zero and this writes a bare carriage return, which leaves the
    // diffed stdout untouched.
    void ClearLine()
    {
        static CONSOLE_SCREEN_BUFFER_INFO info = {};
        if (info.dwSize.X == 0)
        {
            ::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info);
        }
        std::fputws(L"\r", stderr);
        for (int i = 0; i < info.dwSize.X - 1; ++i)
        {
            std::fputws(L" ", stderr);
        }
        std::fputws(L"\r", stderr);
    }

    // --- usage (mirrors sub_140006D00) -------------------------------------

    void PrintUsage(const wchar_t* prog, int code)
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
            std::wprintf(L"Sets the start type of the specified service where start-type\nis one of auto, demand and disabled.\n");
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

    // --- enumeration and open (mirror sub_140005170 / sub_1400054D0) -------

    [[nodiscard]] bool EnumServices(SC_HANDLE scm, DWORD type, DWORD state, std::vector<BYTE>& buffer, DWORD& returned)
    {
        DWORD needed = 0;
        DWORD resume = 0;
        returned = 0;
        ::EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, type, state, nullptr, 0,
                                &needed, &returned, &resume, nullptr);
        if (::GetLastError() != ERROR_MORE_DATA)
        {
            std::wprintf(L"Error querying services on %s:\n", g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return false;
        }
        buffer.resize(needed);
        if (!::EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, type, state, buffer.data(), needed,
                                     &needed, &returned, &resume, nullptr))
        {
            std::wprintf(L"Error querying services on %s:\n", g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return false;
        }
        return true;
    }

    [[nodiscard]] ENUM_SERVICE_STATUS_PROCESSW* Services(std::vector<BYTE>& buffer)
    {
        return reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
    }

    // Open a service by name, with the original's fallback: when the name is not
    // a service key, enumerate win32 services and open the one whose display
    // name matches case-insensitively. Mirrors sub_1400054D0 with a1 = 0.
    [[nodiscard]] SC_HANDLE OpenServiceByNameOrDisplay(SC_HANDLE scm, const wchar_t* name, DWORD access)
    {
        SC_HANDLE direct = ::OpenServiceW(scm, name, access);
        if (direct != nullptr)
        {
            return direct;
        }
        const DWORD saved = ::GetLastError();
        std::vector<BYTE> buffer;
        DWORD count = 0;
        if (!EnumServices(scm, kServiceWin32, SERVICE_STATE_ALL, buffer, count))
        {
            ::SetLastError(saved);
            return nullptr;
        }
        SC_HANDLE found = nullptr;
        ENUM_SERVICE_STATUS_PROCESSW* list = Services(buffer);
        for (DWORD i = 0; i < count; ++i)
        {
            if (IEq(name, list[i].lpDisplayName))
            {
                found = ::OpenServiceW(scm, list[i].lpServiceName, access);
                break;
            }
        }
        if (found == nullptr)
        {
            ::SetLastError(saved);
        }
        return found;
    }

    // --- status printer (mirrors sub_140004000) ----------------------------

    void PrintServiceType(DWORD type)
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

    // Print the description from SERVICE_CONFIG_DESCRIPTION, stripping a single
    // trailing CR/LF the way the status printer does when the text is long.
    void PrintDescriptionStripped(SC_HANDLE service)
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

    void PrintServiceStatus(SC_HANDLE service, const wchar_t* name, const wchar_t* groupFilter, bool interactiveOnly)
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
        case SERVICE_STOPPED:          std::wprintf(L"STOPPED\n"); break;
        case SERVICE_START_PENDING:    std::wprintf(L"START_PENDING\n"); break;
        case SERVICE_STOP_PENDING:     std::wprintf(L"STOP_PENDING\n"); break;
        case SERVICE_RUNNING:          std::wprintf(L"RUNNING\n"); break;
        case SERVICE_CONTINUE_PENDING: std::wprintf(L"CONTINUE_PENDING\n"); break;
        case SERVICE_PAUSE_PENDING:    std::wprintf(L"PAUSE_PENDING\n"); break;
        case SERVICE_PAUSED:           std::wprintf(L"PAUSED\n"); break;
        default:                       break;
        }

        std::wprintf(L"\t\t\t       ");
        std::wprintf((status.dwControlsAccepted & SERVICE_ACCEPT_STOP) != 0 ? L"(STOPPABLE," : L"(NOT_STOPPABLE,");
        std::wprintf((status.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) != 0 ? L"PAUSABLE," : L"NOT_PAUSABLE,");
        if ((status.dwControlsAccepted & SERVICE_ACCEPT_PRESHUTDOWN) != 0)
        {
            std::wprintf(L"ACCEPTS_PRESHUTDOWN)\n");
        }
        else
        {
            std::wprintf((status.dwControlsAccepted & SERVICE_ACCEPT_SHUTDOWN) != 0 ? L"ACCEPTS_SHUTDOWN)\n" : L"IGNORES_SHUTDOWN)\n");
        }

        std::wprintf(L"\tWIN32_EXIT_CODE\t  : %-2d (0x%x)\n", status.dwWin32ExitCode, status.dwWin32ExitCode);
        std::wprintf(L"\tSERVICE_EXIT_CODE : %-2d (0x%x)\n", status.dwServiceSpecificExitCode, status.dwServiceSpecificExitCode);
        std::wprintf(L"\tCHECKPOINT\t  : 0x%x\n", status.dwCheckPoint);
        std::wprintf(L"\tWAIT_HINT\t  : %d ms\n", status.dwWaitHint);
        std::wprintf(L"\n");
    }

    // --- config printer (mirrors sub_140003BA0) ----------------------------

    void PrintStartType(DWORD startType, bool delayed)
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

    void PrintErrorControl(DWORD errorControl)
    {
        std::wprintf(L"\tERROR_CONTROL\t  : %-2x ", errorControl);
        switch (errorControl)
        {
        case SERVICE_ERROR_IGNORE:   std::wprintf(L"IGNORE\n"); break;
        case SERVICE_ERROR_NORMAL:   std::wprintf(L"NORMAL\n"); break;
        case SERVICE_ERROR_SEVERE:   std::wprintf(L"SEVERE\n"); break;
        case SERVICE_ERROR_CRITICAL: std::wprintf(L"CRITICAL\n"); break;
        default:                     break;
        }
    }

    void PrintFailureActions(SC_HANDLE service)
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
            // The original prints the raw SC_ACTION.Delay field under a "seconds"
            // label without converting from its millisecond unit. Matched here.
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

    void PrintServiceConfig(SC_HANDLE service, const wchar_t* name)
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

    // --- security printer (mirrors sub_140004450) --------------------------

    [[nodiscard]] std::wstring AceAccountName(PSID sid)
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

    void PrintAceAccess(DWORD mask)
    {
        if (mask == SERVICE_ALL_ACCESS)
        {
            std::wprintf(L"\t        All\n");
            return;
        }
        GENERIC_MAPPING mapping = { 0x0002008Du, 0x00020002u, 0x00020170u, 0x000F01FFu };
        ::MapGenericMask(&mask, &mapping);
        if ((mask & SERVICE_QUERY_STATUS) != 0)         { std::wprintf(L"\t        Query status\n"); }
        if ((mask & SERVICE_QUERY_CONFIG) != 0)         { std::wprintf(L"\t        Query Config\n"); }
        if ((mask & SERVICE_CHANGE_CONFIG) != 0)        { std::wprintf(L"\t        Change Config\n"); }
        if ((mask & SERVICE_INTERROGATE) != 0)          { std::wprintf(L"\t        Interrogate\n"); }
        if ((mask & SERVICE_ENUMERATE_DEPENDENTS) != 0) { std::wprintf(L"\t        Enumerate Dependents\n"); }
        if ((mask & SERVICE_PAUSE_CONTINUE) != 0)       { std::wprintf(L"\t        Pause/Resume\n"); }
        if ((mask & SERVICE_START) != 0)                { std::wprintf(L"\t        Start\n"); }
        if ((mask & SERVICE_STOP) != 0)                 { std::wprintf(L"\t        Stop\n"); }
        if ((mask & SERVICE_USER_DEFINED_CONTROL) != 0) { std::wprintf(L"\t        User-Defined Control\n"); }
        if ((mask & READ_CONTROL) != 0)                 { std::wprintf(L"\t        Read Permissions\n"); }
        if ((mask & WRITE_DAC) != 0)                    { std::wprintf(L"\t        Change Permissions\n"); }
        if ((mask & WRITE_OWNER) != 0)                  { std::wprintf(L"\t        Change Owner\n"); }
    }

    void PrintServiceSecurity(SC_HANDLE service, const wchar_t* name)
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

    // --- command handlers --------------------------------------------------

    void CmdQuery(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
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
                if (IEq(value, L"driver")) { type = kServiceDriverAndWin32 & SERVICE_DRIVER; }
                else if (IEq(value, L"service")) { type = kServiceWin32; }
                else if (IEq(value, L"interactive")) { interactive = true; }
                else if (IEq(value, L"all")) { type = kServiceDriverAndWin32; }
                else { PrintUsage(prog, 1); return; }
            }
            else if (IEq(a, L"-s") || IEq(a, L"/s") || IEq(a, L"state="))
            {
                if (++i >= args.size())
                {
                    PrintUsage(prog, 1);
                    return;
                }
                const std::wstring& value = args[i];
                if (IEq(value, L"active")) { state = SERVICE_ACTIVE; }
                else if (IEq(value, L"inactive")) { state = SERVICE_INACTIVE; }
                else if (IEq(value, L"all")) { state = SERVICE_STATE_ALL; }
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
                if (::_wcsnicmp(serviceName, list[i].lpServiceName, len) != 0
                    && ::_wcsnicmp(serviceName, list[i].lpDisplayName, len) != 0)
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

    void CmdConfig(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
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
                if (::_wcsnicmp(serviceName, list[i].lpServiceName, len) != 0
                    && ::_wcsnicmp(serviceName, list[i].lpDisplayName, len) != 0)
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

    void CmdSecurity(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
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
            if (serviceName != nullptr
                && ::_wcsicmp(serviceName, list[i].lpServiceName) != 0
                && ::_wcsicmp(serviceName, list[i].lpDisplayName) != 0)
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

    void CmdDepend(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
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

    void CmdFind(SC_HANDLE, const std::vector<std::wstring>& args, const wchar_t* prog)
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
        const DWORD rc = ::NetServerEnum(nullptr, 100, &buffer, MAX_PREFERRED_LENGTH,
                                         &entries, &total, SV_TYPE_NT, nullptr, &resume);
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
            std::wprintf(includeInactive ? L"No service named %s found.\n" : L"No active service named %s found.\n", serviceName);
        }
        std::wprintf(L"\n");
    }

    void CmdStart(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
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

    // Returns true when the control succeeded (so restart can proceed to start).
    [[nodiscard]] bool CmdControl(SC_HANDLE scm, int code, const std::vector<std::wstring>& args, const wchar_t* prog)
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

    void CmdSetConfig(SC_HANDLE scm, const std::vector<std::wstring>& args, const wchar_t* prog)
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
            if (IEq(startTypeArg, L"demand")) { startType = SERVICE_DEMAND_START; }
            else if (IEq(startTypeArg, L"disabled")) { startType = SERVICE_DISABLED; }
            else if (IEq(startTypeArg, L"auto")) { startType = SERVICE_AUTO_START; }
            else { valid = false; }

            if (valid)
            {
                ScHandle service(OpenServiceByNameOrDisplay(scm, serviceName, SERVICE_CHANGE_CONFIG));
                if (service)
                {
                    if (::ChangeServiceConfigW(service.Get(), SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE,
                                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
                    {
                        std::wprintf(L"Start type of %s on %s set to %s.\n", serviceName, g_machine.c_str(), startTypeArg);
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

    void Dispatch(SC_HANDLE scm, int code, const std::vector<std::wstring>& args, const wchar_t* prog)
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

    // --- command line (mirrors sub_140004E10) ------------------------------

    [[nodiscard]] int CommandCode(const wchar_t* a)
    {
        if (IEq(a, L"query")) { return 1; }
        if (IEq(a, L"queryeex") || IEq(a, L"interrogate")) { return 2; }
        if (IEq(a, L"restart")) { return 10; }
        if (IEq(a, L"start")) { return 4; }
        if (IEq(a, L"stop")) { return 3; }
        if (IEq(a, L"pause")) { return 5; }
        if (IEq(a, L"setconfig")) { return 11; }
        if (IEq(a, L"cont") || IEq(a, L"continue")) { return 6; }
        if (IEq(a, L"sec") || IEq(a, L"security")) { return 12; }
        if (IEq(a, L"config")) { return 7; }
        if (IEq(a, L"depend") || IEq(a, L"enumdepend")) { return 8; }
        if (IEq(a, L"find")) { return 9; }
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

    [[nodiscard]] bool ParseCommandLine(int argc, wchar_t** argv, CommandLine& cmd)
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
}

int wmain(int argc, wchar_t** argv)
{
    std::setlocale(LC_ALL, "");

    const bool noBanner = StripFlagW(argc, argv, L"-nobanner", L"/nobanner");
    pstools::PrintBanner(kBanner, noBanner);

    const bool acceptFlag = StripFlagW(argc, argv, L"-accepteula", L"/accepteula");
    if (!pstools::AcceptEula(kApp, acceptFlag))
    {
        std::fprintf(stderr, "Eula declined.\n\n");
        return 1;
    }

    const HMODULE advapi = ::GetModuleHandleW(L"advapi32.dll");
    g_queryServiceConfig2W = reinterpret_cast<QueryServiceConfig2WFn>(
        reinterpret_cast<void*>(::GetProcAddress(advapi, "QueryServiceConfig2W")));

    CommandLine cmd;
    if (!ParseCommandLine(argc, argv, cmd))
    {
        PrintUsage(argv[0], 0);
        return -1;
    }

    if (cmd.haveUser && !cmd.havePassword)
    {
        cmd.password = PromptHiddenPasswordW();
        cmd.havePassword = true;
    }

    const std::wstring local = LocalComputerNameW();
    bool remote = false;
    if (cmd.haveMachine && cmd.machine.size() > 2)
    {
        remote = ::_wcsicmp(cmd.machine.c_str() + 2, local.c_str()) != 0;
    }

    if (!remote)
    {
        ScHandle scm(::OpenSCManagerW(nullptr, L"ServicesActive", kScmAccess));
        if (!scm)
        {
            std::wprintf(L"Unable to access Service Control Manager:\n");
            pstools::PrintSystemError(::GetLastError());
            return -1;
        }
        g_machine = L"\\\\" + local;
        Dispatch(scm.Get(), cmd.code, cmd.args, argv[0]);
        return 0;
    }

    const std::wstring bareName = cmd.machine.substr(2);
    if (cmd.haveUser)
    {
        if (!RemoteConnectW(bareName, cmd.user, cmd.password))
        {
            std::wprintf(L"Unable to connect to %s:\n", cmd.machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return -1;
        }
    }

    g_machine = cmd.machine;
    ScHandle scm(::OpenSCManagerW(cmd.machine.c_str(), L"ServicesActive", kScmAccess));
    if (scm)
    {
        Dispatch(scm.Get(), cmd.code, cmd.args, argv[0]);
        if (cmd.haveUser)
        {
            RemoteDisconnectW(bareName);
        }
        return 0;
    }

    std::wprintf(L"Unable to access Service Control Manager on %s:\n", g_machine.c_str());
    pstools::PrintSystemError(::GetLastError());
    if (cmd.haveUser)
    {
        RemoteDisconnectW(bareName);
    }
    return -1;
}
