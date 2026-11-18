// Export.cpp -- report builders for the OutlookAttachView save formats, matching
// the original's output. OutlookAttachView uses the older NirSoft HTML template
// (an HTML 3.2 doctype with a bgcolor table, not the CSS mytable template) and
// the slug-tag XML template. The text, tab, tabular, HTML and XML reports are
// UTF-16LE with a BOM. Only the CSV report defaults to UTF-8 with a BOM. The
// SaveFileEncoding option can force ANSI, UTF-16 or UTF-8 for every format.
#include "Export.h"
#include "Columns.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kReportTitle = L"Outlook Attachments";
    const wchar_t* kCredit = L"Created by using";
    const wchar_t* kProduct = L"OutlookAttachView";
    const wchar_t* kHomePage = L"http://www.nirsoft.net/";

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

    std::wstring csvField(const std::wstring& s)
    {
        if (s.find_first_of(L",\"\r\n") == std::wstring::npos) return s;
        std::wstring out = L"\"";
        for (wchar_t c : s) { if (c == L'"') out += L'"'; out += c; }
        out += L'"';
        return out;
    }

    // Slug a title the way the original does: lower-case and replace space,
    // backslash, slash and parentheses with an underscore.
    std::wstring slug(const std::wstring& title)
    {
        std::wstring out;
        for (wchar_t c : title)
        {
            if (c == L' ' || c == L'\\' || c == L'/' || c == L'(' || c == L')') out += L'_';
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

std::wstring BuildReport(const std::vector<AttachRecord>& items,
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
                if (name.size() < 18) name.resize(18, L' ');
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
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>"; out += kReportTitle; out += L"</title></head>\r\n<body>\r\n <h3>";
        out += kReportTitle; out += L"</h3>\r\n";
        out += L"<br><h4>"; out += kCredit; out += L" <a href=\""; out += kHomePage;
        out += L"\" target=\"newwin\">"; out += kProduct; out += L"</a></h4><p>";
        out += L"<table border=\"1\" cellpadding=\"5\"><tr>\r\n";
        for (int c : columns)
            out += L"<th width=\"" + std::to_wstring(kColumns[c].htmlWidth) + L"\">" + kColumns[c].title + L"\r\n";
        for (const auto& it : items)
        {
            out += L"<tr>";
            for (int c : columns)
            {
                std::wstring v = esc(it.column(c));
                if (v.empty()) v = L"&nbsp;";
                out += L"<td bgcolor=#FFFFFF nowrap>" + v;
            }
            out += L"\r\n";
        }
        out += L"</table>\r\n</body></html>";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>"; out += kReportTitle; out += L"</title></head>\r\n<body>\r\n <h3>";
        out += kReportTitle; out += L"</h3>\r\n";
        out += L"<br><h4>"; out += kCredit; out += L" <a href=\""; out += kHomePage;
        out += L"\" target=\"newwin\">"; out += kProduct; out += L"</a></h4><p>";
        for (const auto& it : items)
        {
            out += L"<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : columns)
            {
                std::wstring v = esc(it.column(c));
                if (v.empty()) v = L"&nbsp;";
                out += L"<tr><td nowrap><b>" + std::wstring(kColumns[c].title) +
                       L"</b><td bgcolor=#FFFFFF nowrap>" + v + L"\r\n";
            }
            out += L"</table><br>\r\n";
        }
        out += L"</body></html>";
        break;
    }
    case ExportFormat::Xml:
    {
        out += L"<?xml version=\"1.0\" ?>\r\n";
        out += L"<" + slug(kReportTitle) + L">\r\n";
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
        out += L"</" + slug(kReportTitle) + L">\r\n";
        break;
    }
    }

    return out;
}

bool WriteReport(const std::wstring& path,
                 const std::vector<AttachRecord>& items,
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
    if (_wcsicmp(sw.c_str(), L"/stext") == 0)    { out = ExportFormat::Text;           return true; }
    if (_wcsicmp(sw.c_str(), L"/stab") == 0)     { out = ExportFormat::Tab;            return true; }
    if (_wcsicmp(sw.c_str(), L"/stabular") == 0) { out = ExportFormat::Tabular;        return true; }
    if (_wcsicmp(sw.c_str(), L"/scomma") == 0)   { out = ExportFormat::Csv;            return true; }
    if (_wcsicmp(sw.c_str(), L"/shtml") == 0)    { out = ExportFormat::HtmlHorizontal; return true; }
    if (_wcsicmp(sw.c_str(), L"/sverhtml") == 0) { out = ExportFormat::HtmlVertical;   return true; }
    if (_wcsicmp(sw.c_str(), L"/sxml") == 0)     { out = ExportFormat::Xml;            return true; }
    return false;
}
