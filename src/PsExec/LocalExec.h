// PsExec local direct-execution path (mirrors sub_140012860): the limited-user
// token builder for -l, the suspended-process priority and affinity apply, and
// the CreateProcess driver that waits for the exit code or detaches with -d.
#pragma once

#include "PsExecCommon.h"

namespace psexec
{
    // Build a limited-user token from base. Mirrors sub_140002F30: disable the
    // Administrators group, delete every privilege the token holds that the Users
    // account is not granted, drop the token to Low integrity, and grant the user
    // full access to the token's own default DACL. Returns nullptr on failure.
    [[nodiscard]] inline HANDLE CreateLimitedUserToken(HANDLE base)
    {
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

        PSID adminSid = nullptr;
        if (!::AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0,
                                        0, 0, 0, &adminSid))
        {
            return nullptr;
        }
        PSID usersSid = nullptr;
        if (!::AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0,
                                        0, 0, 0, &usersSid))
        {
            ::FreeSid(adminSid);
            return nullptr;
        }

        // The privileges the Users account is granted, recovered through LSA. The
        // original falls back to this fixed list when the query returns nothing.
        std::vector<LUID> keepLuids;
        {
            LSA_OBJECT_ATTRIBUTES oa = {};
            LSA_HANDLE policy = nullptr;
            if (::LsaOpenPolicy(nullptr, &oa, POLICY_LOOKUP_NAMES, &policy) == 0)
            {
                PLSA_UNICODE_STRING rights = nullptr;
                ULONG count = 0;
                if (::LsaEnumerateAccountRights(policy, usersSid, &rights, &count) == 0 && count != 0)
                {
                    for (ULONG k = 0; k < count; ++k)
                    {
                        LUID luid = {};
                        if (::LookupPrivilegeValueW(nullptr, rights[k].Buffer, &luid))
                        {
                            keepLuids.push_back(luid);
                        }
                    }
                    ::LsaFreeMemory(rights);
                }
                ::LsaClose(policy);
            }
        }
        if (keepLuids.empty())
        {
            static const wchar_t* const kFallback[] = {
                L"SeShutdownPrivilege",           L"SeChangeNotifyPrivilege", L"SeUndockPrivilege",
                L"SeIncreaseWorkingSetPrivilege", L"SeTimeZonePrivilege",
            };
            for (const wchar_t* name : kFallback)
            {
                LUID luid = {};
                if (::LookupPrivilegeValueW(nullptr, name, &luid))
                {
                    keepLuids.push_back(luid);
                }
            }
        }

        // The token's current privileges, so the ones to delete are those not kept.
        DWORD needed = 0;
        ::GetTokenInformation(base, TokenPrivileges, nullptr, 0, &needed);
        std::vector<BYTE> buffer(needed != 0 ? needed : 1);
        if (!::GetTokenInformation(base, TokenPrivileges, buffer.data(), static_cast<DWORD>(buffer.size()), &needed))
        {
            ::FreeSid(adminSid);
            ::FreeSid(usersSid);
            return nullptr;
        }
        const auto* privileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(buffer.data());

        std::vector<LUID_AND_ATTRIBUTES> deletePrivs;
        for (DWORD k = 0; k < privileges->PrivilegeCount; ++k)
        {
            const LUID luid = privileges->Privileges[k].Luid;
            bool keep = false;
            for (const LUID& kept : keepLuids)
            {
                if (kept.LowPart == luid.LowPart && kept.HighPart == luid.HighPart)
                {
                    keep = true;
                    break;
                }
            }
            if (!keep)
            {
                LUID_AND_ATTRIBUTES la = {};
                la.Luid = luid;
                deletePrivs.push_back(la);
            }
        }

        SID_AND_ATTRIBUTES disable = {};
        disable.Sid = adminSid;

        HANDLE restricted = nullptr;
        const BOOL ok =
            ::CreateRestrictedToken(base, 0, 1, &disable, static_cast<DWORD>(deletePrivs.size()),
                                    deletePrivs.empty() ? nullptr : deletePrivs.data(), 0, nullptr, &restricted);

        ::FreeSid(adminSid);
        ::FreeSid(usersSid);
        if (!ok || restricted == nullptr)
        {
            return nullptr;
        }

        // Drop to Low integrity, matching the original's S-1-16-4096 assignment.
        PSID lowSid = nullptr;
        if (::ConvertStringSidToSidW(L"S-1-16-4096", &lowSid))
        {
            TOKEN_MANDATORY_LABEL label = {};
            label.Label.Sid = lowSid;
            label.Label.Attributes = SE_GROUP_INTEGRITY;
            const DWORD cb = sizeof(TOKEN_MANDATORY_LABEL) + ::GetLengthSid(lowSid);
            ::SetTokenInformation(restricted, TokenIntegrityLevel, &label, cb);
            ::LocalFree(lowSid);
        }
        return restricted;
    }

    // Apply the chosen priority and affinity to a freshly created, still-suspended
    // process, then resume it. Mirrors the tail of sub_140012860. Returns false and
    // fills err when the group-affinity call fails, in which case the process has
    // already been terminated and its handles closed.
    [[nodiscard]] inline bool ApplyAndResume(const Options& o, const PROCESS_INFORMATION& pi, DWORD& err)
    {
        if (o.priorityClass == kBackgroundClass)
        {
            ::SetPriorityClass(pi.hProcess, kBackgroundClass);
        }

        if (!o.groupSpecified)
        {
            if (o.affinityMask != 0)
            {
                ::SetProcessAffinityMask(pi.hProcess, o.affinityMask);
            }
        }
        else
        {
            GROUP_AFFINITY ga = {};
            ga.Group = o.group;
            ga.Mask = o.affinityMask != 0 ? static_cast<KAFFINITY>(o.affinityMask) : static_cast<KAFFINITY>(0xFFFF);
            if (!::SetThreadGroupAffinity(pi.hThread, &ga, nullptr))
            {
                err = ::GetLastError();
                std::fprintf(stderr, "\rPsExec could not set group affinity %s:\n", o.app.c_str());
                pstools::PrintSystemError(err);
                ::TerminateProcess(pi.hProcess, 0);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
                return false;
            }
        }
        ::ResumeThread(pi.hThread);
        return true;
    }

    // Run the command on the local machine directly. Mirrors sub_140012860: build
    // the "app" args command line, create the process with CreateProcessWithLogonW
    // when alternate credentials are supplied, otherwise with CreateProcessAsUserW
    // off the caller's token (optionally restricted for -l), then apply priority
    // and affinity, resume, and either wait for the exit code or, with -d, report
    // the process id. Returns the process exit code, the process id for -d, or the
    // Win32 error on a launch failure.
    [[nodiscard]] inline int RunLocal(const Options& o)
    {
        const std::string cmdLine = "\"" + o.app + "\" " + o.args;
        std::wstring wcmd = Widen(cmdLine);
        std::wstring wcwd;
        const wchar_t* cwd = nullptr;
        if (o.haveWorkDir && !o.workDir.empty())
        {
            wcwd = Widen(o.workDir);
            cwd = wcwd.c_str();
        }
        const DWORD flags = o.priorityClass | kCreateFlags;

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        bool created = false;

        if (o.haveUser && !o.limited)
        {
            std::string account;
            std::string domain;
            SplitUser(o.user, account, domain);
            const std::wstring wuser = Widen(account);
            const std::wstring wdomain = Widen(domain);
            const std::wstring wpass = Widen(o.password);
            const DWORD logonFlags = o.noProfile ? 0u : static_cast<DWORD>(LOGON_WITH_PROFILE);
            created =
                ::CreateProcessWithLogonW(wuser.c_str(), domain.empty() ? nullptr : wdomain.c_str(), wpass.c_str(),
                                          logonFlags, nullptr, wcmd.data(), flags, nullptr, cwd, &si, &pi) != 0;
        }
        else
        {
            ScopedHandle base;
            ::OpenProcessToken(::GetCurrentProcess(), kTokenAccess, base.Address());
            ScopedHandle runToken;
            if (o.limited)
            {
                HANDLE limited = CreateLimitedUserToken(base.Get());
                if (limited == nullptr)
                {
                    const DWORD err = ::GetLastError();
                    std::fprintf(stderr, "\rPsExec could not create a limited user token:\n");
                    pstools::PrintSystemError(err);
                    return static_cast<int>(err);
                }
                runToken.Reset(limited);
            }
            const HANDLE useToken = o.limited ? runToken.Get() : base.Get();
            created = ::CreateProcessAsUserW(useToken, nullptr, wcmd.data(), nullptr, nullptr, FALSE, flags, nullptr,
                                             cwd, &si, &pi) != 0;
        }

        if (!created)
        {
            const DWORD err = ::GetLastError();
            std::fprintf(stderr, "\rPsExec could not start %s:\n", o.app.c_str());
            pstools::PrintSystemError(err);
            return static_cast<int>(err);
        }

        DWORD affinityErr = 0;
        if (!ApplyAndResume(o, pi, affinityErr))
        {
            return static_cast<int>(affinityErr);
        }

        int result = 0;
        if (o.dontWait)
        {
            std::fprintf(stderr, "%s started with process ID %d.\n", o.app.c_str(), static_cast<int>(pi.dwProcessId));
            result = static_cast<int>(pi.dwProcessId);
        }
        else
        {
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 0;
            ::GetExitCodeProcess(pi.hProcess, &code);
            std::fprintf(stderr, "%s exited with error code %d.\n", o.app.c_str(), static_cast<int>(code));
            result = static_cast<int>(code);
        }
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        return result;
    }
} // namespace psexec
