// ProcessList.h -- running-process enumeration for the Select Process picker.
//
// Lists processes with name, PID, owning user, private bytes, working set and
// architecture, matching the columns the original picker shows.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include "MemSnapshot.h"

namespace vm
{
    struct ProcInfo
    {
        DWORD pid = 0;
        std::wstring name;
        std::wstring user;
        std::wstring arch;
        uint64_t privateBytes = 0;
        uint64_t workingSet = 0;
    };

    namespace detail
    {
        [[nodiscard]] inline std::wstring ProcessUser(HANDLE proc)
        {
            HANDLE rawTok = nullptr;
            if (!::OpenProcessToken(proc, TOKEN_QUERY, &rawTok))
            {
                return std::wstring();
            }
            Handle token(rawTok);
            DWORD len = 0;
            ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &len);
            if (len == 0)
            {
                return std::wstring();
            }
            std::vector<BYTE> buf(len);
            if (!::GetTokenInformation(token.get(), TokenUser, buf.data(), len, &len))
            {
                return std::wstring();
            }
            auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
            wchar_t account[256] = {};
            wchar_t domain[256] = {};
            DWORD accLen = static_cast<DWORD>(std::size(account));
            DWORD domLen = static_cast<DWORD>(std::size(domain));
            SID_NAME_USE use{};
            if (::LookupAccountSidW(nullptr, tu->User.Sid, account, &accLen, domain, &domLen, &use))
            {
                std::wstring result;
                if (domain[0])
                {
                    result = domain;
                    result += L"\\";
                }
                result += account;
                return result;
            }
            return std::wstring();
        }

        [[nodiscard]] inline std::wstring ProcessArch(HANDLE proc)
        {
            BOOL wow64 = FALSE;
            if (::IsWow64Process(proc, &wow64) && wow64)
            {
                return L"32-bit";
            }
            return L"64-bit";
        }
    } // namespace detail

    // Enumerate processes. The showAll toggle only affects which entries a
    // caller chooses to display. Processes that cannot be opened are still
    // listed, with blank user and sizes.
    [[nodiscard]] inline std::vector<ProcInfo> EnumProcesses()
    {
        detail::EnableDebugPrivilege();

        std::vector<ProcInfo> out;
        Handle snap(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        if (!snap.valid())
        {
            return out;
        }
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        for (BOOL more = ::Process32FirstW(snap.get(), &pe); more; more = ::Process32NextW(snap.get(), &pe))
        {
            ProcInfo p;
            p.pid = pe.th32ProcessID;
            p.name = pe.szExeFile;
            if (p.pid == 0)
            {
                continue;
            }

            Handle proc(::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p.pid));
            if (!proc.valid())
            {
                proc.reset(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid));
            }
            if (proc.valid())
            {
                p.user = detail::ProcessUser(proc.get());
                p.arch = detail::ProcessArch(proc.get());
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                pmc.cb = sizeof(pmc);
                if (::GetProcessMemoryInfo(proc.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
                {
                    p.privateBytes = pmc.PrivateUsage;
                    p.workingSet = pmc.WorkingSetSize;
                }
            }
            out.push_back(std::move(p));
        }

        std::sort(out.begin(), out.end(),
                  [](const ProcInfo& a, const ProcInfo& b)
                  {
                      int c = _wcsicmp(a.name.c_str(), b.name.c_str());
                      return c < 0 || (c == 0 && a.pid < b.pid);
                  });
        return out;
    }
} // namespace vm
