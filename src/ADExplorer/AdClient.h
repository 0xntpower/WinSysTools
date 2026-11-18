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
//
// The Client method bodies live in AdClient.cpp. This header keeps the class
// shape plus the small inline helpers, the structs and the template Bind.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Binding the directory through ADSI needs the full OLE surface (objbase, oaidl).
// WIN32_LEAN_AND_MEAN would strip what activeds.h depends on, so it is not set,
// matching the AdRestore recreation.
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
        std::wstring rdn;         // display label, the leading DN component
        std::wstring dn;          // full distinguished name
        std::wstring objectClass; // most specific class
        bool container = true;    // everything in AD may hold children
    };

    struct Attribute
    {
        std::wstring name;
        std::wstring syntax;
        int count = 0;
        std::wstring value; // values joined for the list column
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
            case ADSTYPE_DN_STRING:
                return L"DN String";
            case ADSTYPE_CASE_EXACT_STRING:
                return L"Case Sensitive String";
            case ADSTYPE_CASE_IGNORE_STRING:
                return L"Unicode String";
            case ADSTYPE_PRINTABLE_STRING:
                return L"Printable String";
            case ADSTYPE_NUMERIC_STRING:
                return L"Numeric String";
            case ADSTYPE_BOOLEAN:
                return L"Boolean";
            case ADSTYPE_INTEGER:
                return L"Integer";
            case ADSTYPE_OCTET_STRING:
                return L"Octet String";
            case ADSTYPE_UTC_TIME:
                return L"UTC Time";
            case ADSTYPE_LARGE_INTEGER:
                return L"Large Integer";
            case ADSTYPE_NT_SECURITY_DESCRIPTOR:
                return L"NT Security Descriptor";
            case ADSTYPE_PROV_SPECIFIC:
                return L"Octet String";
            default:
                return L"Unknown";
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
            if (::ConvertSidToStringSidW(const_cast<PSID>(static_cast<const void*>(data)), &sidText) &&
                sidText != nullptr)
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
            ::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date,
                             static_cast<int>(std::size(date)));
            ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, static_cast<int>(std::size(time)));
            return std::wstring(date) + L" " + time;
        }

        // Format one ADSVALUE for display. The attribute name picks the SID and
        // GUID special cases, matching how the original renders them.
        [[nodiscard]] inline std::wstring FormatValue(const std::wstring& attr, const ADSVALUE& v)
        {
            switch (v.dwType)
            {
            case ADSTYPE_DN_STRING:
                return v.DNString ? v.DNString : L"";
            case ADSTYPE_CASE_EXACT_STRING:
                return v.CaseExactString ? v.CaseExactString : L"";
            case ADSTYPE_CASE_IGNORE_STRING:
                return v.CaseIgnoreString ? v.CaseIgnoreString : L"";
            case ADSTYPE_PRINTABLE_STRING:
                return v.PrintableString ? v.PrintableString : L"";
            case ADSTYPE_NUMERIC_STRING:
                return v.NumericString ? v.NumericString : L"";
            case ADSTYPE_BOOLEAN:
                return v.Boolean ? L"TRUE" : L"FALSE";
            case ADSTYPE_INTEGER:
            {
                wchar_t buf[32] = {};
                _snwprintf_s(buf, _TRUNCATE, L"%u", v.Integer);
                return buf;
            }
            case ADSTYPE_LARGE_INTEGER:
            {
                wchar_t buf[32] = {};
                const long long value = (static_cast<long long>(v.LargeInteger.HighPart) << 32) |
                                        static_cast<unsigned>(v.LargeInteger.LowPart);
                _snwprintf_s(buf, _TRUNCATE, L"%lld", value);
                return buf;
            }
            case ADSTYPE_UTC_TIME:
                return FormatTime(v.UTCTime);
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
    } // namespace detail

    // Not thread-safe. One instance holds one bound connection. COM must be
    // initialized on the calling thread before Connect.
    //
    // The non-template method bodies live in AdClient.cpp. The trivial accessors
    // and the template Bind stay inline here.
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
        [[nodiscard]] bool Connect(const std::wstring& server, const std::wstring& user, const std::wstring& password,
                                   std::wstring& errorOut);

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
        [[nodiscard]] std::vector<Child> EnumerateChildren(const std::wstring& dn);

        // Read every attribute of one object through an ADS_SCOPE_BASE search.
        [[nodiscard]] std::vector<Attribute> ReadAttributes(const std::wstring& dn);

        // Run a user LDAP filter under the given scope from a base DN.
        [[nodiscard]] std::vector<Child> Search(const std::wstring& baseDn, const std::wstring& filter,
                                                ADS_SCOPEENUM scope);

        // Write one attribute value to the live directory through WLDAP32, the
        // original's modify path (sub_1400367E0 binds with ldap_bind_sW and calls
        // ldap_modify_ext_sW). Reproduced for fidelity. It is out of scope and the
        // GUI never calls it, so directory contents are never changed by this
        // recreation. See analysis/ADExplorer.md.
        [[nodiscard]] ULONG ModifyAttribute(const std::wstring& dn, const std::wstring& attribute,
                                            const std::wstring& value);

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

        static void SetScope(IDirectorySearch* search, ADS_SCOPEENUM scope);
        static void ReadColumn(IDirectorySearch* search, ADS_SEARCH_HANDLE handle, LPWSTR columnName, Attribute& attr);
        [[nodiscard]] static std::wstring ColumnFirstString(IDirectorySearch* search, ADS_SEARCH_HANDLE handle,
                                                            LPCWSTR name);
        [[nodiscard]] static std::wstring ColumnLastString(IDirectorySearch* search, ADS_SEARCH_HANDLE handle,
                                                           LPCWSTR name);
        static void GetSingle(IADs* object, LPCWSTR name, std::wstring& out);
        void AddContext(IADs* rootDse, LPCWSTR name);
        void AddContexts(IADs* rootDse, LPCWSTR name);
        void PushUnique(const std::wstring& dn);
        [[nodiscard]] static std::wstring ErrorText(HRESULT hr);

        std::wstring server_;
        std::wstring user_;
        std::wstring password_;
        std::wstring prefix_ = L"LDAP://";
        std::wstring serverName_;
        std::vector<std::wstring> namingContexts_;
    };
} // namespace ad
