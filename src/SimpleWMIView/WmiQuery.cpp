// WmiQuery.cpp -- the WMI (WQL) engine.
#include "WmiQuery.h"
#include <atlbase.h>
#include <wbemidl.h>
#include <cwchar>

#pragma comment(lib, "wbemuuid.lib")

namespace
{
    std::wstring bstrToStr(BSTR b)
    {
        return (b && *b) ? std::wstring(b, ::SysStringLen(b)) : std::wstring();
    }

    // Parse a WMI CIM_DATETIME string ("yyyymmddHHMMSS.ffffff+ooo") into a UTC
    // FILETIME. The field is local wall-clock time with a trailing UTC offset
    // in minutes, so UTC = wallclock - offset. Matches SWbemDateTime.GetFileTime
    // with bIsLocal = FALSE, which the original uses.
    bool parseCimDateTime(const std::wstring& s, FILETIME& out)
    {
        if (s.size() < 21)
        {
            return false;
        }
        auto num = [&](size_t off, int len) -> int
        {
            int v = 0;
            for (int i = 0; i < len; ++i)
            {
                wchar_t c = s[off + i];
                if (c < L'0' || c > L'9')
                {
                    return -1;
                }
                v = v * 10 + (c - L'0');
            }
            return v;
        };
        SYSTEMTIME st{};
        int year = num(0, 4), mon = num(4, 2), day = num(6, 2);
        int hour = num(8, 2), minu = num(10, 2), sec = num(12, 2);
        int micro = num(15, 6);
        if (year < 0 || mon < 0 || day < 0 || hour < 0 || minu < 0 || sec < 0)
        {
            return false;
        }
        st.wYear = (WORD)year;
        st.wMonth = (WORD)mon;
        st.wDay = (WORD)day;
        st.wHour = (WORD)hour;
        st.wMinute = (WORD)minu;
        st.wSecond = (WORD)sec;
        st.wMilliseconds = (WORD)(micro < 0 ? 0 : micro / 1000);

        FILETIME ft{};
        if (!::SystemTimeToFileTime(&st, &ft))
        {
            return false;
        }

        // Apply the UTC offset (+/- minutes) to get UTC.
        wchar_t sign = s[21];
        int offMin = num(22, 3);
        if (offMin > 0 && (sign == L'+' || sign == L'-'))
        {
            ULARGE_INTEGER u{};
            u.LowPart = ft.dwLowDateTime;
            u.HighPart = ft.dwHighDateTime;
            long long delta = (long long)offMin * 60 * 10000000LL;
            if (sign == L'+')
            {
                u.QuadPart -= delta;
            }
            else
            {
                u.QuadPart += delta;
            }
            ft.dwLowDateTime = u.LowPart;
            ft.dwHighDateTime = u.HighPart;
        }
        out = ft;
        return true;
    }

    // Append one scalar SAFEARRAY element (as text) to a joined string.
    void appendArrayElement(std::wstring& out, VARTYPE elemType, void* p, long i)
    {
        wchar_t b[32];
        switch (elemType)
        {
        case VT_BSTR:
        {
            BSTR v = ((BSTR*)p)[i];
            out += bstrToStr(v);
            break;
        }
        case VT_I4:
        case VT_UI4:
        case VT_INT:
        case VT_UINT:
            _itow(((LONG*)p)[i], b, 10);
            out += b;
            break;
        case VT_I2:
        case VT_UI2:
            _itow(((SHORT*)p)[i], b, 10);
            out += b;
            break;
        case VT_I1:
        case VT_UI1:
            _itow(((BYTE*)p)[i], b, 10);
            out += b;
            break;
        case VT_BOOL:
            out += ((VARIANT_BOOL*)p)[i] ? L"True" : L"False";
            break;
        case VT_I8:
            _i64tow(((LONGLONG*)p)[i], b, 10);
            out += b;
            break;
        case VT_UI8:
            _ui64tow(((ULONGLONG*)p)[i], b, 10);
            out += b;
            break;
        case VT_R4:
            _snwprintf(b, 31, L"%f", ((float*)p)[i]);
            out += b;
            break;
        case VT_R8:
            _snwprintf(b, 31, L"%f", ((double*)p)[i]);
            out += b;
            break;
        default:
            break;
        }
    }

    std::wstring joinArray(VARIANT& v)
    {
        std::wstring out;
        SAFEARRAY* sa = (v.vt & VT_BYREF) ? *v.pparray : v.parray;
        if (!sa)
        {
            return out;
        }
        VARTYPE et = v.vt & VT_TYPEMASK;
        long lb = 0, ub = -1;
        ::SafeArrayGetLBound(sa, 1, &lb);
        ::SafeArrayGetUBound(sa, 1, &ub);
        void* data = nullptr;
        if (FAILED(::SafeArrayAccessData(sa, &data)))
        {
            return out;
        }
        for (long i = lb; i <= ub; ++i)
        {
            if (i != lb)
            {
                out += L", ";
            }
            appendArrayElement(out, et, data, i - lb);
        }
        ::SafeArrayUnaccessData(sa);
        return out;
    }

    // Build a Cell from a property VARIANT and its CIM type, following the
    // original's value store (sub_140002B04): datetime -> FILETIME, 64-bit
    // integers parsed from BSTR, arrays joined, other numerics kept.
    Cell makeCell(VARIANT& v, int cimType)
    {
        Cell c;
        int base = cimType & ~CIM_FLAG_ARRAY;

        if (v.vt == VT_NULL || v.vt == VT_EMPTY)
        {
            c.kind = Cell::Empty;
            return c;
        }
        if (v.vt & VT_ARRAY)
        {
            c.kind = Cell::Str;
            c.str = joinArray(v);
            return c;
        }
        if (base == CIM_DATETIME && v.vt == VT_BSTR)
        {
            FILETIME ft{};
            if (parseCimDateTime(bstrToStr(v.bstrVal), ft))
            {
                c.kind = Cell::Date;
                c.ft = ft;
            }
            else
            {
                c.kind = Cell::Str;
                c.str = bstrToStr(v.bstrVal);
            }
            return c;
        }
        if ((base == CIM_SINT64 || base == CIM_UINT64) && v.vt == VT_BSTR)
        {
            // WMI hands 64-bit integers back as strings.
            c.str = bstrToStr(v.bstrVal);
            if (base == CIM_UINT64)
            {
                c.kind = Cell::Uint;
                c.i64 = (long long)_wcstoui64(c.str.c_str(), nullptr, 10);
            }
            else
            {
                c.kind = Cell::Int;
                swscanf(c.str.c_str(), L"%I64d", &c.i64);
            }
            return c;
        }
        switch (v.vt)
        {
        case VT_BSTR:
            c.kind = Cell::Str;
            c.str = bstrToStr(v.bstrVal);
            break;
        case VT_BOOL:
            c.kind = Cell::Bool;
            c.i64 = v.boolVal ? 1 : 0;
            break;
        case VT_I1:
            c.kind = Cell::Int;
            c.i64 = v.cVal;
            break;
        case VT_UI1:
            c.kind = (base == CIM_SINT8) ? Cell::Int : Cell::Uint;
            c.i64 = v.bVal;
            break;
        case VT_I2:
            c.kind = Cell::Int;
            c.i64 = v.iVal;
            break;
        case VT_UI2:
            c.kind = Cell::Uint;
            c.i64 = v.uiVal;
            break;
        case VT_I4:
        case VT_INT:
            c.kind = Cell::Int;
            c.i64 = v.lVal;
            break;
        case VT_UI4:
        case VT_UINT:
            c.kind = Cell::Uint;
            c.i64 = (unsigned)v.ulVal;
            break;
        case VT_I8:
            c.kind = Cell::Int;
            c.i64 = v.llVal;
            break;
        case VT_UI8:
            c.kind = Cell::Uint;
            c.i64 = (long long)v.ullVal;
            break;
        case VT_R4:
            c.kind = Cell::Real;
            c.real = v.fltVal;
            break;
        case VT_R8:
            c.kind = Cell::Real;
            c.real = v.dblVal;
            break;
        default:
        {
            // Fall back to a string conversion for anything else.
            CComVariant copy;
            if (SUCCEEDED(copy.ChangeType(VT_BSTR, &v)) && copy.vt == VT_BSTR)
            {
                c.kind = Cell::Str;
                c.str = bstrToStr(copy.bstrVal);
            }
            else
            {
                c.kind = Cell::Empty;
            }
            break;
        }
        }
        return c;
    }

    // Read the property names and CIM types of a class object into the columns.
    void columnsFromObject(IWbemClassObject* obj, std::vector<WmiColumn>& cols)
    {
        if (!obj)
        {
            return;
        }
        obj->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY);
        CComBSTR name;
        VARIANT v;
        VariantInit(&v);
        CIMTYPE t = 0;
        while (obj->Next(0, &name, &v, &t, nullptr) == WBEM_S_NO_ERROR)
        {
            WmiColumn col;
            col.name = bstrToStr(name);
            col.cimType = (t & ~CIM_FLAG_ARRAY);
            col.isArray = (t & CIM_FLAG_ARRAY) != 0;
            cols.push_back(col);
            name.Empty();
            VariantClear(&v);
            t = 0;
        }
        obj->EndEnumeration();
    }
} // namespace

WmiResult RunWmiQuery(const WmiQueryParams& p)
{
    WmiResult result;
    DWORD start = ::GetTickCount();

    // Build the namespace path. A remote connection uses \\computer\namespace.
    std::wstring ns = p.nameSpace;
    if (p.connectComputer == 2 && !p.computerName.empty())
    {
        ns = L"\\\\" + p.computerName + L"\\" + p.nameSpace;
    }

    CComPtr<IWbemLocator> locator;
    result.hr = locator.CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER);
    if (FAILED(result.hr) || !locator)
    {
        return result;
    }

    CComPtr<IWbemServices> services;
    CComBSTR nsPath(ns.c_str());
    CComBSTR user, pass;
    if (p.connectComputer == 2 && !p.userName.empty())
    {
        user = p.userName.c_str();
        pass = p.password.c_str();
    }
    result.hr = locator->ConnectServer(nsPath, user, pass, nullptr, 0, nullptr, nullptr, &services);
    if (FAILED(result.hr) || !services)
    {
        return result;
    }

    // Set the proxy security on the connection (needed for remote, harmless
    // locally), matching the original's CoSetProxyBlanket use.
    ::CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                        RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    // A class name (no spaces) becomes "SELECT * FROM <class>". Anything with a
    // space is treated as a full WQL query, as the original does.
    bool freeQuery = p.classOrQuery.find(L' ') != std::wstring::npos;
    std::wstring query = freeQuery ? p.classOrQuery : (L"SELECT * FROM " + p.classOrQuery);

    // For a plain class, take the column set from the class definition first so
    // empty results still show the right columns.
    if (!freeQuery)
    {
        CComPtr<IWbemClassObject> classObj;
        CComBSTR cls(p.classOrQuery.c_str());
        if (SUCCEEDED(services->GetObject(cls, 0, nullptr, &classObj, nullptr)) && classObj)
        {
            columnsFromObject(classObj, result.columns);
        }
    }

    CComPtr<IEnumWbemClassObject> e;
    CComBSTR wql(L"WQL");
    CComBSTR wqlQuery(query.c_str());
    result.hr = services->ExecQuery(wql, wqlQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &e);
    if (FAILED(result.hr) || !e)
    {
        result.queryTimeMs = ::GetTickCount() - start;
        return result;
    }

    for (;;)
    {
        CComPtr<IWbemClassObject> obj;
        ULONG got = 0;
        HRESULT hrn = e->Next(WBEM_INFINITE, 1, &obj, &got);
        if (hrn != WBEM_S_NO_ERROR || got == 0 || !obj)
        {
            break;
        }

        // For a free query the columns come from the first returned object.
        if (result.columns.empty())
        {
            columnsFromObject(obj, result.columns);
        }

        WmiRow row;
        row.cells.resize(result.columns.size());

        obj->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY);
        CComBSTR name;
        VARIANT v;
        VariantInit(&v);
        CIMTYPE t = 0;
        while (obj->Next(0, &name, &v, &t, nullptr) == WBEM_S_NO_ERROR)
        {
            int col = result.columnIndex(bstrToStr(name));
            if (col < 0)
            {
                // A property not already in the column set (free-query rows can
                // differ): add a column and widen every existing row.
                WmiColumn nc;
                nc.name = bstrToStr(name);
                nc.cimType = (t & ~CIM_FLAG_ARRAY);
                nc.isArray = (t & CIM_FLAG_ARRAY) != 0;
                result.columns.push_back(nc);
                for (auto& r : result.rows)
                {
                    r.cells.resize(result.columns.size());
                }
                row.cells.resize(result.columns.size());
                col = (int)result.columns.size() - 1;
            }
            row.cells[col] = makeCell(v, t);
            name.Empty();
            VariantClear(&v);
            t = 0;
        }
        obj->EndEnumeration();

        result.rows.push_back(std::move(row));
    }

    result.queryTimeMs = ::GetTickCount() - start;
    return result;
}
