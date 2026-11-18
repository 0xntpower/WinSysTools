// Export.cpp -- report builders for the five MIMEView save formats.
#include "Export.h"
#include "Columns.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kReportTitle = L"Internet Explorer MIME Types";

    std::wstring htmlEscape(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
        {
            switch (c)
            {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            default:   out += c; break;
            }
        }
        return out;
    }

    std::string toUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }
}

std::wstring BuildReport(const std::vector<MimeType>& items,
                         const std::vector<int>& columns,
                         ExportFormat format)
{
    std::wstring out;

    switch (format)
    {
    case ExportFormat::Text:
    {
        for (const auto& m : items)
        {
            for (int c : columns)
                out += std::wstring(kColumns[c].title) + L" : " + m.column(c) + L"\r\n";
            out += L"==================================================\r\n\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        // Matches the original /stab -- no header row, every field (incl. the last)
        // is followed by a tab, then CRLF.
        for (const auto& m : items)
        {
            for (int c : columns)
                out += m.column(c) + L"\t";
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tabular:
    {
        std::vector<size_t> w(columns.size());
        for (size_t i = 0; i < columns.size(); ++i)
            w[i] = wcslen(kColumns[columns[i]].title);
        for (const auto& m : items)
            for (size_t i = 0; i < columns.size(); ++i)
                w[i] = std::max(w[i], m.column(columns[i]).size());
        auto pad = [](std::wstring s, size_t width) { s.resize(std::max(s.size(), width), L' '); return s; };
        for (size_t i = 0; i < columns.size(); ++i)
            out += pad(kColumns[columns[i]].title, w[i]) + L" ";
        out += L"\r\n";
        for (const auto& m : items)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += pad(m.column(columns[i]), w[i]) + L" ";
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::HtmlHorizontal:
    {
        out += L"<html><head><meta charset=\"utf-8\"><title>";
        out += kReportTitle;
        out += L"</title></head>\r\n<body>\r\n<h3>";
        out += kReportTitle;
        out += L"</h3>\r\n<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">\r\n<tr bgcolor=\"#cccccc\">";
        for (int c : columns)
            out += L"<th align=\"left\">" + htmlEscape(kColumns[c].title) + L"</th>";
        out += L"</tr>\r\n";
        for (const auto& m : items)
        {
            out += L"<tr>";
            for (int c : columns)
                out += L"<td>" + htmlEscape(m.column(c)) + L"&nbsp;</td>";
            out += L"</tr>\r\n";
        }
        out += L"</table>\r\n</body></html>\r\n";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        out += L"<html><head><meta charset=\"utf-8\"><title>";
        out += kReportTitle;
        out += L"</title></head>\r\n<body>\r\n<h3>";
        out += kReportTitle;
        out += L"</h3>\r\n";
        for (const auto& m : items)
        {
            out += L"<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">\r\n";
            for (int c : columns)
                out += L"<tr><td bgcolor=\"#cccccc\">" + htmlEscape(kColumns[c].title) +
                       L"</td><td>" + htmlEscape(m.column(c)) + L"&nbsp;</td></tr>\r\n";
            out += L"</table><br>\r\n";
        }
        out += L"</body></html>\r\n";
        break;
    }
    }

    return out;
}

bool WriteReport(const std::wstring& path,
                 const std::vector<MimeType>& items,
                 const std::vector<int>& columns,
                 ExportFormat format)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << toUtf8(BuildReport(items, columns, format));
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (sw == L"/stext")    { out = ExportFormat::Text;           return true; }
    if (sw == L"/stab")     { out = ExportFormat::Tab;            return true; }
    if (sw == L"/stabular") { out = ExportFormat::Tabular;        return true; }
    if (sw == L"/shtml")    { out = ExportFormat::HtmlHorizontal; return true; }
    if (sw == L"/sverhtml") { out = ExportFormat::HtmlVertical;   return true; }
    return false;
}
