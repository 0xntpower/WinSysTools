// Export.cpp -- report builders for the seven ProductKeyScanner save formats.
#include "Export.h"
#include "Columns.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kReportTitle = L"Product Keys";  // STRINGTABLE id 8

    std::wstring htmlEscape(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
        {
            switch (c)
            {
            case L'&': out += L"&amp;";  break;
            case L'<': out += L"&lt;";   break;
            case L'>': out += L"&gt;";   break;
            case L'"': out += L"&quot;"; break;
            default:   out += c;         break;
            }
        }
        return out;
    }

    std::wstring jsonEscape(const std::wstring& s)
    {
        std::wstring out;
        for (wchar_t c : s)
        {
            switch (c)
            {
            case L'"':  out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n";  break;
            case L'\r': out += L"\\r";  break;
            case L'\t': out += L"\\t";  break;
            default:    out += c;       break;
            }
        }
        return out;
    }

    // CSV field quoting: wrap in quotes when the value holds a comma, quote or newline.
    std::wstring csvField(const std::wstring& s)
    {
        if (s.find_first_of(L",\"\r\n") == std::wstring::npos) return s;
        std::wstring out = L"\"";
        for (wchar_t c : s) { if (c == L'"') out += L'"'; out += c; }
        out += L"\"";
        return out;
    }

    // NirSoft XML/JSON element names: lowercased title with spaces turned into
    // underscores ("Product Key" -> "product_key").
    std::wstring tagName(const std::wstring& title)
    {
        std::wstring t = title;
        std::transform(t.begin(), t.end(), t.begin(), towlower);
        std::replace(t.begin(), t.end(), L' ', L'_');
        return t;
    }

    std::string toUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    void htmlHeader(std::wstring& out)
    {
        out += L"<html><head>";
        out += L"<meta http-equiv='content-type' content='text/html;charset=utf-8'>";
        out += L"<title>"; out += kReportTitle; out += L"</title><style>\r\n";
        out += L".mytable  { border: 1px groove}\r\n";
        out += L".mytable  td {padding: 5px ; border: 1px groove}\r\n";
        out += L".mytable th {padding: 10px; border: 1px groove;}\r\n";
        out += L"</style></head>\r\n<body>\r\n<h3>";
        out += kReportTitle;
        out += L"</h3>\r\n";
    }

    void htmlFooter(std::wstring& out)
    {
        out += L"<br/><h4>Created by using <a href=\"https://www.nirsoft.net/\" target=\"newwin\">"
               L"ProductKeyScanner</a></h4><p/>\r\n";
        out += L"</body></html>\r\n";
    }
}

std::wstring BuildReport(const std::vector<ProductKeyRecord>& items,
                         const std::vector<int>& columns,
                         ExportFormat format,
                         const ExportOptions& opts)
{
    std::wstring out;

    switch (format)
    {
    case ExportFormat::Text:
    {
        for (const auto& m : items)
        {
            for (int c : columns)
            {
                wchar_t line[1024];
                wsprintfW(line, L"%-18s: %s\r\n", kColumns[c].title, m.column(c).c_str());
                out += line;
            }
            out += L"==================================================\r\n\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        if (opts.addHeaderLine)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += std::wstring(kColumns[columns[i]].title) + (i + 1 < columns.size() ? L"\t" : L"");
            out += L"\r\n";
        }
        for (const auto& m : items)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += m.column(columns[i]) + (i + 1 < columns.size() ? L"\t" : L"");
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Csv:
    {
        if (opts.addHeaderLine)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += csvField(kColumns[columns[i]].title) + (i + 1 < columns.size() ? L"," : L"");
            out += L"\r\n";
        }
        for (const auto& m : items)
        {
            for (size_t i = 0; i < columns.size(); ++i)
                out += csvField(m.column(columns[i])) + (i + 1 < columns.size() ? L"," : L"");
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::HtmlHorizontal:
    {
        htmlHeader(out);
        out += L"<table class=\"mytable\"><tr>";
        for (int c : columns)
            out += L"<th>" + htmlEscape(kColumns[c].title) + L"</th>";
        out += L"</tr>\r\n";
        for (const auto& m : items)
        {
            out += L"<tr>";
            for (int c : columns)
                out += L"<td>" + htmlEscape(m.column(c)) + L"&nbsp;</td>";
            out += L"</tr>\r\n";
        }
        out += L"</table><p/>\r\n";
        htmlFooter(out);
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        htmlHeader(out);
        for (const auto& m : items)
        {
            out += L"<table class=\"mytable\">\r\n";
            for (int c : columns)
                out += L"<tr><td style=\"font-weight:700;white-space: nowrap;\">" + htmlEscape(kColumns[c].title) +
                       L"</td><td>" + htmlEscape(m.column(c)) + L"&nbsp;</td></tr>\r\n";
            out += L"</table><p/>\r\n";
        }
        htmlFooter(out);
        break;
    }
    case ExportFormat::Xml:
    {
        out += L"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\r\n";
        out += L"<product_keys>\r\n";
        for (const auto& m : items)
        {
            out += L"<item>\r\n";
            for (int c : columns)
            {
                std::wstring tag = tagName(kColumns[c].title);
                out += L"<" + tag + L">" + htmlEscape(m.column(c)) + L"</" + tag + L">\r\n";
            }
            out += L"</item>\r\n";
        }
        out += L"</product_keys>\r\n";
        break;
    }
    case ExportFormat::Json:
    {
        out += L"[\r\n";
        for (size_t r = 0; r < items.size(); ++r)
        {
            out += L"\t{\r\n";
            for (size_t i = 0; i < columns.size(); ++i)
            {
                int c = columns[i];
                out += L"\t\t\"" + tagName(kColumns[c].title) + L"\": \"" +
                       jsonEscape(items[r].column(c)) + L"\"" + (i + 1 < columns.size() ? L"," : L"") + L"\r\n";
            }
            out += L"\t}";
            out += (r + 1 < items.size() ? L",\r\n" : L"\r\n");
        }
        out += L"]\r\n";
        break;
    }
    }

    return out;
}

bool WriteReport(const std::wstring& path,
                 const std::vector<ProductKeyRecord>& items,
                 const std::vector<int>& columns,
                 ExportFormat format,
                 const ExportOptions& opts)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    // UTF-8 with a BOM, matching the UTF-8 XML declaration the original emits.
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    f.write(reinterpret_cast<const char*>(bom), 3);
    f << toUtf8(BuildReport(items, columns, format, opts));
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (_wcsicmp(sw.c_str(), L"/stext")    == 0) { out = ExportFormat::Text;           return true; }
    if (_wcsicmp(sw.c_str(), L"/stab")     == 0) { out = ExportFormat::Tab;            return true; }
    if (_wcsicmp(sw.c_str(), L"/sjson")    == 0) { out = ExportFormat::Json;           return true; }
    if (_wcsicmp(sw.c_str(), L"/shtml")    == 0) { out = ExportFormat::HtmlHorizontal; return true; }
    if (_wcsicmp(sw.c_str(), L"/sverhtml") == 0) { out = ExportFormat::HtmlVertical;   return true; }
    if (_wcsicmp(sw.c_str(), L"/sxml")     == 0) { out = ExportFormat::Xml;            return true; }
    if (_wcsicmp(sw.c_str(), L"/scomma")   == 0) { out = ExportFormat::Csv;            return true; }
    return false;
}
