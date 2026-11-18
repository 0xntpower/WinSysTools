// Export.cpp -- report builders for the TaskSchedulerView save formats, matching
// the original's output (field order, structure and encoding). The text, tab,
// tabular, HTML, XML and JSON reports are UTF-16LE with a BOM. Only the CSV
// report is UTF-8 with a BOM. The /SaveFileEncoding option can force ANSI,
// UTF-16 or UTF-8 for every format.
#include "Export.h"
#include "Columns.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kReportTitle = L"Tasks List";
    const wchar_t* kHomePage = L"https://github.com/0xntpower/WinSysTools";
    const wchar_t* kProduct = L"TaskSchedulerView";

    std::wstring esc(const std::wstring& s)
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

    std::wstring jsonEscape(const std::wstring& s)
    {
        std::wstring out;
        for (wchar_t c : s) { if (c == L'"') out += L"\\\""; else out += c; }
        return out;
    }

    std::wstring nlToBr(const std::wstring& s)
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

    std::wstring htmlCell(const std::wstring& raw)
    {
        std::wstring e = nlToBr(esc(raw));
        if (raw.rfind(L"http://", 0) == 0 || raw.rfind(L"https://", 0) == 0)
            return L"<a href=\"" + e + L"\">" + e + L"</a>";
        return e.empty() ? std::wstring(L"&nbsp;") : e;
    }

    std::wstring csvField(const std::wstring& s)
    {
        if (s.find_first_of(L",\"\r\n") == std::wstring::npos) return s;
        std::wstring out = L"\"";
        for (wchar_t c : s) { if (c == L'"') out += L'"'; out += c; }
        out += L'"';
        return out;
    }

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

    std::string toMulti(const std::wstring& w, UINT cp)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }
}

std::wstring BuildReport(const std::vector<TaskRecord>& items,
                         const std::vector<int>& columns,
                         ExportFormat format,
                         const ExportOptions& opt)
{
    std::wstring out;

    switch (format)
    {
    case ExportFormat::Text:
    {
        const std::wstring sep(50, L'=');
        for (const auto& it : items)
        {
            out += sep + L"\r\n";
            for (int c : columns)
            {
                std::wstring name = kColumns[c].title;
                name.resize(std::max<size_t>(name.size(), 18), L' ');
                out += name + L": " + it.column(c) + L"\r\n";
            }
            out += sep + L"\r\n\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        if (opt.addHeaderLine)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += std::wstring(kColumns[columns[i]].title) + (i + 1 < columns.size() ? L"\t" : L"");
            out += L"\r\n";
        }
        for (const auto& it : items)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += it.column(columns[i]) + (i + 1 < columns.size() ? L"\t" : L"");
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tabular:
    {
        auto pad = [](std::wstring s, size_t w) { if (s.size() < w) s.resize(w, L' '); return s + L" "; };
        std::vector<size_t> w;
        for (int c : columns) w.push_back(std::max<size_t>(wcslen(kColumns[c].title), 12));
        if (opt.addHeaderLine)
        {
            for (size_t i = 0; i < columns.size(); ++i) out += pad(kColumns[columns[i]].title, w[i]);
            out += L"\r\n";
        }
        for (const auto& it : items)
        {
            for (size_t i = 0; i < columns.size(); ++i) out += pad(it.column(columns[i]), w[i]);
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
                out += csvField(it.column(columns[i])) + (i + 1 < columns.size() ? L"," : L"");
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
        out += L"<br/><h4>Created by using <a href=\""; out += kHomePage;
        out += L"\" target=\"newwin\">"; out += kProduct; out += L"</a></h4><p/><table style=\"width: ";
        out += std::to_wstring(total); out += L"px\" class=\"mytable\"><tr style=\"background-color:#E0E0E0;\">\r\n";
        for (int c : columns)
            out += L"<th style=\"width: " + std::to_wstring(kColumns[c].htmlWidth) + L"px\">" +
                   kColumns[c].title + L"</th>\r\n";
        out += L"</tr>";
        for (const auto& it : items)
        {
            out += L"<tr>";
            for (int c : columns)
                out += L"<td style=\"background-color:#FFFFFF;\">" + htmlCell(it.column(c)) + L"</td>";
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
        out += L"<br/><h4>Created by using <a href=\""; out += kHomePage;
        out += L"\" target=\"newwin\">"; out += kProduct; out += L"</a></h4><p/>\r\n";
        for (const auto& it : items)
        {
            out += L"<table class=\"mytable\">\r\n";
            for (int c : columns)
                out += L"<tr><td style=\"background-color:#E0E0E0;\">" + std::wstring(kColumns[c].title) +
                       L"</td><td style=\"background-color:#FFFFFF;\">" + htmlCell(it.column(c)) + L"</td></tr>\r\n";
            out += L"</table><br>\r\n";
        }
        out += L"</body></html>";
        break;
    }
    case ExportFormat::Xml:
    {
        out += L"<?xml version=\"1.0\" encoding=\"UTF-16\" ?>\r\n<tasks_list>\r\n";
        for (const auto& it : items)
        {
            out += L"<item>\r\n";
            for (int c : columns)
            {
                std::wstring tag = slug(kColumns[c].title);
                out += L"<" + tag + L">" + nlToBr(esc(it.column(c))) + L"</" + tag + L">\r\n";
            }
            out += L"</item>\r\n";
        }
        out += L"</tasks_list>\r\n";
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
                       jsonEscape(items[r].column(columns[i])) + L"\"" + (i + 1 < columns.size() ? L",\r\n" : L"\r\n");
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
                 const std::vector<TaskRecord>& items,
                 const std::vector<int>& columns,
                 ExportFormat format,
                 const ExportOptions& opt)
{
    std::wstring body = BuildReport(items, columns, format, opt);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    // Pick the encoding: honour an explicit SaveFileEncoding, otherwise CSV is
    // UTF-8 with a BOM and every other format is UTF-16LE with a BOM.
    int enc = opt.encoding;
    if (enc == 0) enc = (format == ExportFormat::Csv) ? 3 : 2;

    if (enc == 2)  // UTF-16LE + BOM
    {
        static const unsigned char bom[2] = { 0xFF, 0xFE };
        f.write(reinterpret_cast<const char*>(bom), 2);
        f.write(reinterpret_cast<const char*>(body.data()),
                static_cast<std::streamsize>(body.size() * sizeof(wchar_t)));
    }
    else if (enc == 3)  // UTF-8 + BOM
    {
        static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        f.write(reinterpret_cast<const char*>(bom), 3);
        std::string u8 = toMulti(body, CP_UTF8);
        f.write(u8.data(), static_cast<std::streamsize>(u8.size()));
    }
    else  // ANSI
    {
        std::string a = toMulti(body, CP_ACP);
        f.write(a.data(), static_cast<std::streamsize>(a.size()));
    }
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (sw == L"/stext")    { out = ExportFormat::Text;           return true; }
    if (sw == L"/stab")     { out = ExportFormat::Tab;            return true; }
    if (sw == L"/stabular") { out = ExportFormat::Tabular;        return true; }
    if (sw == L"/shtml")    { out = ExportFormat::HtmlHorizontal; return true; }
    if (sw == L"/sverhtml") { out = ExportFormat::HtmlVertical;   return true; }
    if (sw == L"/sxml")     { out = ExportFormat::Xml;            return true; }
    if (sw == L"/scomma")   { out = ExportFormat::Csv;            return true; }
    if (sw == L"/sjson")    { out = ExportFormat::Json;           return true; }
    return false;
}
