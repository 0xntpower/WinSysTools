// PsService shared helpers: the RAII SC_HANDLE, the service-access masks, the
// wide local pieces the ANSI PsTools header does not cover, and the SCM
// enumerate and open routines. See analysis/PsService.md for the sub_ map.
#pragma once

#include "../common/PsTools.h"

#include <windows.h>
#include <winsvc.h>
#include <sddl.h>
#include <lm.h>

#include <conio.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace psservice
{
    // Empty on the local path until wmain sets it to L"\\COMPUTERNAME".
    inline std::wstring g_machine;

    // Bound dynamically from advapi32 for the description, delayed-autostart,
    // and failure-action config that QueryServiceConfig cannot return.
    using QueryServiceConfig2WFn = BOOL(__stdcall*)(SC_HANDLE, DWORD, LPBYTE, DWORD, LPDWORD);
    inline QueryServiceConfig2WFn g_queryServiceConfig2W = nullptr;

    constexpr DWORD kQueryStatusAccess = SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_INTERROGATE;
    constexpr DWORD kConfigAccess = SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS;
    constexpr DWORD kSecurityAccess = READ_CONTROL | SERVICE_QUERY_CONFIG;
    constexpr DWORD kScmAccess = MAXIMUM_ALLOWED | SC_MANAGER_CONNECT;
    constexpr DWORD kScmFindAccess = SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE;

    constexpr DWORD kServiceWin32 = SERVICE_WIN32;
    constexpr DWORD kServiceDriverAndWin32 = SERVICE_DRIVER | SERVICE_WIN32;

    [[nodiscard]] inline bool IEq(const wchar_t* a, const wchar_t* b)
    {
        return ::_wcsicmp(a, b) == 0;
    }

    [[nodiscard]] inline bool IEq(const std::wstring& a, const wchar_t* b)
    {
        return ::_wcsicmp(a.c_str(), b) == 0;
    }

    [[nodiscard]] inline const wchar_t* BaseName(const wchar_t* path)
    {
        const wchar_t* slash = ::wcsrchr(path, L'\\');
        return slash != nullptr ? slash + 1 : path;
    }

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

    // Wide equivalent of pstools::StripFlag, removing the first matching argv entry.
    inline bool StripFlagW(int& argc, wchar_t** argv, const wchar_t* dashForm, const wchar_t* slashForm)
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

    [[nodiscard]] inline std::wstring LocalComputerNameW()
    {
        wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD cch = static_cast<DWORD>(std::size(name));
        if (!::GetComputerNameW(name, &cch))
        {
            return {};
        }
        return std::wstring(name, cch);
    }

    // Hidden password prompt (mirrors sub_140001520).
    [[nodiscard]] inline std::wstring PromptHiddenPasswordW()
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

    inline bool g_connected = false;

    // Connect to \\computer\IPC$ with explicit credentials (mirrors sub_140003370).
    [[nodiscard]] inline bool RemoteConnectW(const std::wstring& computer, const std::wstring& user,
                                             const std::wstring& password)
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

    inline void RemoteDisconnectW(const std::wstring& computer)
    {
        if (!g_connected)
        {
            return;
        }
        std::wstring remote = L"\\\\" + computer + L"\\IPC$";
        ::WNetCancelConnection2W(remote.c_str(), 0, TRUE);
    }

    // Erase the current console line on stderr. On a redirected stream the width
    // stays zero and this writes a bare CR, leaving the diffed stdout untouched.
    inline void ClearLine()
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

    [[nodiscard]] inline bool EnumServices(SC_HANDLE scm, DWORD type, DWORD state, std::vector<BYTE>& buffer,
                                           DWORD& returned)
    {
        DWORD needed = 0;
        DWORD resume = 0;
        returned = 0;
        ::EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, type, state, nullptr, 0, &needed, &returned, &resume,
                                nullptr);
        if (::GetLastError() != ERROR_MORE_DATA)
        {
            std::wprintf(L"Error querying services on %s:\n", g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return false;
        }
        buffer.resize(needed);
        if (!::EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, type, state, buffer.data(), needed, &needed, &returned,
                                     &resume, nullptr))
        {
            std::wprintf(L"Error querying services on %s:\n", g_machine.c_str());
            pstools::PrintSystemError(::GetLastError());
            return false;
        }
        return true;
    }

    [[nodiscard]] inline ENUM_SERVICE_STATUS_PROCESSW* Services(std::vector<BYTE>& buffer)
    {
        return reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
    }

    // Open by key name, falling back to a display-name match (mirrors sub_1400054D0, a1 = 0).
    [[nodiscard]] inline SC_HANDLE OpenServiceByNameOrDisplay(SC_HANDLE scm, const wchar_t* name, DWORD access)
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
} // namespace psservice
