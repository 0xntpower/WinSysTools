// Export.cpp -- report builders for the six OfficeIns save formats. The original is
// an ANSI app, so the files are written in the system ANSI code page for byte fidelity.
#include "Export.h"
#include "Columns.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kReportTitle = L"Office Add-Ins List";   // STRINGTABLE id 8

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

    // The XML element names are the lowercased title with spaces turned into
    // underscores: "Start Mode" -> "start_mode".
    std::wstring xmlTag(const std::wstring& title)
    {
        std::wstring t = title;
        std::transform(t.begin(), t.end(), t.begin(), towlower);
        std::replace(t.begin(), t.end(), L' ', L'_');
        return t;
    }

    std::string toAnsi(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    void htmlHeader(std::wstring& out)
    {
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>";
        out += kReportTitle;
        out += L"</title></head>\r\n<body>\r\n<h3>";
        out += kReportTitle;
        out += L"</h3>\r\n";
    }

    void htmlFooter(std::wstring& out)
    {
        // "Created by using" (STRINGTABLE id 6) plus the tool name.
        out += L"<p><br><h4>Created by using OfficeIns</h4>\r\n";
        out += L"</body></html>\r\n";
    }
}

std::wstring BuildReport(const std::vector<Addin>& items,
                         const std::vector<int>& columns,
                         ExportFormat format)
{
    std::wstring out;

    switch (format)
    {
    case ExportFormat::Text:
    {
        // The /stext mode wraps each record with a 50-char separator above and
        // below, titles left-padded to the widest column title, separator " : ".
        const std::wstring sep(50, L'=');
        size_t titleW = 0;
        for (int c : columns) titleW = std::max(titleW, wcslen(kColumns[c].title));
        for (const auto& m : items)
        {
            out += sep + L"\r\n";
            for (int c : columns)
            {
                std::wstring title = kColumns[c].title;
                title.resize(std::max(title.size(), titleW), L' ');
                out += title + L" : " + m.column(c) + L"\r\n";
            }
            out += sep + L"\r\n\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        // The /stab mode appends a tab after every column, including the last.
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
        // Fixed per-column widths, each value truncated to width-1 then padded to
        // width. No header row, matching the original /stabular output.
        for (const auto& m : items)
        {
            for (int c : columns)
            {
                int w = kTabularWidth[c];
                std::wstring v = m.column(c);
                if ((int)v.size() > w - 1) v.resize(w - 1);
                v.resize(w, L' ');
                out += v;
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::HtmlHorizontal:
    {
        htmlHeader(out);
        out += L"<table border=\"1\" cellpadding=\"5\"><tr bgcolor=\"#cccccc\">";
        for (int c : columns)
            out += L"<td><b>" + htmlEscape(kColumns[c].title) + L"</b></td>";
        out += L"</tr>\r\n";
        for (const auto& m : items)
        {
            out += L"<tr>";
            for (int c : columns)
                out += L"<td nowrap>" + htmlEscape(m.column(c)) + L"&nbsp;</td>";
            out += L"</tr>\r\n";
        }
        out += L"</table>\r\n";
        htmlFooter(out);
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        htmlHeader(out);
        for (const auto& m : items)
        {
            out += L"<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : columns)
                out += L"<tr><td bgcolor=\"#cccccc\" nowrap><b>" + htmlEscape(kColumns[c].title) +
                       L"</b></td><td nowrap>" + htmlEscape(m.column(c)) + L"&nbsp;</td></tr>\r\n";
            out += L"</table><br>\r\n";
        }
        htmlFooter(out);
        break;
    }
    case ExportFormat::Xml:
    {
        std::wstring root = xmlTag(kReportTitle);
        out += L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n";
        out += L"<" + root + L">\r\n";
        for (const auto& m : items)
        {
            out += L"\t<item>\r\n";
            for (int c : columns)
            {
                std::wstring tag = xmlTag(kColumns[c].title);
                out += L"\t\t<" + tag + L">" + htmlEscape(m.column(c)) + L"</" + tag + L">\r\n";
            }
            out += L"\t</item>\r\n";
        }
        out += L"</" + root + L">\r\n";
        break;
    }
    }

    return out;
}

bool WriteReport(const std::wstring& path,
                 const std::vector<Addin>& items,
                 const std::vector<int>& columns,
                 ExportFormat format)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << toAnsi(BuildReport(items, columns, format));
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (_wcsicmp(sw.c_str(), L"/stext")    == 0) { out = ExportFormat::Text;           return true; }
    if (_wcsicmp(sw.c_str(), L"/stab")     == 0) { out = ExportFormat::Tab;            return true; }
    if (_wcsicmp(sw.c_str(), L"/stabular") == 0) { out = ExportFormat::Tabular;        return true; }
    if (_wcsicmp(sw.c_str(), L"/shtml")    == 0) { out = ExportFormat::HtmlHorizontal; return true; }
    if (_wcsicmp(sw.c_str(), L"/sverhtml") == 0) { out = ExportFormat::HtmlVertical;   return true; }
    if (_wcsicmp(sw.c_str(), L"/sxml")     == 0) { out = ExportFormat::Xml;            return true; }
    return false;
}
