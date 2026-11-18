// AutoLogon.h -- Winlogon autologon enable/disable logic, the LSA DefaultPassword
// secret handling, and the current-state read for the Autologon recreation.
//
// Recovered from Autologon64.exe:
//   enable  -- sub_140002910
//   disable -- the WM_COMMAND id 2 path of sub_140003020
//   read    -- the WM_INITDIALOG path of sub_140003020
//   eula    -- sub_140001460 / sub_140001500 / sub_140001DB0
//
// Registry values live under HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\
// Winlogon. The password is kept as the LSA private secret "DefaultPassword"
// through LsaStorePrivateData, and only falls back to a plaintext registry value
// when the LSA policy cannot be opened or written, exactly as the original does.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ntsecapi.h>

#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace autologon
{
    inline constexpr wchar_t kWinlogonKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
    inline constexpr wchar_t kControlKey[]  = L"System\\CurrentControlSet\\Control";
    inline constexpr wchar_t kDefaultUserName[]   = L"DefaultUserName";
    inline constexpr wchar_t kDefaultDomainName[] = L"DefaultDomainName";
    inline constexpr wchar_t kDefaultPassword[]   = L"DefaultPassword";
    inline constexpr wchar_t kAutoAdminLogon[]    = L"AutoAdminLogon";
    inline constexpr wchar_t kEasSubKey[]         = L"EAS";

    inline constexpr USHORT kMaxLsaBytes = 0xFFFD;
    inline constexpr DWORD  kFieldChars  = 256;

    enum class EnableResult
    {
        SuccessEncrypted = 0,   // LSA secret stored, no plaintext in the registry
        SuccessPlaintext,       // LSA store unavailable, password written as REG_SZ
        InvalidCredentials,
        OpenKeyFailed,
        WriteUserFailed,
        WriteDomainFailed,
        WritePasswordFailed,
        SetAutoAdminFailed
    };

    enum class DisableResult
    {
        Success = 0,
        OpenKeyFailed,
        SetAutoAdminFailed
    };

    struct CurrentState
    {
        std::wstring user;
        std::wstring domain;
        std::wstring password;
        bool keyOpened = false;
    };

    // RAII guard over an HKEY.
    class ScopedHKey
    {
    public:
        ScopedHKey() = default;
        ~ScopedHKey() { reset(); }
        ScopedHKey(const ScopedHKey&) = delete;
        ScopedHKey& operator=(const ScopedHKey&) = delete;

        void reset() noexcept
        {
            if (key_ != nullptr)
            {
                ::RegCloseKey(key_);
                key_ = nullptr;
            }
        }

        [[nodiscard]] HKEY* put() noexcept { return &key_; }
        [[nodiscard]] HKEY get() const noexcept { return key_; }
        [[nodiscard]] explicit operator bool() const noexcept { return key_ != nullptr; }

    private:
        HKEY key_ = nullptr;
    };

    [[nodiscard]] inline bool NtOk(NTSTATUS status) noexcept
    {
        return status >= 0;
    }

    // KEY_WOW64_64KEY when the current process runs under WOW64, otherwise 0. A
    // native x64 process is never WOW64, so this resolves to 0 at run time, which
    // matches the original's dword_140065DA8 on the x64 build.
    [[nodiscard]] inline REGSAM Wow64Flag()
    {
        BOOL wow64 = FALSE;
        ::IsWow64Process(::GetCurrentProcess(), &wow64);
        return wow64 ? KEY_WOW64_64KEY : 0;
    }

    [[nodiscard]] inline bool SetSz(HKEY key, const wchar_t* name, const wchar_t* value)
    {
        const size_t chars = ::wcslen(value) + 1;
        return ::RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value),
                   static_cast<DWORD>(chars * sizeof(wchar_t))) == ERROR_SUCCESS;
    }

    // Returns false when the value is absent, leaving `out` untouched so the caller's
    // default survives, which is how the dialog keeps the current account name.
    inline bool ReadSz(HKEY key, const wchar_t* name, std::wstring& out)
    {
        DWORD type = 0;
        DWORD bytes = 0;
        if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS)
        {
            return false;
        }
        std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
        DWORD cb = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        if (::RegQueryValueExW(key, name, nullptr, &type,
                               reinterpret_cast<LPBYTE>(buf.data()), &cb) != ERROR_SUCCESS)
        {
            return false;
        }
        buf.resize(::wcslen(buf.c_str()));
        out = buf;
        return true;
    }

    inline void InitLsaString(LSA_UNICODE_STRING& s, const wchar_t* value)
    {
        size_t bytes = ::wcslen(value) * sizeof(wchar_t);
        if (bytes > kMaxLsaBytes)
        {
            bytes = kMaxLsaBytes;
        }
        s.Buffer = const_cast<wchar_t*>(value);
        s.Length = static_cast<USHORT>(bytes);
        s.MaximumLength = static_cast<USHORT>(bytes + sizeof(wchar_t));
    }

    // Store the password as the LSA private secret "DefaultPassword". Returns true
    // when the secret was stored. SECURITY: writes a real secret into the LSA
    // policy only when called with a real password.
    [[nodiscard]] inline bool StoreLsaSecret(const wchar_t* password)
    {
        LSA_OBJECT_ATTRIBUTES attrs{};
        attrs.Length = sizeof(attrs);
        LSA_HANDLE policy = nullptr;
        if (!NtOk(::LsaOpenPolicy(nullptr, &attrs, POLICY_CREATE_SECRET, &policy)))
        {
            return false;
        }

        LSA_UNICODE_STRING name{};
        LSA_UNICODE_STRING data{};
        InitLsaString(name, kDefaultPassword);
        InitLsaString(data, password);
        const bool stored = NtOk(::LsaStorePrivateData(policy, &name, &data));
        ::LsaClose(policy);
        return stored;
    }

    // Remove the Exchange ActiveSync "EAS" policy key, which can block autologon.
    // The original opens Control with full access and deletes the EAS subkey.
    inline void RemoveEasPolicy()
    {
        ScopedHKey control;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kControlKey, 0, KEY_ALL_ACCESS, control.put()) == ERROR_SUCCESS)
        {
            ::RegDeleteKeyW(control.get(), kEasSubKey);
        }
    }

    // Read the account to pre-fill, matching the original: the current token user is
    // the default, then any existing Winlogon DefaultUserName / DefaultDomainName /
    // DefaultPassword override it. Read-only.
    inline void CurrentAccount(std::wstring& user, std::wstring& domain)
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return;
        }
        DWORD len = 0;
        ::GetTokenInformation(token, TokenUser, nullptr, 0, &len);
        if (len != 0)
        {
            std::vector<BYTE> buf(len);
            if (::GetTokenInformation(token, TokenUser, buf.data(), len, &len))
            {
                const auto* tu = reinterpret_cast<const TOKEN_USER*>(buf.data());
                wchar_t name[kFieldChars] = {};
                wchar_t dom[kFieldChars] = {};
                DWORD cchName = kFieldChars;
                DWORD cchDom = kFieldChars;
                SID_NAME_USE use{};
                if (::LookupAccountSidW(nullptr, tu->User.Sid, name, &cchName, dom, &cchDom, &use))
                {
                    user = name;
                    domain = dom;
                }
            }
        }
        ::CloseHandle(token);
    }

    [[nodiscard]] inline CurrentState ReadCurrentState()
    {
        CurrentState s;
        CurrentAccount(s.user, s.domain);
        ScopedHKey winlogon;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kWinlogonKey, 0, Wow64Flag() | KEY_READ, winlogon.put()) == ERROR_SUCCESS)
        {
            s.keyOpened = true;
            ReadSz(winlogon.get(), kDefaultUserName, s.user);
            ReadSz(winlogon.get(), kDefaultDomainName, s.domain);
            ReadSz(winlogon.get(), kDefaultPassword, s.password);
        }
        return s;
    }

    // Enable autologon. SECURITY: this writes the Winlogon values, stores the LSA
    // secret and sets AutoAdminLogon=1 on the local machine. Call only with real
    // credentials when autologon is actually wanted.
    [[nodiscard]] inline EnableResult Enable(const wchar_t* user, const wchar_t* domain, const wchar_t* password)
    {
        HANDLE token = nullptr;
        if (!::LogonUserW(user, domain, password, LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token))
        {
            return EnableResult::InvalidCredentials;
        }
        ::CloseHandle(token);

        ScopedHKey winlogon;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kWinlogonKey, 0, Wow64Flag() | KEY_WRITE, winlogon.put()) != ERROR_SUCCESS)
        {
            return EnableResult::OpenKeyFailed;
        }

        if (!SetSz(winlogon.get(), kDefaultUserName, user))
        {
            return EnableResult::WriteUserFailed;
        }
        if (!SetSz(winlogon.get(), kDefaultDomainName, domain))
        {
            return EnableResult::WriteDomainFailed;
        }

        const bool secretStored = StoreLsaSecret(password);
        if (secretStored)
        {
            // The secret lives in the LSA policy, so drop any stale plaintext value.
            ::RegDeleteValueW(winlogon.get(), kDefaultPassword);
        }
        else if (!SetSz(winlogon.get(), kDefaultPassword, password))
        {
            return EnableResult::WritePasswordFailed;
        }

        RemoveEasPolicy();

        if (!SetSz(winlogon.get(), kAutoAdminLogon, L"1"))
        {
            return EnableResult::SetAutoAdminFailed;
        }

        return secretStored ? EnableResult::SuccessEncrypted : EnableResult::SuccessPlaintext;
    }

    // Disable autologon. Deletes the three Winlogon values and sets
    // AutoAdminLogon=0. Like the original, it does not clear the LSA secret.
    [[nodiscard]] inline DisableResult Disable()
    {
        ScopedHKey winlogon;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kWinlogonKey, 0, Wow64Flag() | KEY_WRITE, winlogon.put()) != ERROR_SUCCESS)
        {
            return DisableResult::OpenKeyFailed;
        }
        ::RegDeleteValueW(winlogon.get(), kDefaultUserName);
        ::RegDeleteValueW(winlogon.get(), kDefaultPassword);
        ::RegDeleteValueW(winlogon.get(), kDefaultDomainName);
        if (!SetSz(winlogon.get(), kAutoAdminLogon, L"0"))
        {
            return DisableResult::SetAutoAdminFailed;
        }
        return DisableResult::Success;
    }
}

// EULA gate: the per-tool flag HKCU\Software\Sysinternals\Autologon\EulaAccepted,
// plus the -accepteula / /accepteula command-line switch. The original also honours
// the machine-wide and user-wide Software\Sysinternals keys, which this recreation
// leaves to the per-tool flag.
namespace eula
{
    inline constexpr wchar_t kSysinternalsKey[] = L"Software\\Sysinternals\\Autologon";
    inline constexpr wchar_t kEulaAccepted[]    = L"EulaAccepted";

    [[nodiscard]] inline bool IsAccepted()
    {
        autologon::ScopedHKey key;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, kSysinternalsKey, 0, KEY_READ | KEY_WOW64_64KEY, key.put()) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD value = 0;
        DWORD type = 0;
        DWORD cb = sizeof(value);
        if (::RegQueryValueExW(key.get(), kEulaAccepted, nullptr, &type,
                               reinterpret_cast<LPBYTE>(&value), &cb) != ERROR_SUCCESS)
        {
            return false;
        }
        return type == REG_DWORD && value != 0;
    }

    inline void Accept()
    {
        autologon::ScopedHKey key;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, kSysinternalsKey, 0, nullptr, 0,
                              KEY_WRITE | KEY_WOW64_64KEY, nullptr, key.put(), nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            ::RegSetValueExW(key.get(), kEulaAccepted, 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&one), sizeof(one));
        }
    }
}
