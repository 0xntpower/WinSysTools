// Export.cpp -- the six report builders, reproducing the original's old-generation ANSI
// export layer. The HTML and XML format strings were taken directly from the binary
// (see analysis/smsniff.md). Output is single-byte text, CRLF line ends, no byte-order
// mark. The XML declaration is the ISO-8859-1 form the original emits.
#include "Export.h"
#include <windows.h>
#include <algorithm>
#include <fstream>

namespace
{
    const wchar_t* kProduct = L"SmartSniff";
    const wchar_t* kHomePage = L"http://www.nirsoft.net/";

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
            default:   out += c;         break;
            }
        }
        return out;
    }

    // Lowercase the title and turn spaces into underscores, so "Local Address" becomes
    // "local_address", matching the original's XML element naming.
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

    std::string toAnsi(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }
}

std::string BuildReportA(const ExportRequest& req, ExportFormat format)
{
    std::wstring out;
    const auto& cols = req.columns;
    auto title = [&](int c) { return std::wstring(req.colDefs[c].title); };
    auto cell = [&](int r, int c) { return req.cell(r, c); };

    switch (format)
    {
    case ExportFormat::Text:
    {
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += std::wstring(60, L'=') + L"\r\n";
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
        if (req.addHeaderLine)
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
        if (req.addHeaderLine)
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
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>";
        out += req.reportTitle;
        out += L"</title></head>\r\n<body>\r\n <h3>";
        out += req.reportTitle;
        out += L"</h3>\r\n<br><h4>Created by using <a href=\"";
        out += kHomePage;
        out += L"\" target=\"newwin\">";
        out += kProduct;
        out += L"</a></h4><p>";
        out += L"<table border=\"1\" cellpadding=\"5\"><tr>\r\n";
        for (int c : cols) out += L"<th>" + htmlEscape(title(c)) + L"\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (int c : cols)
            {
                std::wstring v = htmlEscape(cell(r, c));
                if (v.empty()) v = L"&nbsp;";
                out += L"<td bgcolor=#FFFFFF nowrap>" + v + L"\r\n";
            }
        }
        out += L"</table>\r\n\r\n</body></html>";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        out += L"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += L"<html><head><title>";
        out += req.reportTitle;
        out += L"</title></head>\r\n<body>\r\n <h3>";
        out += req.reportTitle;
        out += L"</h3>\r\n<br><h4>Created by using <a href=\"";
        out += kHomePage;
        out += L"\" target=\"newwin\">";
        out += kProduct;
        out += L"</a></h4><p>\r\n";
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
        out += L"<?xml version=\"1.0\"  encoding=\"ISO-8859-1\" ?>\r\n<" + root + L">\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += L"<item>\r\n";
            for (int c : cols)
            {
                std::wstring tag = xmlTag(title(c));
                out += L"\t<" + tag + L">" + htmlEscape(cell(r, c)) + L"</" + tag + L">\r\n";
            }
            out += L"</item>\r\n";
        }
        out += L"</" + root + L">\r\n";
        break;
    }
    }
    return toAnsi(out);
}

bool WriteReport(const std::wstring& path, const ExportRequest& req, ExportFormat format)
{
    std::string bytes = BuildReportA(req, format);

    if (path.empty())
    {
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(), (DWORD)bytes.size(), &written, nullptr);
        return true;
    }

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(bytes.data(), (std::streamsize)bytes.size());
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
