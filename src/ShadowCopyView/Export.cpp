// Export.cpp -- report builders for the seven save formats, matching the original's
// structure and encoding. The text, tab, tabular, HTML, vertical HTML and XML reports
// are UTF-16LE with a BOM. The comma report is UTF-8 with a BOM. The tab and comma
// formats write a header line. The HTML output is the original's HTML 3.2 layout.
#include "Export.h"
#include <windows.h>
#include <algorithm>
#include <fstream>

namespace
{
    const wchar_t* kProduct = L"ShadowCopyView";
    const wchar_t* kHomePage = L"https://github.com/0xntpower/WinSysTools";

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

    // XML element names are the lowercased title with spaces turned into underscores,
    // for example "Snapshot Name" becomes "snapshot_name".
    std::wstring xmlTag(const std::wstring& title)
    {
        std::wstring t = title;
        std::transform(t.begin(), t.end(), t.begin(), towlower);
        std::replace(t.begin(), t.end(), L' ', L'_');
        return t;
    }

    std::wstring csvField(const std::wstring& s)
    {
        if (s.find_first_of(L",\"\r\n") == std::wstring::npos) return s;
        std::wstring out = L"\"";
        for (wchar_t c : s) { if (c == L'"') out += L'"'; out += c; }
        out += L'"';
        return out;
    }

    std::string toCodePage(const std::wstring& w, UINT cp)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    void htmlPreamble(std::wstring& out, const wchar_t* title)
    {
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>";
        out += title;
        out += L"</title></head>\r\n<body>\r\n <h3>";
        out += title;
        out += L"</h3>\r\n<br><h4>Created by using <a href=\"";
        out += kHomePage;
        out += L"\" target=\"newwin\">";
        out += kProduct;
        out += L"</a></h4><p>";
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
        const std::wstring sep(50, L'=');
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += sep + L"\r\n";
            for (int c : cols)
            {
                std::wstring t = title(c);
                t.resize(std::max<size_t>(t.size(), 18), L' ');
                out += t + L": " + cell(r, c) + L"\r\n";
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        for (int c : cols) out += title(c) + L"\t";
        out += L"\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (int c : cols) out += cell(r, c) + L"\t";
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Comma:
    {
        for (size_t i = 0; i < cols.size(); ++i) out += (i ? L"," : L"") + csvField(title(cols[i]));
        out += L"\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (size_t i = 0; i < cols.size(); ++i) out += (i ? L"," : L"") + csvField(cell(r, cols[i]));
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tabular:
    {
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (int c : cols)
            {
                int w = std::max(10, req.colDefs[c].width / 7);
                std::wstring v = cell(r, c);
                if ((int)v.size() > w) v.resize(w);
                v.resize(w, L' ');
                out += v + L" ";
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::HtmlHorizontal:
    {
        htmlPreamble(out, req.reportTitle);
        out += L"<table border=\"1\" cellpadding=\"5\"><tr bgcolor=\"E0E0E0\">\r\n";
        for (int c : cols) out += L"<th>" + htmlEscape(title(c)) + L"\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"<tr>";
            for (int c : cols)
            {
                std::wstring v = htmlEscape(cell(r, c));
                if (v.empty()) v = L"&nbsp;";
                out += L"<td bgcolor=#FFFFFF nowrap>" + v;
            }
            out += L"\r\n";
        }
        out += L"</table>\r\n\r\n</body></html>";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        htmlPreamble(out, req.reportTitle);
        out += L"\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : cols)
            {
                std::wstring v = htmlEscape(cell(r, c));
                if (v.empty()) v = L"&nbsp;";
                out += L"<tr><td nowrap><b>" + htmlEscape(title(c)) + L"</b><td bgcolor=#FFFFFF>" + v + L"\r\n";
            }
            out += L"</table><p>\r\n";
        }
        out += L"</body></html>";
        break;
    }
    case ExportFormat::Xml:
    {
        std::wstring root = xmlTag(req.reportTitle);
        out += L"<?xml version=\"1.0\" ?>\r\n<" + root + L">\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"<item>\r\n";
            for (int c : cols)
            {
                std::wstring tag = xmlTag(title(c));
                out += L"<" + tag + L">" + htmlEscape(cell(r, c)) + L"</" + tag + L">\r\n";
            }
            out += L"</item>\r\n";
        }
        out += L"</" + root + L">\r\n";
        break;
    }
    }
    return out;
}

bool WriteReport(const std::wstring& path, const ExportRequest& req, ExportFormat format)
{
    std::wstring body = BuildReport(req, format);

    if (path.empty())
    {
        UINT cp = (format == ExportFormat::Comma) ? CP_UTF8 : CP_ACP;
        std::string bytes = toCodePage(body, cp);
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(), (DWORD)bytes.size(), &written, nullptr);
        return true;
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    if (format == ExportFormat::Comma)
    {
        static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        f.write(reinterpret_cast<const char*>(bom), 3);
        std::string u8 = toCodePage(body, CP_UTF8);
        f.write(u8.data(), (std::streamsize)u8.size());
    }
    else
    {
        static const unsigned char bom[2] = { 0xFF, 0xFE };
        f.write(reinterpret_cast<const char*>(bom), 2);
        f.write(reinterpret_cast<const char*>(body.data()), (std::streamsize)(body.size() * sizeof(wchar_t)));
    }
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
