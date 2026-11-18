// Format.h -- cell formatting shared by the ListView, the properties view and the
// exports. Times, sizes and attributes are rendered the way the original renders
// them (sub_140006D24 for dates, sub_140006BE0 for sizes, sub_140002114 for
// attributes), so a rebuilt row matches the original byte for byte.
#pragma once
#include <windows.h>
#include <string>

struct DisplayOptions
{
    bool showTimeInGmt = false;   // Options -> Show Time In GMT (cfg ShowTimeInGMT)
};

inline bool FileTimeIsEmpty(const FILETIME& ft)
{
    return ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0;
}

// Date plus time in the user locale (short date, long time). Empty when the time
// is zero or the year is not past 1000, matching the original.
inline std::wstring FormatFileTime(const FILETIME& ft, const DisplayOptions& d)
{
    if (FileTimeIsEmpty(ft)) return L"";
    SYSTEMTIME st{};
    FILETIME shown = ft;
    if (!d.showTimeInGmt)
    {
        SYSTEMTIME utc{};
        if (ft.dwHighDateTime != 0 && ::FileTimeToSystemTime(&ft, &utc))
        {
            SYSTEMTIME local{};
            if (::SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
                ::SystemTimeToFileTime(&local, &shown);
            else
                ::FileTimeToLocalFileTime(&ft, &shown);
        }
    }
    if (!::FileTimeToSystemTime(&shown, &st) || st.wYear <= 1000) return L"";
    wchar_t date[128]{}, time[128]{};
    ::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 128);
    ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 128);
    std::wstring out = date;
    out += L" ";
    out += time;
    return out;
}

// Whole number with the locale thousands grouping, no decimals.
inline std::wstring FormatSize(unsigned long long value)
{
    wchar_t raw[32];
    _ui64tow_s(value, raw, 32, 10);
    NUMBERFMTW fmt{};
    wchar_t dec[8] = L".", grp[8] = L",";
    ::GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, dec, 8);
    ::GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, grp, 8);
    wchar_t grouping[16] = L"3";
    ::GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SGROUPING, grouping, 16);
    fmt.NumDigits = 0;
    fmt.LeadingZero = 0;
    fmt.Grouping = (grouping[0] == L'3' && grouping[2] == L'2') ? 32 : _wtoi(grouping);
    fmt.lpDecimalSep = dec;
    fmt.lpThousandSep = grp;
    fmt.NegativeOrder = 1;
    wchar_t out[48]{};
    if (::GetNumberFormatW(LOCALE_USER_DEFAULT, 0, raw, &fmt, out, 48) > 0)
        return out;
    return raw;
}

// Attribute letters, in the original's order: A C R H S D T I E X V.
inline std::wstring FormatAttrs(DWORD a)
{
    std::wstring s;
    if (a & FILE_ATTRIBUTE_ARCHIVE)             s += L"A";
    if (a & FILE_ATTRIBUTE_COMPRESSED)          s += L"C";
    if (a & FILE_ATTRIBUTE_READONLY)            s += L"R";
    if (a & FILE_ATTRIBUTE_HIDDEN)              s += L"H";
    if (a & FILE_ATTRIBUTE_SYSTEM)              s += L"S";
    if (a & FILE_ATTRIBUTE_DIRECTORY)           s += L"D";
    if (a & FILE_ATTRIBUTE_TEMPORARY)           s += L"T";
    if (a & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) s += L"I";
    if (a & FILE_ATTRIBUTE_ENCRYPTED)           s += L"E";
    if (a & 0x20000)                            s += L"X";  // FILE_ATTRIBUTE_NO_SCRUB_DATA
    if (a & 0x8000)                             s += L"V";  // FILE_ATTRIBUTE_INTEGRITY_STREAM
    return s;
}
