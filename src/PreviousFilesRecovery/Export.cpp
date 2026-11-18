// Export.cpp -- report builders for the save formats, matching the original's output
// byte for byte (verified against the original's own /scomma, /stab, /stabular, /sxml
// and /shtml output). The comma report is UTF-8 with a BOM, every other report is
// UTF-16LE with a BOM, unless SaveFileEncoding forces ANSI, UTF-16 or UTF-8.
//
// Verified structure:
//  - /scomma  header row of titles, fields joined by "," (no trailing comma)
//  - /stab    header row of titles, each field followed by a tab (trailing tab)
//  - /stabular  no header, fixed-width columns
//  - /stext   title: value blocks, one blank line between records
//  - /sxml    <?xml version="1.0" ?> / <files_list> / per-item <tag>value</tag>
//  - /shtml   HTML 3.2 bordered table, <th>Title header cells
#include "Export.h"
#include <windows.h>
#include <fstream>
#include <algorithm>

namespace
{
    const wchar_t* kHomePage = L"https://github.com/0xntpower/WinSysTools";
    const wchar_t* kProduct = L"PreviousFilesRecovery";

    std::wstring xmlEscape(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s)
            switch (c)
            {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;";  break;
            case L'>': out += L"&gt;";  break;
            default:   out += c;        break;
            }
        return out;
    }

    // The XML element names are the lowercased title with spaces turned into
    // underscores, so "Item Type" becomes "item_type".
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

    std::string toMulti(const std::wstring& w, UINT cp)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }
}

std::wstring BuildReport(const ExportRequest& req, ExportFormat format)
{
    std::wstring out;
    const auto& cols = req.columns;
    auto title = [&](int c) { return std::wstring(kColumns[c].title); };
    auto cell = [&](int r, int c) { return req.cell(r, c); };

    switch (format)
    {
    case ExportFormat::Text:
    {
        // One record per block, each field "title: value" with the title padded to 18.
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (int c : cols)
            {
                std::wstring t = title(c);
                if (t.size() < 18) t.resize(18, L' ');
                out += t + L": " + cell(r, c) + L"\r\n";
            }
            out += L"\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        // Header plus rows, every field followed by a tab (trailing tab included).
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
        // Header plus rows, fields joined by a comma (no trailing comma).
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
        // Fixed per-column character widths from the pixel widths, truncated then padded.
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (int c : cols)
            {
                int w = std::max(10, kColumns[c].width / 7);
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
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>"; out += req.reportTitle; out += L"</title></head>\r\n";
        out += L"<body>\r\n <h3>"; out += req.reportTitle; out += L"</h3>\r\n";
        out += L"<br><h4>Created by using <a href=\""; out += kHomePage;
        out += L"\" target=\"newwin\">"; out += kProduct; out += L"</a></h4><p>";
        out += L"<table border=\"1\" cellpadding=\"5\"><tr bgcolor=\"E0E0E0\">\r\n";
        for (int c : cols) out += L"<th>" + xmlEscape(title(c)) + L"\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"<tr>\r\n";
            for (int c : cols) out += L"<td bgcolor=#FFFFFF nowrap>" + xmlEscape(cell(r, c)) + L"\r\n";
        }
        out += L"</table>\r\n\r\n</body></html>";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>"; out += req.reportTitle; out += L"</title></head>\r\n";
        out += L"<body>\r\n <h3>"; out += req.reportTitle; out += L"</h3>\r\n";
        out += L"<br><h4>Created by using <a href=\""; out += kHomePage;
        out += L"\" target=\"newwin\">"; out += kProduct; out += L"</a></h4><p>\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : cols)
                out += L"<tr><td nowrap><b>" + xmlEscape(title(c)) + L"</b><td bgcolor=#FFFFFF nowrap>" +
                       xmlEscape(cell(r, c)) + L"\r\n";
            out += L"</table><br>\r\n";
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
                out += L"<" + tag + L">" + xmlEscape(cell(r, c)) + L"</" + tag + L">\r\n";
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

    int enc = req.encoding;
    if (enc == 0) enc = (format == ExportFormat::Comma) ? 3 : 2;

    // An empty filename ("") sends the output to stdout.
    if (path.empty())
    {
        std::string a = toMulti(body, CP_ACP);
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), a.data(), (DWORD)a.size(), &written, nullptr);
        return true;
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    if (enc == 2)
    {
        static const unsigned char bom[2] = { 0xFF, 0xFE };
        f.write(reinterpret_cast<const char*>(bom), 2);
        f.write(reinterpret_cast<const char*>(body.data()),
                static_cast<std::streamsize>(body.size() * sizeof(wchar_t)));
    }
    else if (enc == 3)
    {
        static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        f.write(reinterpret_cast<const char*>(bom), 3);
        std::string u8 = toMulti(body, CP_UTF8);
        f.write(u8.data(), static_cast<std::streamsize>(u8.size()));
    }
    else
    {
        std::string a = toMulti(body, CP_ACP);
        f.write(a.data(), static_cast<std::streamsize>(a.size()));
    }
    return f.good();
}

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out)
{
    if (_wcsicmp(sw.c_str(), L"/stext")    == 0) { out = ExportFormat::Text;           return true; }
    if (_wcsicmp(sw.c_str(), L"/stab")     == 0) { out = ExportFormat::Tab;            return true; }
    if (_wcsicmp(sw.c_str(), L"/stabular") == 0) { out = ExportFormat::Tabular;        return true; }
    if (_wcsicmp(sw.c_str(), L"/scomma")   == 0) { out = ExportFormat::Comma;          return true; }
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
