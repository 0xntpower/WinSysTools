// AdClient.h -- the Active Directory browse and read engine.
//
// Source-accurate to ADExplorer64.exe, which browses and reads through ADSI
// (ActiveDS ADsOpenObject plus IDirectorySearch), not raw ldap_search. The
// binary imports ADsOpenObject at many sites (sub_1400067A0 binds a container,
// sub_1400104D0 binds the schema) and reserves WLDAP32 (ldap_modify_ext_sW,
// ldap_rename_ext_sW) for the write path only. This mirrors that split.
//
// Connect reads the rootDSE naming contexts. A tree node expands through an
// ADS_SCOPE_ONELEVEL search for (objectClass=*). Selecting an object reads all
// its attributes through an ADS_SCOPE_BASE search. The common LDAP syntaxes are
// formatted for display, objectSid as S-1-, objectGUID as a GUID, octet strings
// as hex, generalized and UTC times as local time, booleans as TRUE or FALSE.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winldap.h>
#include <activeds.h>
#include <oleauto.h>
#include <sddl.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "activeds.lib")
#pragma comment(lib, "adsiid.lib")
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace ad
{
    using Microsoft::WRL::ComPtr;

    // ADS_SECURE_AUTHENTICATION (0x1), with ADS_FAST_BIND (0x20) for containers,
    // matching sub_1400067A0 (0x21) and sub_1400104D0 (0x1) in the original.
    inline constexpr DWORD kBindSecure = 0x1u;
    inline constexpr DWORD kBindContainer = 0x21u;
    inline constexpr int kPageSize = 1000;

    struct Child
    {
        std::wstring rdn;          // display label, the leading DN component
        std::wstring dn;           // full distinguished name
        std::wstring objectClass;  // most specific class
        bool container = true;     // everything in AD may hold children
    };

    struct Attribute
    {
        std::wstring name;
        std::wstring syntax;
        int count = 0;
        std::wstring value;        // values joined for the list column
    };

    // Leading RDN of a distinguished name, for example "CN=Users" from
    // "CN=Users,DC=contoso,DC=com". Commas escaped with a backslash do not split.
    [[nodiscard]] inline std::wstring LeadingRdn(const std::wstring& dn)
    {
        for (size_t i = 0; i < dn.size(); ++i)
        {
            if (dn[i] == L'\\')
            {
                ++i;
                continue;
            }
            if (dn[i] == L',')
            {
                return dn.substr(0, i);
            }
        }
        return dn;
    }

    namespace detail
    {
        [[nodiscard]] inline std::wstring SyntaxName(ADSTYPEENUM type)
        {
            switch (type)
            {
            case ADSTYPE_DN_STRING:            return L"DN String";
            case ADSTYPE_CASE_EXACT_STRING:    return L"Case Sensitive String";
            case ADSTYPE_CASE_IGNORE_STRING:   return L"Unicode String";
            case ADSTYPE_PRINTABLE_STRING:     return L"Printable String";
            case ADSTYPE_NUMERIC_STRING:       return L"Numeric String";
            case ADSTYPE_BOOLEAN:              return L"Boolean";
            case ADSTYPE_INTEGER:              return L"Integer";
            case ADSTYPE_OCTET_STRING:         return L"Octet String";
            case ADSTYPE_UTC_TIME:             return L"UTC Time";
            case ADSTYPE_LARGE_INTEGER:        return L"Large Integer";
            case ADSTYPE_NT_SECURITY_DESCRIPTOR: return L"NT Security Descriptor";
            case ADSTYPE_PROV_SPECIFIC:        return L"Octet String";
            default:                           return L"Unknown";
            }
        }

        [[nodiscard]] inline std::wstring FormatSid(const BYTE* data, DWORD len)
        {
            if (data == nullptr || len == 0 || !::IsValidSid(const_cast<PSID>(static_cast<const void*>(data))))
            {
                return std::wstring();
            }
            LPWSTR sidText = nullptr;
            std::wstring result;
            if (::ConvertSidToStringSidW(const_cast<PSID>(static_cast<const void*>(data)), &sidText) && sidText != nullptr)
            {
                result = sidText;
                ::LocalFree(sidText);
            }
            return result;
        }

        [[nodiscard]] inline std::wstring FormatGuid(const BYTE* data, DWORD len)
        {
            if (data == nullptr || len != sizeof(GUID))
            {
                return std::wstring();
            }
            GUID guid{};
            std::memcpy(&guid, data, sizeof(GUID));
            wchar_t buf[64] = {};
            if (::StringFromGUID2(guid, buf, static_cast<int>(std::size(buf))) > 0)
            {
                return buf;
            }
            return std::wstring();
        }

        [[nodiscard]] inline std::wstring FormatHex(const BYTE* data, DWORD len)
        {
            static const wchar_t kDigits[] = L"0123456789abcdef";
            std::wstring out;
            out.reserve(static_cast<size_t>(len) * 3);
            for (DWORD i = 0; i < len; ++i)
            {
                if (i != 0)
                {
                    out.push_back(L' ');
                }
                out.push_back(kDigits[data[i] >> 4]);
                out.push_back(kDigits[data[i] & 0x0F]);
            }
            return out;
        }

        [[nodiscard]] inline std::wstring FormatTime(const SYSTEMTIME& st)
        {
            wchar_t date[64] = {};
            wchar_t time[64] = {};
            ::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, static_cast<int>(std::size(date)));
            ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, static_cast<int>(std::size(time)));
            return std::wstring(date) + L" " + time;
        }

        // Format one ADSVALUE for display. The attribute name picks the SID and
        // GUID special cases, matching how the original renders them.
        [[nodiscard]] inline std::wstring FormatValue(const std::wstring& attr, const ADSVALUE& v)
        {
            switch (v.dwType)
            {
            case ADSTYPE_DN_STRING:          return v.DNString ? v.DNString : L"";
            case ADSTYPE_CASE_EXACT_STRING:  return v.CaseExactString ? v.CaseExactString : L"";
            case ADSTYPE_CASE_IGNORE_STRING: return v.CaseIgnoreString ? v.CaseIgnoreString : L"";
            case ADSTYPE_PRINTABLE_STRING:   return v.PrintableString ? v.PrintableString : L"";
            case ADSTYPE_NUMERIC_STRING:     return v.NumericString ? v.NumericString : L"";
            case ADSTYPE_BOOLEAN:            return v.Boolean ? L"TRUE" : L"FALSE";
            case ADSTYPE_INTEGER:
            {
                wchar_t buf[32] = {};
                _snwprintf_s(buf, _TRUNCATE, L"%u", v.Integer);
                return buf;
            }
            case ADSTYPE_LARGE_INTEGER:
            {
                wchar_t buf[32] = {};
                const long long value = (static_cast<long long>(v.LargeInteger.HighPart) << 32)
                                        | static_cast<unsigned>(v.LargeInteger.LowPart);
                _snwprintf_s(buf, _TRUNCATE, L"%lld", value);
                return buf;
            }
            case ADSTYPE_UTC_TIME:           return FormatTime(v.UTCTime);
            case ADSTYPE_NT_SECURITY_DESCRIPTOR:
                return L"<NT security descriptor>";
            case ADSTYPE_OCTET_STRING:
            case ADSTYPE_PROV_SPECIFIC:
            {
                const BYTE* data = v.OctetString.lpValue;
                const DWORD len = v.OctetString.dwLength;
                if (_wcsicmp(attr.c_str(), L"objectSid") == 0 || _wcsicmp(attr.c_str(), L"sIDHistory") == 0)
                {
                    const std::wstring sid = FormatSid(data, len);
                    if (!sid.empty())
                    {
                        return sid;
                    }
                }
                if (_wcsicmp(attr.c_str(), L"objectGUID") == 0)
                {
                    const std::wstring guid = FormatGuid(data, len);
                    if (!guid.empty())
                    {
                        return guid;
                    }
                }
                return FormatHex(data, len);
            }
            default:
                return std::wstring();
            }
        }
    }

    // Not thread-safe. One instance holds one bound connection. COM must be
    // initialized on the calling thread before Connect.
    class Client
    {
    public:
        Client() = default;
        ~Client() = default;

        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;
        Client(Client&&) = default;
        Client& operator=(Client&&) = default;

        // Bind to the rootDSE and read the naming contexts. An empty server
        // binds serverless to the current domain. Returns false on failure and
        // fills errorOut with the ADSI error text.
        [[nodiscard]] bool Connect(const std::wstring& server,
                                   const std::wstring& user,
                                   const std::wstring& password,
                                   std::wstring& errorOut)
        {
            server_ = server;
            user_ = user;
            password_ = password;
            prefix_ = server.empty() ? L"LDAP://" : (L"LDAP://" + server + L"/");
            namingContexts_.clear();

            ComPtr<IADs> rootDse;
            const std::wstring rootPath = prefix_ + L"RootDSE";
            HRESULT hr = Bind(rootPath, kBindSecure, IID_IADs, rootDse);
            if (FAILED(hr))
            {
                errorOut = ErrorText(hr);
                return false;
            }

            AddContext(rootDse.Get(), L"defaultNamingContext");
            AddContext(rootDse.Get(), L"configurationNamingContext");
            AddContext(rootDse.Get(), L"schemaNamingContext");
            AddContexts(rootDse.Get(), L"namingContexts");
            GetSingle(rootDse.Get(), L"dnsHostName", serverName_);
            if (serverName_.empty())
            {
                GetSingle(rootDse.Get(), L"serverName", serverName_);
            }
            return !namingContexts_.empty();
        }

        [[nodiscard]] const std::vector<std::wstring>& NamingContexts() const
        {
            return namingContexts_;
        }

        [[nodiscard]] const std::wstring& ServerName() const
        {
            return serverName_;
        }

        [[nodiscard]] std::wstring PathFor(const std::wstring& dn) const
        {
            return prefix_ + dn;
        }

        // List the immediate children of a container through an ADS_SCOPE_ONELEVEL
        // search for (objectClass=*).
        [[nodiscard]] std::vector<Child> EnumerateChildren(const std::wstring& dn)
        {
            std::vector<Child> out;
            ComPtr<IDirectorySearch> search;
            if (FAILED(Bind(PathFor(dn), kBindContainer, IID_IDirectorySearch, search)))
            {
                return out;
            }

            SetScope(search.Get(), ADS_SCOPE_ONELEVEL);

            LPWSTR attrs[] = {
                const_cast<LPWSTR>(L"distinguishedName"),
                const_cast<LPWSTR>(L"name"),
                const_cast<LPWSTR>(L"objectClass")
            };
            ADS_SEARCH_HANDLE handle = nullptr;
            if (FAILED(search->ExecuteSearch(const_cast<LPWSTR>(L"(objectClass=*)"),
                                             attrs, static_cast<DWORD>(std::size(attrs)), &handle)))
            {
                return out;
            }

            while (search->GetNextRow(handle) == S_OK)
            {
                Child child;
                child.dn = ColumnFirstString(search.Get(), handle, L"distinguishedName");
                if (child.dn.empty())
                {
                    continue;
                }
                child.rdn = LeadingRdn(child.dn);
                child.objectClass = ColumnLastString(search.Get(), handle, L"objectClass");
                out.push_back(std::move(child));
            }
            search->CloseSearchHandle(handle);
            return out;
        }

        // Read every attribute of one object through an ADS_SCOPE_BASE search.
        [[nodiscard]] std::vector<Attribute> ReadAttributes(const std::wstring& dn)
        {
            std::vector<Attribute> out;
            ComPtr<IDirectorySearch> search;
            if (FAILED(Bind(PathFor(dn), kBindContainer, IID_IDirectorySearch, search)))
            {
                return out;
            }

            SetScope(search.Get(), ADS_SCOPE_BASE);

            ADS_SEARCH_HANDLE handle = nullptr;
            if (FAILED(search->ExecuteSearch(const_cast<LPWSTR>(L"(objectClass=*)"), nullptr,
                                             static_cast<DWORD>(-1), &handle)))
            {
                return out;
            }

            if (search->GetNextRow(handle) == S_OK)
            {
                LPWSTR columnName = nullptr;
                while (search->GetNextColumnName(handle, &columnName) == S_OK && columnName != nullptr)
                {
                    Attribute attr;
                    attr.name = columnName;
                    ReadColumn(search.Get(), handle, columnName, attr);
                    out.push_back(std::move(attr));
                    ::FreeADsMem(columnName);
                    columnName = nullptr;
                }
            }
            search->CloseSearchHandle(handle);
            return out;
        }

        // Run a user LDAP filter under the given scope from a base DN.
        [[nodiscard]] std::vector<Child> Search(const std::wstring& baseDn,
                                                const std::wstring& filter,
                                                ADS_SCOPEENUM scope)
        {
            std::vector<Child> out;
            ComPtr<IDirectorySearch> search;
            if (FAILED(Bind(PathFor(baseDn), kBindContainer, IID_IDirectorySearch, search)))
            {
                return out;
            }

            SetScope(search.Get(), scope);

            LPWSTR attrs[] = {
                const_cast<LPWSTR>(L"distinguishedName"),
                const_cast<LPWSTR>(L"objectClass")
            };
            ADS_SEARCH_HANDLE handle = nullptr;
            const std::wstring use = filter.empty() ? L"(objectClass=*)" : filter;
            if (FAILED(search->ExecuteSearch(const_cast<LPWSTR>(use.c_str()),
                                             attrs, static_cast<DWORD>(std::size(attrs)), &handle)))
            {
                return out;
            }

            while (search->GetNextRow(handle) == S_OK)
            {
                Child child;
                child.dn = ColumnFirstString(search.Get(), handle, L"distinguishedName");
                if (child.dn.empty())
                {
                    continue;
                }
                child.rdn = LeadingRdn(child.dn);
                child.objectClass = ColumnLastString(search.Get(), handle, L"objectClass");
                out.push_back(std::move(child));
            }
            search->CloseSearchHandle(handle);
            return out;
        }

        // Write one attribute value to the live directory through WLDAP32, the
        // original's modify path (sub_1400367E0 binds with ldap_bind_sW and calls
        // ldap_modify_ext_sW). Reproduced for fidelity. It is out of scope and the
        // GUI never calls it, so directory contents are never changed by this
        // recreation. See analysis/ADExplorer.md.
        [[nodiscard]] ULONG ModifyAttribute(const std::wstring& dn,
                                             const std::wstring& attribute,
                                             const std::wstring& value)
        {
            LDAP* ld = ::ldap_initW(server_.empty() ? nullptr : const_cast<PWCHAR>(server_.c_str()), LDAP_PORT);
            if (ld == nullptr)
            {
                return LDAP_LOCAL_ERROR;
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
                PWCHAR values[2] = { const_cast<PWCHAR>(value.c_str()), nullptr };
                LDAPModW mod = {};
                mod.mod_op = LDAP_MOD_REPLACE;
                mod.mod_type = const_cast<PWCHAR>(attribute.c_str());
                mod.mod_vals.modv_strvals = values;
                LDAPModW* mods[2] = { &mod, nullptr };
                rc = ::ldap_modify_ext_sW(ld, const_cast<PWCHAR>(dn.c_str()), mods, nullptr, nullptr);
            }
            ::ldap_unbind(ld);
            return rc;
        }

    private:
        template <typename T>
        [[nodiscard]] HRESULT Bind(const std::wstring& path, DWORD flags, REFIID riid, ComPtr<T>& out)
        {
            void* object = nullptr;
            LPWSTR user = user_.empty() ? nullptr : const_cast<LPWSTR>(user_.c_str());
            LPWSTR pass = password_.empty() ? nullptr : const_cast<LPWSTR>(password_.c_str());
            const HRESULT hr = ::ADsOpenObject(const_cast<LPWSTR>(path.c_str()), user, pass, flags, riid, &object);
            if (SUCCEEDED(hr))
            {
                out.Attach(static_cast<T*>(object));
            }
            return hr;
        }

        static void SetScope(IDirectorySearch* search, ADS_SCOPEENUM scope)
        {
            ADS_SEARCHPREF_INFO prefs[2] = {};
            prefs[0].dwSearchPref = ADS_SEARCHPREF_SEARCH_SCOPE;
            prefs[0].vValue.dwType = ADSTYPE_INTEGER;
            prefs[0].vValue.Integer = scope;
            prefs[1].dwSearchPref = ADS_SEARCHPREF_PAGESIZE;
            prefs[1].vValue.dwType = ADSTYPE_INTEGER;
            prefs[1].vValue.Integer = kPageSize;
            search->SetSearchPreference(prefs, static_cast<DWORD>(std::size(prefs)));
        }

        static void ReadColumn(IDirectorySearch* search, ADS_SEARCH_HANDLE handle,
                               LPWSTR columnName, Attribute& attr)
        {
            ADS_SEARCH_COLUMN column = {};
            if (FAILED(search->GetColumn(handle, columnName, &column)))
            {
                attr.syntax = L"(not set)";
                return;
            }
            attr.syntax = detail::SyntaxName(column.dwADsType);
            attr.count = static_cast<int>(column.dwNumValues);
            for (DWORD i = 0; i < column.dwNumValues; ++i)
            {
                if (!attr.value.empty())
                {
                    attr.value += L"; ";
                }
                attr.value += detail::FormatValue(attr.name, column.pADsValues[i]);
            }
            search->FreeColumn(&column);
        }

        [[nodiscard]] static std::wstring ColumnFirstString(IDirectorySearch* search,
                                                            ADS_SEARCH_HANDLE handle, LPCWSTR name)
        {
            std::wstring result;
            ADS_SEARCH_COLUMN column = {};
            if (SUCCEEDED(search->GetColumn(handle, const_cast<LPWSTR>(name), &column)))
            {
                if (column.dwNumValues > 0)
                {
                    result = detail::FormatValue(name, column.pADsValues[0]);
                }
                search->FreeColumn(&column);
            }
            return result;
        }

        [[nodiscard]] static std::wstring ColumnLastString(IDirectorySearch* search,
                                                           ADS_SEARCH_HANDLE handle, LPCWSTR name)
        {
            std::wstring result;
            ADS_SEARCH_COLUMN column = {};
            if (SUCCEEDED(search->GetColumn(handle, const_cast<LPWSTR>(name), &column)))
            {
                if (column.dwNumValues > 0)
                {
                    result = detail::FormatValue(name, column.pADsValues[column.dwNumValues - 1]);
                }
                search->FreeColumn(&column);
            }
            return result;
        }

        static void GetSingle(IADs* object, LPCWSTR name, std::wstring& out)
        {
            VARIANT value;
            ::VariantInit(&value);
            if (SUCCEEDED(object->Get(const_cast<BSTR>(name), &value)) && value.vt == VT_BSTR && value.bstrVal)
            {
                out = value.bstrVal;
            }
            ::VariantClear(&value);
        }

        void AddContext(IADs* rootDse, LPCWSTR name)
        {
            std::wstring dn;
            GetSingle(rootDse, name, dn);
            if (!dn.empty())
            {
                PushUnique(dn);
            }
        }

        void AddContexts(IADs* rootDse, LPCWSTR name)
        {
            VARIANT value;
            ::VariantInit(&value);
            if (SUCCEEDED(rootDse->Get(const_cast<BSTR>(name), &value)))
            {
                if ((value.vt & VT_ARRAY) != 0 && value.parray != nullptr)
                {
                    SAFEARRAY* array = value.parray;
                    LONG lower = 0;
                    LONG upper = 0;
                    ::SafeArrayGetLBound(array, 1, &lower);
                    ::SafeArrayGetUBound(array, 1, &upper);
                    for (LONG i = lower; i <= upper; ++i)
                    {
                        VARIANT element;
                        ::VariantInit(&element);
                        if (SUCCEEDED(::SafeArrayGetElement(array, &i, &element))
                            && element.vt == VT_BSTR && element.bstrVal)
                        {
                            PushUnique(element.bstrVal);
                        }
                        ::VariantClear(&element);
                    }
                }
                else if (value.vt == VT_BSTR && value.bstrVal)
                {
                    PushUnique(value.bstrVal);
                }
            }
            ::VariantClear(&value);
        }

        void PushUnique(const std::wstring& dn)
        {
            for (const std::wstring& existing : namingContexts_)
            {
                if (_wcsicmp(existing.c_str(), dn.c_str()) == 0)
                {
                    return;
                }
            }
            namingContexts_.push_back(dn);
        }

        [[nodiscard]] static std::wstring ErrorText(HRESULT hr)
        {
            WCHAR name[256] = {};
            WCHAR buffer[256] = {};
            DWORD providerError = 0;
            if (SUCCEEDED(::ADsGetLastError(&providerError, buffer,
                                            static_cast<DWORD>(std::size(buffer)),
                                            name, static_cast<DWORD>(std::size(name))))
                && buffer[0] != 0)
            {
                return buffer;
            }
            wchar_t fallback[64] = {};
            _snwprintf_s(fallback, _TRUNCATE, L"Connection failed (0x%08lX)", static_cast<unsigned long>(hr));
            return fallback;
        }

        std::wstring server_;
        std::wstring user_;
        std::wstring password_;
        std::wstring prefix_ = L"LDAP://";
        std::wstring serverName_;
        std::vector<std::wstring> namingContexts_;
    };
}
