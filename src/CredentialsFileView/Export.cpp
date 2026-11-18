// Export.cpp -- report builders for the CredentialsFileView save formats,
// following the older export engine (the one this binary uses). The
// text, tab, tabular, HTML, and XML reports are UTF-16LE with a BOM. The CSV
// report is UTF-8 with a BOM. The SaveFileEncoding option can force ANSI,
// UTF-16 or UTF-8 for every format.
#include "Export.h"
#include "Columns.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kReportTitle = L"Items List";
    const wchar_t* kHomePage = L"https://github.com/0xntpower/WinSysTools";
    const wchar_t* kProduct = L"CredentialsFileView";

    std::wstring esc(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
        {
            switch (c)
            {
            case L'&':
                out += L"&amp;";
                break;
            case L'<':
                out += L"&lt;";
                break;
            case L'>':
                out += L"&gt;";
                break;
            case L'"':
                out += L"&quot;";
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    std::wstring slug(const std::wstring& title)
    {
        std::wstring out;
        for (wchar_t c : title)
        {
            if (c == L' ')
            {
                out += L'_';
            }
            else if (c >= L'A' && c <= L'Z')
            {
                out += (wchar_t)(c - L'A' + L'a');
            }
            else
            {
                out += c;
            }
        }
        return out;
    }

    std::wstring csvField(const std::wstring& s)
    {
        if (s.find_first_of(L",\"\r\n") == std::wstring::npos)
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

    std::wstring pad(std::wstring s, size_t w)
    {
        if (s.size() < w)
        {
            s.resize(w, L' ');
        }
        return s;
    }

    std::string toMulti(const std::wstring& w, UINT cp)
    {
        if (w.empty())
        {
            return {};
        }
        int n = ::WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    const wchar_t* charsetName(int encoding, ExportFormat fmt)
    {
        int enc = encoding;
        if (enc == 0)
        {
            enc = (fmt == ExportFormat::Csv) ? 3 : 2;
        }
        if (enc == 3)
        {
            return L"utf-8";
        }
        if (enc == 1)
        {
            return L"windows-1252";
        }
        return L"utf-16";
    }
} // namespace

std::wstring BuildReport(const std::vector<CredRecord>& items, const std::vector<int>& columns, ExportFormat format,
                         const ExportOptions& opt)
{
    std::wstring out;

    switch (format)
    {
    case ExportFormat::Text:
    {
        for (const auto& it : items)
        {
            for (int c : columns)
            {
                std::wstring name = pad(kColumns[c].title, 18);
                out += name + L": " + it.column(c) + L"\r\n";
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        for (const auto& it : items)
        {
            for (size_t i = 0; i < columns.size(); ++i)
            {
                out += it.column(columns[i]) + (i + 1 < columns.size() ? L"\t" : L"");
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tabular:
    {
        std::vector<size_t> w;
        for (int c : columns)
        {
            size_t width = wcslen(kColumns[c].title);
            for (const auto& it : items)
            {
                width = std::max(width, it.column(c).size());
            }
            w.push_back(width);
        }
        for (size_t i = 0; i < columns.size(); ++i)
        {
            out += pad(kColumns[columns[i]].title, w[i]) + L" ";
        }
        out += L"\r\n";
        for (const auto& it : items)
        {
            for (size_t i = 0; i < columns.size(); ++i)
            {
                out += pad(it.column(columns[i]), w[i]) + L" ";
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Csv:
    {
        for (size_t i = 0; i < columns.size(); ++i)
        {
            out += csvField(kColumns[columns[i]].title) + (i + 1 < columns.size() ? L"," : L"");
        }
        out += L"\r\n";
        for (const auto& it : items)
        {
            for (size_t i = 0; i < columns.size(); ++i)
            {
                out += csvField(it.column(columns[i])) + (i + 1 < columns.size() ? L"," : L"");
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::HtmlHorizontal:
    {
        const wchar_t* cs = charsetName(opt.encoding, format);
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><meta http-equiv='content-type' content='text/html;charset=";
        out += cs;
        out += L"'><title>";
        out += kReportTitle;
        out += L"</title></head>\r\n<body>\r\n";
        out += kReportTitle;
        out += L" <h3>";
        out += kReportTitle;
        out += L"</h3>\r\n";
        out += L"<br><h4>Created by using <a href=\"";
        out += kHomePage;
        out += L"\" target=\"newwin\">";
        out += kProduct;
        out += L"</a></h4><p>\r\n";
        out += L"<table border=\"1\" cellpadding=\"5\"><tr bgcolor=\"#E0E0E0\">\r\n";
        for (int c : columns)
        {
            out += L"<th nowrap>" + std::wstring(kColumns[c].title) + L"</th>";
        }
        out += L"</tr>\r\n";
        for (const auto& it : items)
        {
            out += L"<tr>";
            for (int c : columns)
            {
                std::wstring v = esc(it.column(c));
                out += L"<td bgcolor=#FFFFFF nowrap>" + (v.empty() ? std::wstring(L"&nbsp;") : v) + L"</td>";
            }
            out += L"</tr>\r\n";
        }
        out += L"</table><p>\r\n</body></html>";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        const wchar_t* cs = charsetName(opt.encoding, format);
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><meta http-equiv='content-type' content='text/html;charset=";
        out += cs;
        out += L"'><title>";
        out += kReportTitle;
        out += L"</title></head>\r\n<body>\r\n";
        out += kReportTitle;
        out += L" <h3>";
        out += kReportTitle;
        out += L"</h3>\r\n";
        out += L"<br><h4>Created by using <a href=\"";
        out += kHomePage;
        out += L"\" target=\"newwin\">";
        out += kProduct;
        out += L"</a></h4><p>\r\n";
        for (const auto& it : items)
        {
            out += L"<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : columns)
            {
                std::wstring v = esc(it.column(c));
                out += L"<tr><td nowrap><b>" + std::wstring(kColumns[c].title) +
                       L"</b></td><td bgcolor=#FFFFFF nowrap>" + (v.empty() ? std::wstring(L"&nbsp;") : v) +
                       L"</td></tr>\r\n";
            }
            out += L"</table><p>\r\n";
        }
        out += L"</body></html>";
        break;
    }
    case ExportFormat::Xml:
    {
        out += L"<?xml version=\"1.0\" encoding=\"ISO-8859-1\" ?>\r\n";
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
        break;
    }
    }

    return out;
}

bool WriteReport(const std::wstring& path, const std::vector<CredRecord>& items, const std::vector<int>& columns,
                 ExportFormat format, const ExportOptions& opt)
{
    std::wstring body = BuildReport(items, columns, format, opt);
    std::ofstream f(path, std::ios::binary);
    if (!f)
    {
        return false;
    }

    int enc = opt.encoding;
    if (enc == 0)
    {
        enc = (format == ExportFormat::Csv) ? 3 : 2;
    }

    if (enc == 2) // UTF-16LE + BOM
    {
        static const unsigned char bom[2] = {0xFF, 0xFE};
        f.write(reinterpret_cast<const char*>(bom), 2);
        f.write(reinterpret_cast<const char*>(body.data()),
                static_cast<std::streamsize>(body.size() * sizeof(wchar_t)));
    }
    else if (enc == 3) // UTF-8 + BOM
    {
        static const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        f.write(reinterpret_cast<const char*>(bom), 3);
        std::string u8 = toMulti(body, CP_UTF8);
        f.write(u8.data(), static_cast<std::streamsize>(u8.size()));
    }
    else // ANSI
    {
        std::string a = toMulti(body, CP_ACP);
        f.write(a.data(), static_cast<std::streamsize>(a.size()));
    }
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (sw == L"/stext")
    {
        out = ExportFormat::Text;
        return true;
    }
    if (sw == L"/stab")
    {
        out = ExportFormat::Tab;
        return true;
    }
    if (sw == L"/stabular")
    {
        out = ExportFormat::Tabular;
        return true;
    }
    if (sw == L"/shtml")
    {
        out = ExportFormat::HtmlHorizontal;
        return true;
    }
    if (sw == L"/sverhtml")
    {
        out = ExportFormat::HtmlVertical;
        return true;
    }
    if (sw == L"/sxml")
    {
        out = ExportFormat::Xml;
        return true;
    }
    if (sw == L"/scomma")
    {
        out = ExportFormat::Csv;
        return true;
    }
    return false;
}
