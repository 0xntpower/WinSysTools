// WmiResult.h -- the dynamic-column result model for one WMI query, plus the
// per-cell value formatting.
//
// SimpleWMIView has no fixed column set. The columns are the property names of
// the result objects, and each cell is formatted from its VARIANT by the CIM
// type, exactly as the original does in its value store (sub_140002B04) and its
// date formatter (sub_1400082CC): strings verbatim, CIM_DATETIME as a FILETIME
// rendered short-date + long-time, 64-bit integers parsed from their BSTR form,
// arrays joined with ", ", booleans as True / False, and other numerics as
// decimals.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>

// CIM types are the standard WbemCimtypeEnum values (CIM_STRING = 8,
// CIM_DATETIME = 101, and so on, defined in wbemcli.h). 2000 + type is the
// resource id of the type name string. The array flag is 0x2000.
constexpr int kCimArrayFlag = 0x2000;

// The numeric CIM base types (used for right-aligning numeric columns and for
// numeric sorting). Kept here with literal values so files that do not pull in
// wbemcli.h can still classify a column.
inline bool cimIsNumeric(int cimType)
{
    switch (cimType)
    {
    case 16: case 17:   // SINT8  / UINT8
    case 2:  case 18:   // SINT16 / UINT16
    case 3:  case 19:   // SINT32 / UINT32
    case 20: case 21:   // SINT64 / UINT64
    case 4:  case 5:    // REAL32 / REAL64
        return true;
    default:
        return false;
    }
}

struct DisplayOptions
{
    bool showTimeInGmt   = false;   // Options -> Show Time In GMT
    bool showMilliseconds = false;  // Options -> Show Milliseconds In Time
};

// One cell. The display text is produced on demand so the date columns react to
// the Show Time In GMT / milliseconds options without reloading.
struct Cell
{
    enum Kind { Empty, Str, Int, Uint, Real, Bool, Date } kind = Empty;
    std::wstring  str;        // Str (also arrays joined, references)
    long long     i64 = 0;    // Int / Uint / Bool
    double        real = 0;   // Real
    FILETIME      ft{};       // Date (UTC)

    std::wstring text(const DisplayOptions& o) const
    {
        switch (kind)
        {
        case Str:  return str;
        case Int:  { wchar_t b[32]; _i64tow(i64, b, 10); return b; }
        case Uint: { wchar_t b[32]; _ui64tow((unsigned long long)i64, b, 10); return b; }
        case Bool: return i64 ? L"True" : L"False";
        case Real: { wchar_t b[64]; _snwprintf(b, 63, L"%f", real); return b; }
        case Date: return formatDate(o);
        default:   return {};
        }
    }

    // Sort key for numeric / date columns (the original sorts those by value).
    bool numericKey(long long& out) const
    {
        switch (kind)
        {
        case Int:  out = i64; return true;
        case Uint: out = i64; return true;
        case Bool: out = i64; return true;
        case Date: out = ((long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime; return true;
        case Real: out = (long long)real; return true;
        default:   return false;
        }
    }

private:
    std::wstring formatDate(const DisplayOptions& o) const
    {
        if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0)
            return {};
        FILETIME use = ft;
        if (!o.showTimeInGmt)
            ::FileTimeToLocalFileTime(&ft, &use);   // ft is stored UTC
        SYSTEMTIME st{};
        if (!::FileTimeToSystemTime(&use, &st) || st.wYear <= 1000)
            return {};
        wchar_t date[128]{}, time[128]{};
        ::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 128);
        ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 128);
        std::wstring out = date;
        out += L' ';
        out += time;
        if (o.showMilliseconds)
        {
            wchar_t ms[16];
            _snwprintf(ms, 15, L".%3.3d", st.wMilliseconds);
            out += ms;
        }
        return out;
    }
};

struct WmiColumn
{
    std::wstring name;      // WMI property name
    int          cimType = 8;  // base type, array flag stripped. 8 is CIM_STRING
    bool         isArray = false;
    int          width = 150;
};

struct WmiRow
{
    std::vector<Cell> cells;   // one per column, in column order
};

struct WmiResult
{
    std::vector<WmiColumn> columns;
    std::vector<WmiRow>    rows;
    HRESULT   hr = S_OK;        // last WMI HRESULT (for the status bar)
    DWORD     queryTimeMs = 0;  // total query + enumeration time

    int columnIndex(const std::wstring& name) const
    {
        for (size_t i = 0; i < columns.size(); ++i)
            if (_wcsicmp(columns[i].name.c_str(), name.c_str()) == 0)
                return (int)i;
        return -1;
    }
};
