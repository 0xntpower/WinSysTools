// Export.cpp -- report builders for the SimpleProgramDebugger save formats,
// matching the original's output structure and encoding. Every format except the
// comma-delimited one is UTF-16LE with a BOM. The comma-delimited report is UTF-8
// with no BOM. These were confirmed by capturing the original's own exports.
//
// The text report wraps each record in a 50-'=' rule and left-justifies field
// names to width 18. The tab report terminates every field with a tab, including
// the last. The HTML reports use the classic NirSoft "border=1 cellpadding=5"
// template with a credit line to nirsoft.net. The XML root is debug_events_list.
#include "Export.h"
#include "Columns.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kReportTitle = L"Debug Events List";
    const wchar_t* kHomePage = L"http://www.nirsoft.net/";
    const wchar_t* kProduct = L"SimpleProgramDebugger";

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

    std::wstring htmlCell(const std::wstring& raw)
    {
        std::wstring e = esc(raw);
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
            else if (c >= L'A' && c <= L'Z') out += static_cast<wchar_t>(c - L'A' + L'a');
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

std::wstring BuildReport(const std::vector<DbgRow>& items,
                         const std::vector<int>& columns,
                         ExportFormat format,
                         const ExportOptions& opt)
{
    std::wstring out;

    switch (format)
    {
    case ExportFormat::Text:
    {
        const std::wstring rule(50, L'=');
        for (const auto& it : items)
        {
            out += rule + L"\r\n";
            for (int c : columns)
            {
                std::wstring name = kColumns[c].title;
                name.resize(std::max<size_t>(name.size(), 18), L' ');
                out += name + L": " + it.column(c) + L"\r\n";
            }
            out += rule + L"\r\n\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        if (opt.addHeaderLine)
        {
            for (int c : columns)
                out += std::wstring(kColumns[c].title) + L"\t";
            out += L"\r\n";
        }
        for (const auto& it : items)
        {
            for (int c : columns)
                out += it.column(c) + L"\t";
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tabular:
    {
        if (!items.empty())
        {
            std::vector<size_t> w;
            for (int c : columns)
            {
                size_t m = wcslen(kColumns[c].title);
                for (const auto& it : items) m = std::max(m, it.column(c).size());
                w.push_back(m);
            }
            auto pad = [](std::wstring s, size_t width) { if (s.size() < width) s.resize(width, L' '); return s + L" "; };
            for (size_t i = 0; i < columns.size(); ++i) out += pad(kColumns[columns[i]].title, w[i]);
            out += L"\r\n";
            for (const auto& it : items)
            {
                for (size_t i = 0; i < columns.size(); ++i) out += pad(it.column(columns[i]), w[i]);
                out += L"\r\n";
            }
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
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>"; out += kReportTitle; out += L"</title></head>\r\n<body>\r\n <h3>";
        out += kReportTitle; out += L"</h3>\r\n<br><h4>Created by using <a href=\"";
        out += kHomePage; out += L"\" target=\"newwin\">"; out += kProduct;
        out += L"</a></h4><p><table border=\"1\" cellpadding=\"5\"><tr bgcolor=\"E0E0E0\">\r\n";
        for (int c : columns)
            out += L"<th>" + std::wstring(kColumns[c].title) + L"\r\n";
        for (const auto& it : items)
        {
            out += L"<tr>\r\n";
            for (int c : columns)
                out += L"<td bgcolor=#FFFFFF nowrap>" + htmlCell(it.column(c)) + L"\r\n";
        }
        out += L"</table>\r\n\r\n</body></html>";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>"; out += kReportTitle; out += L"</title></head>\r\n<body>\r\n <h3>";
        out += kReportTitle; out += L"</h3>\r\n<br><h4>Created by using <a href=\"";
        out += kHomePage; out += L"\" target=\"newwin\">"; out += kProduct; out += L"</a></h4><p>\r\n";
        for (const auto& it : items)
        {
            out += L"<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : columns)
                out += L"<tr><td bgcolor=#E0E0E0 nowrap><b>" + std::wstring(kColumns[c].title) +
                       L"</b><td bgcolor=#FFFFFF>" + htmlCell(it.column(c)) + L"\r\n";
            out += L"</table><p>\r\n";
        }
        out += L"</body></html>";
        break;
    }
    case ExportFormat::Xml:
    {
        out += L"<?xml version=\"1.0\" ?>\r\n<debug_events_list>\r\n";
        for (const auto& it : items)
        {
            out += L"<item>\r\n";
            for (int c : columns)
            {
                std::wstring tag = slug(kColumns[c].title);
                out += L"<" + tag + L">" + esc(it.column(c)) + L"</" + tag + L">\r\n";
            }
            out += L"</item>\r\n";
        }
        out += L"</debug_events_list>\r\n";
        break;
    }
    }

    return out;
}

bool WriteReport(const std::wstring& path,
                 const std::vector<DbgRow>& items,
                 const std::vector<int>& columns,
                 ExportFormat format,
                 const ExportOptions& opt)
{
    std::wstring body = BuildReport(items, columns, format, opt);
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    if (format == ExportFormat::Csv)
    {
        // UTF-8, no BOM.
        std::string u8 = toUtf8(body);
        f.write(u8.data(), static_cast<std::streamsize>(u8.size()));
    }
    else
    {
        // UTF-16LE with a BOM.
        static const unsigned char bom[2] = { 0xFF, 0xFE };
        f.write(reinterpret_cast<const char*>(bom), 2);
        f.write(reinterpret_cast<const char*>(body.data()),
                static_cast<std::streamsize>(body.size() * sizeof(wchar_t)));
    }
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (_wcsicmp(sw.c_str(), L"/stext") == 0)    { out = ExportFormat::Text;           return true; }
    if (_wcsicmp(sw.c_str(), L"/stab") == 0)     { out = ExportFormat::Tab;            return true; }
    if (_wcsicmp(sw.c_str(), L"/stabular") == 0) { out = ExportFormat::Tabular;        return true; }
    if (_wcsicmp(sw.c_str(), L"/shtml") == 0)    { out = ExportFormat::HtmlHorizontal; return true; }
    if (_wcsicmp(sw.c_str(), L"/sverhtml") == 0) { out = ExportFormat::HtmlVertical;   return true; }
    if (_wcsicmp(sw.c_str(), L"/sxml") == 0)     { out = ExportFormat::Xml;            return true; }
    if (_wcsicmp(sw.c_str(), L"/scomma") == 0)   { out = ExportFormat::Csv;            return true; }
    return false;
}

ExportFormat FormatFromFilterIndex(int idx)
{
    switch (idx)
    {
    case 1:  return ExportFormat::Text;
    case 2:  return ExportFormat::Tab;
    case 3:  return ExportFormat::Tabular;
    case 4:  return ExportFormat::HtmlHorizontal;
    case 5:  return ExportFormat::HtmlVertical;
    case 6:  return ExportFormat::Xml;
    case 7:  return ExportFormat::Csv;
    default: return ExportFormat::Text;
    }
}
