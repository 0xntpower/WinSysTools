// AdRestore.cpp -- source-accurate recreation of Sysinternals AdRestore v1.2.
//
// Enumerates tombstoned (deleted) Active Directory objects and optionally
// restores a chosen one. Reconstructed from adrestore64.exe by Mark Russinovich.
//
// Enumeration binds the Deleted Objects container through ADSI and walks it with
// IDirectorySearch under the TOMBSTONE search preference (ADS_SEARCHPREF_TOMBSTONE
// sets the show-deleted control for the query). Restore uses raw WLDAP32 to clear
// isDeleted and set a new distinguishedName under the object's lastKnownParent,
// carrying the LDAP_SERVER_SHOW_DELETED control.
//
// Source map (adrestore64.exe):
//   main                 0x140002B30  banner, EULA gate, arg parse, two-pass search
//   sub_140002660        connect: rootDSE defaultNamingContext then WKGUID container
//   sub_1400022C0        search, per-row attribute print, restore prompt
//   sub_1400027B0        restore: ldap_init/connect/bind + ldap_modify_ext_sW
//   sub_140001910/1AE0   EULA strip, registry check and write
//   sub_140002A80        usage

#ifndef NOMINMAX
#define NOMINMAX
#endif

// This tool binds the directory through ADSI, so it needs the full OLE surface
// (objbase, oaidl). WIN32_LEAN_AND_MEAN would strip what activeds.h depends on.
#include <windows.h>
#include <winldap.h>
#include <activeds.h>
#include <oleauto.h>
#include <wrl/client.h>

#include <cstdio>
#include <cwctype>
#include <string>

#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "activeds.lib")
#pragma comment(lib, "adsiid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr wchar_t kAppName[] = L"AdRestore";
    constexpr wchar_t kSysinternalsKey[] = L"Software\\Sysinternals";
    constexpr wchar_t kEulaValue[] = L"EulaAccepted";

    // Well-known GUID of the Deleted Objects container, bound through the
    // LDAP://<WKGUID=...> moniker relative to the default naming context.
    constexpr wchar_t kDeletedObjectsWkguid[] = L"18e2ea80684f11d2b9aa00c04f79f805";

    // LDAP_SERVER_SHOW_DELETED_OID -- returns tombstones the directory hides.
    constexpr wchar_t kShowDeletedOid[] = L"1.2.840.113556.1.4.417";

    // IDirectorySearch returns a value only when the column holds a string type.
    // Bit set for ADSTYPE 1,3,4,5,8 (DN, case-ignore, printable, numeric, octet).
    constexpr unsigned kPrintableTypeMask = 0x13Au;

    constexpr int kPageSize = 1000;
    constexpr int kAttributeCount = 3;

    // --- Console output ----------------------------------------------------

    void Out(const wchar_t* format, ...)
    {
        va_list args;
        va_start(args, format);
        std::vwprintf(format, args);
        va_end(args);
    }

    int Usage()
    {
        std::printf("Usage: AdRestore [-r] [searchfilter]\n\n");
        std::printf("   -r       Prompt to restore deleted objects found.\n\n");
        std::printf("This command enumerates all objects with the string \"comp\" in the name.\n\n");
        std::printf("     adrestore comp\n");
        return 1;
    }

    // --- EULA --------------------------------------------------------------

    [[nodiscard]] bool EulaFlagSet(HKEY root, const std::wstring& sub)
    {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(root, sub.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key)
            != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD data = 0;
        DWORD cb = sizeof(data);
        const LSTATUS st = ::RegQueryValueExW(key, kEulaValue, nullptr, nullptr,
                                              reinterpret_cast<LPBYTE>(&data), &cb);
        ::RegCloseKey(key);
        return st == ERROR_SUCCESS && data != 0;
    }

    [[nodiscard]] bool IsEulaAccepted()
    {
        if (EulaFlagSet(HKEY_LOCAL_MACHINE, kSysinternalsKey)
            || EulaFlagSet(HKEY_CURRENT_USER, kSysinternalsKey))
        {
            return true;
        }
        return EulaFlagSet(HKEY_CURRENT_USER, std::wstring(kSysinternalsKey) + L"\\" + kAppName);
    }

    void WriteEulaAccepted()
    {
        const std::wstring sub = std::wstring(kSysinternalsKey) + L"\\" + kAppName;
        HKEY key = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, nullptr, 0,
                              KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            const DWORD one = 1;
            ::RegSetValueExW(key, kEulaValue, 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&one), sizeof(one));
            ::RegCloseKey(key);
        }
    }

    // Remove an -accepteula / /accepteula entry from argv, shifting the rest down.
    [[nodiscard]] bool StripAccepteula(int& argc, wchar_t** argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (::_wcsicmp(argv[i], L"-accepteula") == 0 || ::_wcsicmp(argv[i], L"/accepteula") == 0)
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

    // Gate the run on the EULA. Records acceptance in the registry and returns
    // true to proceed, or false when the user declines.
    //
    // The original also shows a RichEdit license dialog on interactive desktops.
    // The console Y/N prompt is the faithful headless path, so the dialog is
    // skipped by design.
    [[nodiscard]] bool AcceptEula(int& argc, wchar_t** argv)
    {
        const bool acceptFlag = StripAccepteula(argc, argv);
        if (acceptFlag || IsEulaAccepted())
        {
            WriteEulaAccepted();
            return true;
        }
        for (;;)
        {
            std::printf("Accept Eula (Y/N)? ");
            std::fflush(stdout);
            const int ch = std::getwchar();
            if ((ch & ~0x20) == 'Y')
            {
                WriteEulaAccepted();
                return true;
            }
            if ((ch & ~0x20) == 'N')
            {
                return false;
            }
        }
    }

    // --- Restore -----------------------------------------------------------

    // Clear isDeleted and move the object back under lastKnownParent. The caller
    // identifies the object by its live (deleted) distinguishedName.
    //
    // Returns 0 on success. A negative value is a startup HRESULT. A positive
    // value is the raw LDAP result from ldap_modify_ext_sW. The original treats
    // any non-negative return as success, so a positive LDAP error still reports
    // "Restore succeeded" -- reproduced here for fidelity.
    [[nodiscard]] int RestoreObject(LPCWSTR dn, LPCWSTR lastKnownParent)
    {
        if (dn == nullptr)
        {
            return static_cast<int>(E_POINTER);
        }

        // A tombstone RDN carries a "\0ADEL:<guid>" suffix. Cut it at the "\0A"
        // escape, then re-parent under lastKnownParent to form the restored DN.
        std::wstring newDn = dn;
        size_t cut = std::wstring::npos;
        for (size_t i = 0; i + 2 < newDn.size(); ++i)
        {
            if (newDn[i] == L'\\' && newDn[i + 1] == L'0' && newDn[i + 2] == L'A')
            {
                cut = i;
                break;
            }
        }
        if (cut != std::wstring::npos)
        {
            newDn.resize(cut);
        }
        else if (lastKnownParent != nullptr)
        {
            return static_cast<int>(E_INVALIDARG);
        }
        newDn += L",";
        if (lastKnownParent != nullptr)
        {
            newDn += lastKnownParent;
        }

        LDAP* ld = ::ldap_initW(nullptr, LDAP_PORT);
        if (ld == nullptr)
        {
            return static_cast<int>(E_FAIL);
        }

        ULONG version = LDAP_VERSION3;
        ULONG rc = ::ldap_set_optionW(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
        if (rc == LDAP_SUCCESS)
        {
            rc = ::ldap_connect(ld, nullptr);
        }
        if (rc == LDAP_SUCCESS)
        {
            rc = ::ldap_bind_sW(ld, nullptr, nullptr, LDAP_AUTH_NEGOTIATE);
        }
        if (rc == LDAP_SUCCESS)
        {
            LDAPModW modDeleted = {};
            modDeleted.mod_op = LDAP_MOD_DELETE;
            modDeleted.mod_type = const_cast<PWCHAR>(L"isDeleted");
            modDeleted.mod_vals.modv_strvals = nullptr;

            PWCHAR dnValues[2] = { const_cast<PWCHAR>(newDn.c_str()), nullptr };
            LDAPModW modDn = {};
            modDn.mod_op = LDAP_MOD_REPLACE;
            modDn.mod_type = const_cast<PWCHAR>(L"distinguishedName");
            modDn.mod_vals.modv_strvals = dnValues;

            LDAPModW* mods[3] = { &modDeleted, (lastKnownParent != nullptr ? &modDn : nullptr), nullptr };

            LDAPControlW showDeleted = {};
            showDeleted.ldctl_oid = const_cast<PWCHAR>(kShowDeletedOid);
            showDeleted.ldctl_value.bv_len = 0;
            showDeleted.ldctl_value.bv_val = nullptr;
            showDeleted.ldctl_iscritical = TRUE;
            PLDAPControlW serverControls[2] = { &showDeleted, nullptr };

            rc = ::ldap_modify_ext_sW(ld, const_cast<PWCHAR>(dn), mods, serverControls, nullptr);
        }

        if (rc != LDAP_SUCCESS)
        {
            PWCHAR message = ::ldap_err2stringW(rc);
            if (message != nullptr)
            {
                ::OutputDebugStringW(message);
            }
        }
        ::ldap_unbind(ld);
        return static_cast<int>(rc);
    }

    // --- Enumeration -------------------------------------------------------

    // Bind the Deleted Objects container as IDirectorySearch: open rootDSE, read
    // its defaultNamingContext, then open LDAP://<WKGUID=...,NC> as a container.
    [[nodiscard]] HRESULT OpenDeletedObjects(ComPtr<IDirectorySearch>& search)
    {
        ComPtr<IADs> rootDse;
        HRESULT hr = ::ADsOpenObject(L"LDAP://rootDSE", nullptr, nullptr,
                                     ADS_SECURE_AUTHENTICATION, IID_PPV_ARGS(&rootDse));
        if (FAILED(hr))
        {
            return hr;
        }

        VARIANT namingContext;
        ::VariantInit(&namingContext);
        hr = rootDse->Get(const_cast<BSTR>(L"defaultNamingContext"), &namingContext);
        if (FAILED(hr))
        {
            return hr;
        }

        std::wstring path = L"LDAP://<WKGUID=";
        path += kDeletedObjectsWkguid;
        path += L",";
        path += namingContext.bstrVal;
        path += L">";
        ::VariantClear(&namingContext);

        ComPtr<IADsContainer> container;
        hr = ::ADsOpenObject(path.c_str(), nullptr, nullptr,
                             ADS_SECURE_AUTHENTICATION | ADS_FAST_BIND, IID_PPV_ARGS(&container));
        if (FAILED(hr))
        {
            return hr;
        }
        return container.As(&search);
    }

    // Run one filtered search over the Deleted Objects container, print each
    // object's string attributes, and optionally prompt to restore it. Adds the
    // number of matched objects to count.
    void EnumerateDeleted(LPCWSTR filter, bool restore, int& count)
    {
        ComPtr<IDirectorySearch> search;
        if (FAILED(OpenDeletedObjects(search)))
        {
            return;
        }

        ADS_SEARCHPREF_INFO prefs[3] = {};
        prefs[0].dwSearchPref = ADS_SEARCHPREF_SEARCH_SCOPE;
        prefs[0].vValue.dwType = ADSTYPE_INTEGER;
        prefs[0].vValue.Integer = ADS_SCOPE_ONELEVEL;
        prefs[1].dwSearchPref = ADS_SEARCHPREF_TOMBSTONE;
        prefs[1].vValue.dwType = ADSTYPE_BOOLEAN;
        prefs[1].vValue.Boolean = TRUE;
        prefs[2].dwSearchPref = ADS_SEARCHPREF_PAGESIZE;
        prefs[2].vValue.dwType = ADSTYPE_INTEGER;
        prefs[2].vValue.Integer = kPageSize;
        if (FAILED(search->SetSearchPreference(prefs, 3)))
        {
            return;
        }

        LPWSTR attributes[kAttributeCount] = {
            const_cast<LPWSTR>(L"cn"),
            const_cast<LPWSTR>(L"distinguishedName"),
            const_cast<LPWSTR>(L"lastKnownParent")
        };

        ADS_SEARCH_HANDLE handle = nullptr;
        if (FAILED(search->ExecuteSearch(const_cast<LPWSTR>(filter), attributes, kAttributeCount, &handle)))
        {
            return;
        }

        while (search->GetNextRow(handle) == S_OK)
        {
            for (int c = 0; c < kAttributeCount; ++c)
            {
                ADS_SEARCH_COLUMN column = {};
                if (FAILED(search->GetColumn(handle, attributes[c], &column)))
                {
                    continue;
                }
                if (column.dwADsType <= ADSTYPE_OCTET_STRING
                    && ((kPrintableTypeMask >> column.dwADsType) & 1u) != 0)
                {
                    Out(L"%s: ", attributes[c]);
                    for (DWORD v = 0; v < column.dwNumValues; ++v)
                    {
                        Out(L"%s", column.pADsValues[v].CaseIgnoreString);
                        if (v + 1 < column.dwNumValues)
                        {
                            Out(L",");
                        }
                    }
                    Out(L"\n");
                }
                search->FreeColumn(&column);
            }
            ++count;
            Out(L"\n");

            if (restore)
            {
                Out(L"Do you want to restore this object (y/n)? ");
                std::fflush(stdout);
                wchar_t line[MAX_PATH] = {};
                if (std::fgetws(line, static_cast<int>(std::size(line)), stdin) != nullptr
                    && std::towupper(line[0]) == L'Y')
                {
                    ADS_SEARCH_COLUMN dnColumn = {};
                    if (SUCCEEDED(search->GetColumn(handle, const_cast<LPWSTR>(L"distinguishedName"), &dnColumn)))
                    {
                        ADS_SEARCH_COLUMN parentColumn = {};
                        const HRESULT parentHr = search->GetColumn(
                            handle, const_cast<LPWSTR>(L"lastKnownParent"), &parentColumn);
                        LPCWSTR dn = dnColumn.pADsValues[0].CaseIgnoreString;
                        int rc = 0;
                        if (parentHr != S_OK)
                        {
                            rc = RestoreObject(dn, nullptr);
                        }
                        else
                        {
                            rc = RestoreObject(dn, parentColumn.pADsValues[0].CaseIgnoreString);
                            search->FreeColumn(&parentColumn);
                        }
                        if (rc >= 0)
                        {
                            Out(L"\nRestore succeeded.\n");
                        }
                        else
                        {
                            Out(L"\nRestore failed: %d\n", rc);
                        }
                        search->FreeColumn(&dnColumn);
                    }
                }
                Out(L"\n");
            }
        }

        search->CloseSearchHandle(handle);
    }
}

int wmain(int argc, wchar_t** argv)
{
    ::CoInitialize(nullptr);

    Out(L"\nAdRestore v1.2\n");
    Out(L"by Mark Russinovich\n");
    Out(L"Sysinternals - www.sysinternals.com\n\n");

    if (!AcceptEula(argc, argv))
    {
        return 1;
    }

    int filterIndex = -1;
    bool restore = false;
    for (int i = 1; i < argc; ++i)
    {
        const wchar_t* arg = argv[i];
        if (arg[0] == L'-' || arg[0] == L'/')
        {
            if (std::towupper(arg[1]) != L'R')
            {
                return Usage();
            }
            restore = true;
        }
        else
        {
            if (filterIndex != -1)
            {
                return Usage();
            }
            filterIndex = i;
        }
    }

    Out(L"\nEnumerating domain deleted objects:\n\n");

    unsigned total = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        wchar_t filter[MAX_PATH + 8] = {};
        if (filterIndex == -1)
        {
            std::swprintf(filter, std::size(filter), pass == 0 ? L"(cn=*)" : L"(ou=*)");
        }
        else
        {
            std::swprintf(filter, std::size(filter), pass == 0 ? L"(cn=*%s*)" : L"(ou=*%s*)",
                          argv[filterIndex]);
        }
        int count = 0;
        EnumerateDeleted(filter, restore, count);
        total += static_cast<unsigned>(count);
    }

    Out(L"Found %d item%s matching search criteria.\n\n", total, total == 1 ? L"" : L"s");
    return 0;
}
