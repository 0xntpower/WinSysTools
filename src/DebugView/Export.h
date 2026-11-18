// Export.h -- save and clipboard report building.
// The main window formats each visible column into a Report, because the time
// column depends on live view options, and this layer only joins and writes the
// cells. Tab-delimited output mirrors the original Save, and CSV is offered too.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct Report
{
    std::vector<std::wstring> headers;
    std::vector<std::vector<std::wstring>> rows;
};

enum class ExportFormat
{
    Tab,
    Csv
};

inline std::wstring csvField(const std::wstring& s)
{
    bool quote = s.find_first_of(L",\"\r\n") != std::wstring::npos;
    if (!quote)
    {
        return s;
    }
    std::wstring out = L"\"";
    for (wchar_t c : s)
    {
        if (c == L'"')
        {
            out += L'"';
        }
        out += c;
    }
    out += L'"';
    return out;
}

inline std::wstring BuildReport(const Report& rep, ExportFormat fmt, bool withHeader)
{
    const wchar_t sep = (fmt == ExportFormat::Csv) ? L',' : L'\t';
    auto cell = [&](const std::wstring& s)
    {
        return fmt == ExportFormat::Csv ? csvField(s) : s;
    };

    std::wstring out;
    if (withHeader && !rep.headers.empty())
    {
        for (size_t c = 0; c < rep.headers.size(); ++c)
        {
            if (c)
            {
                out += sep;
            }
            out += cell(rep.headers[c]);
        }
        out += L"\r\n";
    }
    for (const auto& row : rep.rows)
    {
        for (size_t c = 0; c < row.size(); ++c)
        {
            if (c)
            {
                out += sep;
            }
            out += cell(row[c]);
        }
        out += L"\r\n";
    }
    return out;
}

inline bool WriteReport(const std::wstring& path, const Report& rep, ExportFormat fmt, bool withHeader)
{
    std::wstring text = BuildReport(rep, fmt, withHeader);
    int n =
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), n, nullptr, nullptr);

    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    static const BYTE bom[3] = {0xEF, 0xBB, 0xBF};
    DWORD wrote = 0;
    ::WriteFile(h, bom, 3, &wrote, nullptr);
    if (!utf8.empty())
    {
        ::WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &wrote, nullptr);
    }
    ::CloseHandle(h);
    return true;
}
