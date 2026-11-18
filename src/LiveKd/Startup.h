// LiveKd startup gates: the EULA flag check and accept (sub_1400018B0), the
// elevation check (sub_140008170), the named-privilege enabler (sub_140002240),
// and the native-live-dump support probe (sub_14000BDA0). See analysis/LiveKd.md.
#pragma once

#include "LiveKdCommon.h"

namespace livekd
{
    using RtlGetVersionFn = LONG(NTAPI*)(PRTL_OSVERSIONINFOW);

    // EULA gate (sub_1400018B0). Matches the standalone Sysinternals pattern: the
    // suite-wide flag under HKLM or HKCU\Software\Sysinternals, or the per-tool
    // flag under HKCU\Software\Sysinternals\LiveKd.
    constexpr wchar_t kSysinternalsKey[] = L"Software\\Sysinternals";
    constexpr wchar_t kLiveKdKey[] = L"Software\\Sysinternals\\LiveKd";

    [[nodiscard]] inline bool EulaFlagSet(HKEY root, const wchar_t* sub)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0;
        DWORD cb = sizeof(data);
        const LSTATUS st =
            RegQueryValueExW(key, L"EulaAccepted", nullptr, nullptr, reinterpret_cast<LPBYTE>(&data), &cb);
        RegCloseKey(key);
        return st == ERROR_SUCCESS && data != 0;
    }

    inline void WriteEulaAccepted()
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kLiveKdKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
            ERROR_SUCCESS)
        {
            const DWORD one = 1;
            RegSetValueExW(key, L"EulaAccepted", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
            RegCloseKey(key);
        }
    }

    // ponytail: the original pops a RichEdit GUI dialog on an interactive desktop
    // (sub_1400018B0). The console message is the faithful headless path for a
    // CLI tool, so the dialog is skipped by design.
    [[nodiscard]] inline bool EnsureEula(bool acceptFlag)
    {
        if (acceptFlag || EulaFlagSet(HKEY_LOCAL_MACHINE, kSysinternalsKey) ||
            EulaFlagSet(HKEY_CURRENT_USER, kSysinternalsKey) || EulaFlagSet(HKEY_CURRENT_USER, kLiveKdKey))
        {
            WriteEulaAccepted();
            return true;
        }
        std::fputs("\nLiveKd License Agreement\n", stderr);
        std::fputs("This is the first run of this program. "
                   "You must accept the EULA to continue.\n",
                   stderr);
        std::fputs("Use -accepteula to accept the EULA.\n\n", stderr);
        return false;
    }

    // Elevation check (sub_140008170). CheckTokenMembership against the effective
    // token reports the Administrators group as absent for a non-elevated admin,
    // which is the real "running elevated" test the original's token-group walk
    // performs.
    [[nodiscard]] inline bool IsRunningElevated()
    {
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        PSID adminSid = nullptr;
        if (!AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0,
                                      0, 0, &adminSid))
        {
            return false;
        }
        BOOL isMember = FALSE;
        if (!CheckTokenMembership(nullptr, adminSid, &isMember))
        {
            isMember = FALSE;
        }
        FreeSid(adminSid);
        return isMember != FALSE;
    }

    // Enable a named privilege on the process token (sub_140002240).
    [[nodiscard]] inline bool EnablePrivilege(const wchar_t* name)
    {
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &rawToken))
        {
            return false;
        }
        ScopedHandle token(rawToken);
        LUID luid{};
        if (!LookupPrivilegeValueW(nullptr, name, &luid))
        {
            return false;
        }
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!AdjustTokenPrivileges(token.Get(), FALSE, &tp, sizeof(tp), nullptr, nullptr))
        {
            return false;
        }
        return GetLastError() == ERROR_SUCCESS;
    }

    // Native live-dump support needs Windows 8.1 or above. RtlGetVersion is used in
    // place of the VersionHelpers macros because those honour the app manifest's
    // compatibility lie and would misreport the real build (sub_14000BDA0).
    [[nodiscard]] inline bool IsNativeLiveDumpSupported()
    {
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
        {
            return false;
        }
        const auto rtlGetVersion =
            reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (rtlGetVersion == nullptr)
        {
            return false;
        }
        RTL_OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        if (rtlGetVersion(&info) < 0)
        {
            return false;
        }
        if (info.dwMajorVersion > 6)
        {
            return true;
        }
        return info.dwMajorVersion == 6 && info.dwMinorVersion >= 3;
    }
} // namespace livekd
