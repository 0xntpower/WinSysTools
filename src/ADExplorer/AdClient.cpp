// AdClient.cpp -- the Active Directory browse and read engine method bodies.
//
// Split out of AdClient.h so the header carries the class shape plus the small
// inline helpers and the template Bind. The bodies are unchanged: the rootDSE
// naming-context read on Connect, the ONELEVEL child enumeration, the BASE
// attribute read, the user-filter search, the WLDAP32 write path kept for
// fidelity and never wired to the GUI, and the ADSI column helpers.
#include "AdClient.h"

namespace ad
{
    bool Client::Connect(const std::wstring& server, const std::wstring& user, const std::wstring& password,
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

    std::vector<Child> Client::EnumerateChildren(const std::wstring& dn)
    {
        std::vector<Child> out;
        ComPtr<IDirectorySearch> search;
        if (FAILED(Bind(PathFor(dn), kBindContainer, IID_IDirectorySearch, search)))
        {
            return out;
        }

        SetScope(search.Get(), ADS_SCOPE_ONELEVEL);

        LPWSTR attrs[] = {const_cast<LPWSTR>(L"distinguishedName"), const_cast<LPWSTR>(L"name"),
                          const_cast<LPWSTR>(L"objectClass")};
        ADS_SEARCH_HANDLE handle = nullptr;
        if (FAILED(search->ExecuteSearch(const_cast<LPWSTR>(L"(objectClass=*)"), attrs,
                                         static_cast<DWORD>(std::size(attrs)), &handle)))
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

    std::vector<Attribute> Client::ReadAttributes(const std::wstring& dn)
    {
        std::vector<Attribute> out;
        ComPtr<IDirectorySearch> search;
        if (FAILED(Bind(PathFor(dn), kBindContainer, IID_IDirectorySearch, search)))
        {
            return out;
        }

        SetScope(search.Get(), ADS_SCOPE_BASE);

        ADS_SEARCH_HANDLE handle = nullptr;
        if (FAILED(search->ExecuteSearch(const_cast<LPWSTR>(L"(objectClass=*)"), nullptr, static_cast<DWORD>(-1),
                                         &handle)))
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

    std::vector<Child> Client::Search(const std::wstring& baseDn, const std::wstring& filter, ADS_SCOPEENUM scope)
    {
        std::vector<Child> out;
        ComPtr<IDirectorySearch> search;
        if (FAILED(Bind(PathFor(baseDn), kBindContainer, IID_IDirectorySearch, search)))
        {
            return out;
        }

        SetScope(search.Get(), scope);

        LPWSTR attrs[] = {const_cast<LPWSTR>(L"distinguishedName"), const_cast<LPWSTR>(L"objectClass")};
        ADS_SEARCH_HANDLE handle = nullptr;
        const std::wstring use = filter.empty() ? L"(objectClass=*)" : filter;
        if (FAILED(search->ExecuteSearch(const_cast<LPWSTR>(use.c_str()), attrs, static_cast<DWORD>(std::size(attrs)),
                                         &handle)))
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

    ULONG Client::ModifyAttribute(const std::wstring& dn, const std::wstring& attribute, const std::wstring& value)
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
            PWCHAR values[2] = {const_cast<PWCHAR>(value.c_str()), nullptr};
            LDAPModW mod = {};
            mod.mod_op = LDAP_MOD_REPLACE;
            mod.mod_type = const_cast<PWCHAR>(attribute.c_str());
            mod.mod_vals.modv_strvals = values;
            LDAPModW* mods[2] = {&mod, nullptr};
            rc = ::ldap_modify_ext_sW(ld, const_cast<PWCHAR>(dn.c_str()), mods, nullptr, nullptr);
        }
        ::ldap_unbind(ld);
        return rc;
    }

    void Client::SetScope(IDirectorySearch* search, ADS_SCOPEENUM scope)
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

    void Client::ReadColumn(IDirectorySearch* search, ADS_SEARCH_HANDLE handle, LPWSTR columnName, Attribute& attr)
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

    std::wstring Client::ColumnFirstString(IDirectorySearch* search, ADS_SEARCH_HANDLE handle, LPCWSTR name)
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

    std::wstring Client::ColumnLastString(IDirectorySearch* search, ADS_SEARCH_HANDLE handle, LPCWSTR name)
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

    void Client::GetSingle(IADs* object, LPCWSTR name, std::wstring& out)
    {
        VARIANT value;
        ::VariantInit(&value);
        if (SUCCEEDED(object->Get(const_cast<BSTR>(name), &value)) && value.vt == VT_BSTR && value.bstrVal)
        {
            out = value.bstrVal;
        }
        ::VariantClear(&value);
    }

    void Client::AddContext(IADs* rootDse, LPCWSTR name)
    {
        std::wstring dn;
        GetSingle(rootDse, name, dn);
        if (!dn.empty())
        {
            PushUnique(dn);
        }
    }

    void Client::AddContexts(IADs* rootDse, LPCWSTR name)
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
                    if (SUCCEEDED(::SafeArrayGetElement(array, &i, &element)) && element.vt == VT_BSTR &&
                        element.bstrVal)
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

    void Client::PushUnique(const std::wstring& dn)
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

    std::wstring Client::ErrorText(HRESULT hr)
    {
        WCHAR name[256] = {};
        WCHAR buffer[256] = {};
        DWORD providerError = 0;
        if (SUCCEEDED(::ADsGetLastError(&providerError, buffer, static_cast<DWORD>(std::size(buffer)), name,
                                        static_cast<DWORD>(std::size(name)))) &&
            buffer[0] != 0)
        {
            return buffer;
        }
        wchar_t fallback[64] = {};
        _snwprintf_s(fallback, _TRUNCATE, L"Connection failed (0x%08lX)", static_cast<unsigned long>(hr));
        return fallback;
    }
} // namespace ad
