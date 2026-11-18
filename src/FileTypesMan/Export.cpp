// Export.cpp -- report builders for the seven save formats. FileTypesMan is a Unicode
// app that writes its exports in the system ANSI code page, so the output is encoded
// with CP_ACP and CRLF line endings for byte fidelity with the original.
#include "Export.h"
#include <windows.h>
#include <algorithm>
#include <fstream>

namespace
{
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
    // underscores, for example "File Type Group" becomes "file_type_group".
    std::wstring xmlTag(const std::wstring& title)
    {
        std::wstring t = title;
        std::transform(t.begin(), t.end(), t.begin(), towlower);
        std::replace(t.begin(), t.end(), L' ', L'_');
        return t;
    }

    // Quote a CSV field when it carries a comma, quote or newline (doubling quotes).
    std::wstring csvField(const std::wstring& s)
    {
        if (s.find_first_of(L",\"\r\n") == std::wstring::npos) return s;
        std::wstring out = L"\"";
        for (wchar_t c : s) { if (c == L'"') out += L'"'; out += c; }
        out += L'"';
        return out;
    }

    std::string toAnsi(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    void htmlHeader(std::wstring& out, const wchar_t* title)
    {
        out += L"<html>\r\n<head>\r\n<title>";
        out += title;
        out += L"</title>\r\n</head>\r\n<body>\r\n<h2>";
        out += title;
        out += L"</h2>\r\n";
    }
    void htmlFooter(std::wstring& out)
    {
        out += L"<p><br><h4>Created by using "
               L"<a href=\"https://github.com/0xntpower/WinSysTools\" target=\"newwin\">FileTypesMan</a></h4>\r\n";
        out += L"</body>\r\n</html>\r\n";
    }
}

std::wstring BuildReport(const ExportRequest& req, ExportFormat format)
{
    std::wstring out;
    const auto& cols = req.columns;
    auto title = [&](int c) { return std::wstring(req.colDefs[c].title); };
    auto cell = [&](int r, int c) { return req.cell(r, c); };

    switch (format)
    {
    case ExportFormat::Text:
    {
        // Each record framed by a separator line, titles left-padded to the widest.
        const std::wstring sep(60, L'=');
        size_t titleW = 0;
        for (int c : cols) titleW = std::max(titleW, title(c).size());
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += sep + L"\r\n";
            for (int c : cols)
            {
                std::wstring t = title(c);
                t.resize(std::max(t.size(), titleW), L' ');
                out += t + L": " + cell(r, c) + L"\r\n";
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        if (req.headerLine)
        {
            for (size_t i = 0; i < cols.size(); ++i) out += (i ? L"\t" : L"") + title(cols[i]);
            out += L"\r\n";
        }
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (size_t i = 0; i < cols.size(); ++i) out += (i ? L"\t" : L"") + cell(r, cols[i]);
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Comma:
    {
        if (req.headerLine)
        {
            for (size_t i = 0; i < cols.size(); ++i) out += (i ? L"," : L"") + csvField(title(cols[i]));
            out += L"\r\n";
        }
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (size_t i = 0; i < cols.size(); ++i) out += (i ? L"," : L"") + csvField(cell(r, cols[i]));
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tabular:
    {
        // Fixed per-column character widths derived from the pixel widths, each value
        // truncated to width-1 then padded. No header row.
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (int c : cols)
            {
                int w = std::max(10, req.colDefs[c].width / 7);
                std::wstring v = cell(r, c);
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
        htmlHeader(out, req.reportTitle);
        out += L"<table border=\"1\" cellpadding=\"5\"><tr bgcolor=\"#cccccc\">";
        for (int c : cols) out += L"<td><b>" + htmlEscape(title(c)) + L"</b></td>";
        out += L"</tr>\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"<tr>";
            for (int c : cols) out += L"<td nowrap>" + htmlEscape(cell(r, c)) + L"&nbsp;</td>";
            out += L"</tr>\r\n";
        }
        out += L"</table>\r\n";
        htmlFooter(out);
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        htmlHeader(out, req.reportTitle);
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : cols)
                out += L"<tr><td bgcolor=\"#cccccc\" nowrap><b>" + htmlEscape(title(c)) +
                       L"</b></td><td nowrap>" + htmlEscape(cell(r, c)) + L"&nbsp;</td></tr>\r\n";
            out += L"</table><br>\r\n";
        }
        htmlFooter(out);
        break;
    }
    case ExportFormat::Xml:
    {
        std::wstring root = xmlTag(req.reportTitle);
        out += L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<" + root + L">\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"\t<item>\r\n";
            for (int c : cols)
            {
                std::wstring tag = xmlTag(title(c));
                out += L"\t\t<" + tag + L">" + htmlEscape(cell(r, c)) + L"</" + tag + L">\r\n";
            }
            out += L"\t</item>\r\n";
        }
        out += L"</" + root + L">\r\n";
        break;
    }
    }
    return out;
}

bool WriteReport(const std::wstring& path, const ExportRequest& req, ExportFormat format)
{
    std::wstring report = BuildReport(req, format);
    // An empty filename ("") sends the output to stdout (readme v1.86).
    if (path.empty())
    {
        std::string ansi = toAnsi(report);
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), ansi.data(), (DWORD)ansi.size(), &written, nullptr);
        return true;
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << toAnsi(report);
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (_wcsicmp(sw.c_str(), L"/stext")    == 0) { out = ExportFormat::Text;           return true; }
    if (_wcsicmp(sw.c_str(), L"/stab")     == 0) { out = ExportFormat::Tab;            return true; }
    if (_wcsicmp(sw.c_str(), L"/scomma")   == 0) { out = ExportFormat::Comma;          return true; }
    if (_wcsicmp(sw.c_str(), L"/stabular") == 0) { out = ExportFormat::Tabular;        return true; }
    if (_wcsicmp(sw.c_str(), L"/shtml")    == 0) { out = ExportFormat::HtmlHorizontal; return true; }
    if (_wcsicmp(sw.c_str(), L"/sverhtml") == 0) { out = ExportFormat::HtmlVertical;   return true; }
    if (_wcsicmp(sw.c_str(), L"/sxml")     == 0) { out = ExportFormat::Xml;            return true; }
    return false;
}

ExportFormat FormatFromFilterIndex(int index1)
{
    switch (index1)
    {
    case 1:  return ExportFormat::Text;
    case 2:  return ExportFormat::Tab;
    case 3:  return ExportFormat::Tabular;
    case 4:  return ExportFormat::HtmlHorizontal;
    case 5:  return ExportFormat::HtmlVertical;
    case 6:  return ExportFormat::Xml;
    case 7:  return ExportFormat::Comma;
    default: return ExportFormat::Text;
    }
}
