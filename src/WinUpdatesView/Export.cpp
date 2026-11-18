// Export.cpp -- report builders for the WinUpdatesView save formats, matching
// the original's exact output (field order, structure and encoding). The text,
// tab, HTML, XML and JSON reports are UTF-16LE with a BOM. Only the CSV report
// is UTF-8 with a BOM.
#include "Export.h"
#include "Columns.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kReportTitle = L"Windows Updates History";

    std::wstring htmlEscape(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
            switch (c)
            {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            default:   out += c; break;
            }
        return out;
    }

    std::wstring xmlEscape(const std::wstring& s)
    {
        std::wstring out;
        for (wchar_t c : s)
            switch (c)
            {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            default:   out += c; break;
            }
        return out;
    }

    // The original's JSON writer only escapes the double quote. Newlines and
    // backslashes are left verbatim (the KB890830 description keeps its raw
    // embedded line breaks).
    std::wstring jsonEscape(const std::wstring& s)
    {
        std::wstring out;
        for (wchar_t c : s)
        {
            if (c == L'"') out += L"\\\"";
            else out += c;
        }
        return out;
    }

    // In the markup reports (HTML and XML) embedded line breaks become <br>.
    std::wstring nlToBr(std::wstring s)
    {
        std::wstring out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == L'\r' && i + 1 < s.size() && s[i + 1] == L'\n') { out += L"<br>"; ++i; }
            else if (s[i] == L'\r' || s[i] == L'\n') out += L"<br>";
            else out += s[i];
        }
        return out;
    }

    // One HTML table cell's inner markup. URL values are turned into links and
    // empty values render as a non-breaking space, as the original does.
    std::wstring htmlCell(const std::wstring& raw)
    {
        std::wstring esc = nlToBr(htmlEscape(raw));
        if (raw.rfind(L"http://", 0) == 0 || raw.rfind(L"https://", 0) == 0)
            return L"<a href=\"" + esc + L"\">" + esc + L"</a>";
        return esc.empty() ? std::wstring(L"&nbsp;") : esc;
    }

    // CSV field: quote when it contains a comma, quote or line break.
    std::wstring csvField(const std::wstring& s)
    {
        if (s.find_first_of(L",\"\r\n") == std::wstring::npos) return s;
        std::wstring out = L"\"";
        for (wchar_t c : s) { if (c == L'"') out += L'"'; out += c; }
        out += L'"';
        return out;
    }

    // XML element name from a column title: lowercase, spaces -> underscore.
    std::wstring slug(const std::wstring& title)
    {
        std::wstring out;
        for (wchar_t c : title)
        {
            if (c == L' ') out += L'_';
            else if (c >= L'A' && c <= L'Z') out += (wchar_t)(c - L'A' + L'a');
            else out += c;
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

std::wstring BuildReport(const std::vector<UpdateRecord>& items,
                         const std::vector<int>& columns,
                         ExportFormat format,
                         const ExportOptions& opt)
{
    const DisplayOptions& d = opt.display;
    std::wstring out;

    switch (format)
    {
    case ExportFormat::Text:
    {
        // Each record is bracketed by a 50-'=' rule and followed by a blank
        // line. Field names are left-justified to a minimum width of 18, then
        // ": " (names longer than 18 are not truncated).
        const std::wstring sep(50, L'=');
        for (const auto& it : items)
        {
            out += sep + L"\r\n";
            for (int c : columns)
            {
                std::wstring name = kColumns[c].title;
                name.resize(std::max<size_t>(name.size(), 18), L' ');
                out += name + L": " + it.column(c, d) + L"\r\n";
            }
            out += sep + L"\r\n\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        // Tab-delimited, every field (including the last) followed by a tab.
        if (opt.addHeaderLine)
        {
            for (int c : columns) out += std::wstring(kColumns[c].title) + L"\t";
            out += L"\r\n";
        }
        for (const auto& it : items)
        {
            for (int c : columns) out += it.column(c, d) + L"\t";
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Csv:
    {
        if (opt.addHeaderLine)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += csvField(kColumns[columns[i]].title) + (i + 1 < columns.size() ? L"," : L"");
            out += L"\r\n";
        }
        for (const auto& it : items)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += csvField(it.column(columns[i], d)) + (i + 1 < columns.size() ? L"," : L"");
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::HtmlHorizontal:
    {
        int total = 0;
        for (int c : columns) total += kColumns[c].htmlWidth;
        out += L"<!doctype html>\r\n";
        out += L"<html><head><title>"; out += kReportTitle; out += L"</title><style>\r\n";
        out += L".mytable  { border: 1px groove}\r\n";
        out += L".mytable  td {padding: 5px ; border: 1px groove}\r\n";
        out += L".mytable th {padding: 10px; border: 1px groove;}\r\n";
        out += L"</style></head>\r\n<body>\r\n <h3>"; out += kReportTitle; out += L"</h3>\r\n";
        out += L"<br/><h4>Created by using Windows Updates History Viewer</h4><p/><table style=\"width: ";
        out += std::to_wstring(total); out += L"px\" class=\"mytable\"><tr style=\"background-color:#E0E0E0;\">\r\n";
        for (int c : columns)
            out += L"<th style=\"width: " + std::to_wstring(kColumns[c].htmlWidth) + L"px\">" +
                   kColumns[c].title + L"</th>\r\n";
        // The original closes only the header row here. Data <tr> elements are
        // left open (one </tr> prefixes the first data row), matching its output.
        out += L"</tr>";
        for (const auto& it : items)
        {
            out += L"<tr>";
            for (int c : columns)
                out += L"<td style=\"background-color:#FFFFFF;\">" + htmlCell(it.column(c, d)) + L"</td>";
            out += L"\r\n";
        }
        out += L"</table>\r\n\r\n</body></html>";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        out += L"<!doctype html>\r\n";
        out += L"<html><head><title>"; out += kReportTitle; out += L"</title><style>\r\n";
        out += L".mytable  { border: 1px groove}\r\n";
        out += L".mytable  td {padding: 5px ; border: 1px groove}\r\n";
        out += L".mytable th {padding: 10px; border: 1px groove;}\r\n";
        out += L"</style></head>\r\n<body>\r\n <h3>"; out += kReportTitle; out += L"</h3>\r\n";
        out += L"<br/><h4>Created by using Windows Updates History Viewer</h4><p/>\r\n";
        for (const auto& it : items)
        {
            out += L"<table class=\"mytable\">\r\n";
            for (int c : columns)
                out += L"<tr><td style=\"background-color:#E0E0E0;\">" + std::wstring(kColumns[c].title) +
                       L"</td><td style=\"background-color:#FFFFFF;\">" + htmlCell(it.column(c, d)) + L"</td></tr>\r\n";
            out += L"</table><br>\r\n";
        }
        out += L"</body></html>";
        break;
    }
    case ExportFormat::Xml:
    {
        out += L"<?xml version=\"1.0\" ?>\r\n<windows_updates_history>\r\n";
        for (const auto& it : items)
        {
            out += L"<item>\r\n";
            for (int c : columns)
            {
                std::wstring tag = slug(kColumns[c].title);
                out += L"<" + tag + L">" + nlToBr(xmlEscape(it.column(c, d))) + L"</" + tag + L">\r\n";
            }
            out += L"</item>\r\n";
        }
        out += L"</windows_updates_history>\r\n";
        break;
    }
    case ExportFormat::Json:
    {
        out += L"[\r\n\r\n";
        for (size_t r = 0; r < items.size(); ++r)
        {
            out += L"{\r\n";
            for (size_t i = 0; i < columns.size(); ++i)
                out += L"\"" + std::wstring(kColumns[columns[i]].title) + L"\":\"" +
                       jsonEscape(items[r].column(columns[i], d)) + L"\"" + (i + 1 < columns.size() ? L",\r\n" : L"\r\n");
            out += L"}\r\n";
            if (r + 1 < items.size()) out += L",\r\n";
        }
        out += L"\r\n]\r\n";
        break;
    }
    }

    return out;
}

bool WriteReport(const std::wstring& path,
                 const std::vector<UpdateRecord>& items,
                 const std::vector<int>& columns,
                 ExportFormat format,
                 const ExportOptions& opt)
{
    std::wstring body = BuildReport(items, columns, format, opt);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    if (format == ExportFormat::Csv)
    {
        // CSV is UTF-8 with a BOM.
        static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        f.write(reinterpret_cast<const char*>(bom), 3);
        std::string u8 = toUtf8(body);
        f.write(u8.data(), static_cast<std::streamsize>(u8.size()));
    }
    else
    {
        // Text, tab, HTML, XML and JSON are UTF-16LE with a BOM.
        static const unsigned char bom[2] = { 0xFF, 0xFE };
        f.write(reinterpret_cast<const char*>(bom), 2);
        f.write(reinterpret_cast<const char*>(body.data()),
                static_cast<std::streamsize>(body.size() * sizeof(wchar_t)));
    }
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (sw == L"/stext")    { out = ExportFormat::Text;           return true; }
    if (sw == L"/stab")     { out = ExportFormat::Tab;            return true; }
    if (sw == L"/scomma")   { out = ExportFormat::Csv;            return true; }
    if (sw == L"/shtml")    { out = ExportFormat::HtmlHorizontal; return true; }
    if (sw == L"/sverhtml") { out = ExportFormat::HtmlVertical;   return true; }
    if (sw == L"/sxml")     { out = ExportFormat::Xml;            return true; }
    if (sw == L"/sjson")    { out = ExportFormat::Json;           return true; }
    return false;
}
