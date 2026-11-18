// Export.cpp -- the seven list export formats. Report title "Memory Blocks List"
// (string 8) and footer "Created by using" (string 6) match the original.
#include "Export.h"
#include "Columns.h"
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kTitle  = L"Memory Blocks List";
    const wchar_t* kFooter = L"Created by using HeapMemView";

    std::wstring esc_html(const std::wstring& s)
    {
        std::wstring o;
        for (wchar_t c : s)
        {
            switch (c)
            {
            case L'&': o += L"&amp;"; break;
            case L'<': o += L"&lt;"; break;
            case L'>': o += L"&gt;"; break;
            default:   o += c; break;
            }
        }
        if (o.empty()) o = L"&nbsp;";
        return o;
    }

    std::wstring esc_xml(const std::wstring& s)
    {
        std::wstring o;
        for (wchar_t c : s)
        {
            switch (c)
            {
            case L'&': o += L"&amp;"; break;
            case L'<': o += L"&lt;"; break;
            case L'>': o += L"&gt;"; break;
            case L'"': o += L"&quot;"; break;
            default:   o += c; break;
            }
        }
        return o;
    }

    std::wstring textReport(const std::vector<HeapBlock>& items, const std::vector<int>& cols)
    {
        size_t widest = 0;
        for (int c : cols) widest = std::max(widest, wcslen(kColumns[c].title));

        std::wstring out;
        const std::wstring line(60, L'=');
        for (const auto& it : items)
        {
            out += line; out += L"\r\n";
            for (int c : cols)
            {
                std::wstring t = kColumns[c].title;
                t.append(widest - t.size(), L' ');
                out += t; out += L" : "; out += it.column(c); out += L"\r\n";
            }
            out += L"\r\n";
        }
        return out;
    }

    std::wstring delimitedReport(const std::vector<HeapBlock>& items, const std::vector<int>& cols, wchar_t sep)
    {
        std::wstring out;
        for (size_t i = 0; i < cols.size(); ++i)
        {
            if (i) out += sep;
            out += kColumns[cols[i]].title;
        }
        out += L"\r\n";
        for (const auto& it : items)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                if (i) out += sep;
                out += it.column(cols[i]);
            }
            out += L"\r\n";
        }
        return out;
    }

    std::wstring tabularReport(const std::vector<HeapBlock>& items, const std::vector<int>& cols)
    {
        // Fixed per-column field width, each value truncated to width-1 and padded.
        std::vector<size_t> w;
        for (int c : cols)
        {
            size_t mx = wcslen(kColumns[c].title);
            for (const auto& it : items) mx = std::max(mx, it.column(c).size());
            w.push_back(mx + 2);
        }
        std::wstring out;
        for (const auto& it : items)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                std::wstring v = it.column(cols[i]);
                if (v.size() > w[i] - 1) v.resize(w[i] - 1);
                v.append(w[i] - v.size(), L' ');
                out += v;
            }
            out += L"\r\n";
        }
        return out;
    }

    std::wstring htmlReport(const std::vector<HeapBlock>& items, const std::vector<int>& cols, bool horizontal)
    {
        std::wstring out =
            L"<html>\r\n<head>\r\n<title>" + std::wstring(kTitle) + L"</title>\r\n</head>\r\n"
            L"<body bgcolor=\"#FFFFFF\">\r\n<h3>" + std::wstring(kTitle) + L"</h3>\r\n";

        if (horizontal)
        {
            out += L"<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">\r\n<tr bgcolor=\"#CCCCFF\">";
            for (int c : cols) out += L"<td><b>" + esc_html(kColumns[c].title) + L"</b></td>";
            out += L"</tr>\r\n";
            for (const auto& it : items)
            {
                out += L"<tr>";
                for (int c : cols) out += L"<td nowrap>" + esc_html(it.column(c)) + L"</td>";
                out += L"</tr>\r\n";
            }
            out += L"</table>\r\n";
        }
        else
        {
            for (const auto& it : items)
            {
                out += L"<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">\r\n";
                for (int c : cols)
                    out += L"<tr><td bgcolor=\"#CCCCFF\"><b>" + esc_html(kColumns[c].title) +
                           L"</b></td><td nowrap>" + esc_html(it.column(c)) + L"</td></tr>\r\n";
                out += L"</table>\r\n<br>\r\n";
            }
        }

        out += L"<p><font size=\"2\">" + std::wstring(kFooter) +
               L" HeapMemView</font></p>\r\n</body>\r\n</html>\r\n";
        return out;
    }

    std::wstring xmlName(int c)
    {
        std::wstring n = kColumns[c].title, o;
        for (wchar_t ch : n) if (ch != L' ' && ch != L'.' && ch != L'-') o += ch;
        if (o.empty() || iswdigit(o[0])) o = L"_" + o;
        return o;
    }

    std::wstring xmlReport(const std::vector<HeapBlock>& items, const std::vector<int>& cols)
    {
        std::wstring out = L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<memory_blocks_list>\r\n";
        for (const auto& it : items)
        {
            out += L"  <item>\r\n";
            for (int c : cols)
            {
                std::wstring tag = xmlName(c);
                out += L"    <" + tag + L">" + esc_xml(it.column(c)) + L"</" + tag + L">\r\n";
            }
            out += L"  </item>\r\n";
        }
        out += L"</memory_blocks_list>\r\n";
        return out;
    }
}

std::wstring BuildReport(const std::vector<HeapBlock>& items, const std::vector<int>& cols, ExportFormat fmt)
{
    switch (fmt)
    {
    case ExportFormat::Text:           return textReport(items, cols);
    case ExportFormat::Tab:            return delimitedReport(items, cols, L'\t');
    case ExportFormat::Tabular:        return tabularReport(items, cols);
    case ExportFormat::HtmlHorizontal: return htmlReport(items, cols, true);
    case ExportFormat::HtmlVertical:   return htmlReport(items, cols, false);
    case ExportFormat::Xml:            return xmlReport(items, cols);
    case ExportFormat::Comma:          return delimitedReport(items, cols, L',');
    }
    return L"";
}

void WriteReport(const std::wstring& path, const std::vector<HeapBlock>& items,
                 const std::vector<int>& cols, ExportFormat fmt)
{
    std::wstring w = BuildReport(items, cols, fmt);
    int len = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string a(len > 0 ? len - 1 : 0, '\0');
    if (len > 1)
        ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, a.data(), len, nullptr, nullptr);
    std::ofstream f(path, std::ios::binary);
    f.write(a.data(), static_cast<std::streamsize>(a.size()));
}

bool FormatFromFilterIndex(int index, ExportFormat& out)
{
    if (index < 1 || index > 7) return false;
    out = static_cast<ExportFormat>(index - 1);
    return true;
}
