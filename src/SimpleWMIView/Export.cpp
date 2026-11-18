// Export.cpp -- report builders for the SimpleWMIView save formats.
//
// Structure follows the original's output. The exact tab / csv / text layouts
// were confirmed by running the original binary. Encoding honours the Save File
// Encoding option (Default is UTF-16LE with a BOM, matching the original).
#include "Export.h"
#include <windows.h>
#include <fstream>
#include <cstdio>
#include <algorithm>
#include <io.h>
#include <fcntl.h>

namespace
{
    const wchar_t* kAppName = L"SimpleWMIView";

    std::vector<int> effectiveColumns(const WmiResult& res, const ExportOptions& opt)
    {
        if (!opt.columns.empty())
        {
            return opt.columns;
        }
        std::vector<int> all;
        for (int i = 0; i < (int)res.columns.size(); ++i)
        {
            all.push_back(i);
        }
        return all;
    }

    std::wstring cellText(const WmiResult& res, const WmiRow& r, int col, const DisplayOptions& d)
    {
        if (col < 0 || col >= (int)r.cells.size())
        {
            return {};
        }
        return r.cells[col].text(d);
    }

    std::wstring htmlEscape(const std::wstring& s)
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
            default:
                out += c;
                break;
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

    // XML element name from a column name: lowercase, spaces -> underscore.
    std::wstring slug(const std::wstring& name)
    {
        std::wstring out;
        for (wchar_t c : name)
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
        return out.empty() ? L"field" : out;
    }

    std::string toUtf8(const std::wstring& w, bool bom)
    {
        std::string out;
        if (bom)
        {
            out.append("\xEF\xBB\xBF");
        }
        if (w.empty())
        {
            return out;
        }
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string body(n, '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), body.data(), n, nullptr, nullptr);
        out += body;
        return out;
    }

    std::string toAnsi(const std::wstring& w)
    {
        if (w.empty())
        {
            return {};
        }
        int n = ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }
} // namespace

std::wstring BuildReport(const WmiResult& res, ExportFormat format, const ExportOptions& opt)
{
    const DisplayOptions& d = opt.display;
    std::vector<int> cols = effectiveColumns(res, opt);
    std::wstring out;

    switch (format)
    {
    case ExportFormat::Text:
    {
        // Each record is bracketed by a 50-'=' rule and followed by a blank
        // line. Field names are left-justified to a minimum width of 18 (names
        // longer than 18 are not truncated), then ": ".
        const std::wstring sep(50, L'=');
        for (const auto& r : res.rows)
        {
            out += sep + L"\r\n";
            for (int c : cols)
            {
                std::wstring name = res.columns[c].name;
                name.resize(std::max<size_t>(name.size(), 18), L' ');
                out += name + L": " + cellText(res, r, c, d) + L"\r\n";
            }
            out += sep + L"\r\n\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        // Header row of column names, then one tab-separated row per record
        // (no trailing tab, matching v1.55 and later).
        for (size_t i = 0; i < cols.size(); ++i)
        {
            if (i)
            {
                out += L'\t';
            }
            out += res.columns[cols[i]].name;
        }
        out += L"\r\n";
        for (const auto& r : res.rows)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                if (i)
                {
                    out += L'\t';
                }
                out += cellText(res, r, cols[i], d);
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tabular:
    {
        // Fixed-width columns, each padded to its widest value (or header).
        std::vector<size_t> w(cols.size());
        for (size_t i = 0; i < cols.size(); ++i)
        {
            w[i] = res.columns[cols[i]].name.size();
        }
        for (const auto& r : res.rows)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                w[i] = std::max(w[i], cellText(res, r, cols[i], d).size());
            }
        }
        for (size_t i = 0; i < cols.size(); ++i)
        {
            std::wstring h = res.columns[cols[i]].name;
            h.resize(w[i], L' ');
            out += h;
            out += (i + 1 < cols.size()) ? L" " : L"";
        }
        out += L"\r\n";
        for (const auto& r : res.rows)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                std::wstring v = cellText(res, r, cols[i], d);
                v.resize(w[i], L' ');
                out += v;
                out += (i + 1 < cols.size()) ? L" " : L"";
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Csv:
    {
        for (size_t i = 0; i < cols.size(); ++i)
        {
            out += csvField(res.columns[cols[i]].name) + (i + 1 < cols.size() ? L"," : L"");
        }
        out += L"\r\n";
        for (const auto& r : res.rows)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                out += csvField(cellText(res, r, cols[i], d)) + (i + 1 < cols.size() ? L"," : L"");
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::HtmlHorizontal:
    {
        out += L"<html><head><meta http-equiv='content-type' content='text/html;charset=utf-8'>";
        out += L"<title>";
        out += kAppName;
        out += L"</title></head>\r\n<body>\r\n";
        out += L"<h3>";
        out += kAppName;
        out += L"</h3>\r\n";
        out += L"<br><h4>Created by using ";
        out += kAppName;
        out += L"</h4><p>\r\n";
        out += L"<table border=\"1\" cellpadding=\"5\"><tr>";
        for (int c : cols)
        {
            out += L"<th>" + htmlEscape(res.columns[c].name) + L"</th>";
        }
        out += L"</tr>\r\n";
        for (const auto& r : res.rows)
        {
            out += L"<tr>";
            for (int c : cols)
            {
                out += L"<td nowrap>" + htmlEscape(cellText(res, r, c, d)) + L"</td>";
            }
            out += L"</tr>\r\n";
        }
        out += L"</table>\r\n</body></html>\r\n";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        out += L"<html><head><meta http-equiv='content-type' content='text/html;charset=utf-8'>";
        out += L"<title>";
        out += kAppName;
        out += L"</title></head>\r\n<body>\r\n";
        out += L"<h3>";
        out += kAppName;
        out += L"</h3>\r\n";
        out += L"<br><h4>Created by using ";
        out += kAppName;
        out += L"</h4><p>\r\n";
        for (const auto& r : res.rows)
        {
            out += L"<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : cols)
            {
                out += L"<tr><td nowrap><b>" + htmlEscape(res.columns[c].name) + L"</b></td><td>" +
                       htmlEscape(cellText(res, r, c, d)) + L"</td></tr>\r\n";
            }
            out += L"</table><br>\r\n";
        }
        out += L"</body></html>\r\n";
        break;
    }
    case ExportFormat::Xml:
    {
        out += L"<?xml version=\"1.0\" encoding=\"ISO-8859-1\" ?>\r\n<simple_wmi_view>\r\n";
        for (const auto& r : res.rows)
        {
            out += L"<item>\r\n";
            for (int c : cols)
            {
                std::wstring tag = slug(res.columns[c].name);
                out += L"<" + tag + L">" + htmlEscape(cellText(res, r, c, d)) + L"</" + tag + L">\r\n";
            }
            out += L"</item>\r\n";
        }
        out += L"</simple_wmi_view>\r\n";
        break;
    }
    case ExportFormat::Json:
    {
        out += L"[\r\n";
        for (size_t ri = 0; ri < res.rows.size(); ++ri)
        {
            out += L"{\r\n";
            for (size_t i = 0; i < cols.size(); ++i)
            {
                std::wstring v = cellText(res, res.rows[ri], cols[i], d);
                std::wstring esc;
                for (wchar_t ch : v)
                {
                    if (ch == L'"' || ch == L'\\')
                    {
                        esc += L'\\';
                    }
                    esc += ch;
                }
                out += L"\t\"" + res.columns[cols[i]].name + L"\": \"" + esc + L"\"" +
                       (i + 1 < cols.size() ? L",\r\n" : L"\r\n");
            }
            out += (ri + 1 < res.rows.size()) ? L"},\r\n" : L"}\r\n";
        }
        out += L"]\r\n";
        break;
    }
    }
    return out;
}

bool WriteReport(const std::wstring& path, const WmiResult& res, ExportFormat format, const ExportOptions& opt)
{
    std::wstring body = BuildReport(res, format, opt);

    // Encoding. The original's Default is UTF-16LE with a BOM for every format
    // except the comma-delimited file, which defaults to ANSI with no BOM.
    // ANSI and UTF-8 are byte streams, Unicode is UTF-16LE with a BOM.
    int enc = opt.encoding;
    bool utf16;
    if (enc == Enc_Default)
    {
        utf16 = (format != ExportFormat::Csv);
    }
    else
    {
        utf16 = (enc == Enc_Unicode);
    }
    if (enc == Enc_Default && format == ExportFormat::Csv)
    {
        enc = Enc_Ansi;
    }

    std::string bytes;
    if (utf16)
    {
        bytes.append("\xFF\xFE");
        bytes.append(reinterpret_cast<const char*>(body.data()), body.size() * sizeof(wchar_t));
    }
    else if (enc == Enc_Utf8)
    {
        bytes = toUtf8(body, true);
    }
    else // ANSI
    {
        bytes = toAnsi(body);
    }

    if (path.empty())
    {
        // Send to stdout (binary), as the original does with an empty filename.
        int fd = _fileno(stdout);
        _setmode(fd, _O_BINARY);
        return fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size();
    }

    std::ofstream f(path, std::ios::binary);
    if (!f)
    {
        return false;
    }
    f.write(bytes.data(), (std::streamsize)bytes.size());
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (_wcsicmp(sw.c_str(), L"/stext") == 0)
    {
        out = ExportFormat::Text;
        return true;
    }
    if (_wcsicmp(sw.c_str(), L"/stab") == 0)
    {
        out = ExportFormat::Tab;
        return true;
    }
    if (_wcsicmp(sw.c_str(), L"/stabular") == 0)
    {
        out = ExportFormat::Tabular;
        return true;
    }
    if (_wcsicmp(sw.c_str(), L"/scomma") == 0)
    {
        out = ExportFormat::Csv;
        return true;
    }
    if (_wcsicmp(sw.c_str(), L"/shtml") == 0)
    {
        out = ExportFormat::HtmlHorizontal;
        return true;
    }
    if (_wcsicmp(sw.c_str(), L"/sverhtml") == 0)
    {
        out = ExportFormat::HtmlVertical;
        return true;
    }
    if (_wcsicmp(sw.c_str(), L"/sxml") == 0)
    {
        out = ExportFormat::Xml;
        return true;
    }
    if (_wcsicmp(sw.c_str(), L"/sjson") == 0)
    {
        out = ExportFormat::Json;
        return true;
    }
    return false;
}
