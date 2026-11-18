// Export.cpp -- report builders for the seven save formats, matching the original's
// structure. The output is ANSI, the way the original ANSI build writes it. The text,
// tab, tabular, HTML, vertical HTML, XML and comma reports all go out as single byte
// text with no byte order mark. The HTML follows the original's HTML 3.2 layout and the
// XML declares ISO-8859-1, both read from the binary's own template strings.
#include "Export.h"
#include <windows.h>
#include <algorithm>
#include <fstream>

namespace
{
    const char* kProduct = "SniffPass";
    const char* kHomePage = "http://www.nirsoft.net/";

    std::string narrow(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        ::WideCharToMultiByte(CP_ACP, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    std::string htmlEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            default:  out += c;         break;
            }
        }
        return out;
    }

    std::string xmlTag(const std::string& title)
    {
        std::string t = title;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        std::replace(t.begin(), t.end(), ' ', '_');
        return t;
    }

    std::string csvField(const std::string& s)
    {
        if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
        std::string out = "\"";
        for (char c : s) { if (c == '"') out += '"'; out += c; }
        out += '"';
        return out;
    }

    void htmlPreamble(std::string& out, const std::string& title)
    {
        out += "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\r\n";
        out += "<html><head><title>";
        out += title;
        out += "</title></head>\r\n<body>\r\n <h3>";
        out += title;
        out += "</h3>\r\n<br><h4>Created by using <a href=\"";
        out += kHomePage;
        out += "\" target=\"newwin\">";
        out += kProduct;
        out += "</a></h4><p>";
    }
}

std::string BuildReport(const ExportRequest& req, ExportFormat format)
{
    std::string out;
    const auto& cols = req.columns;
    auto title = [&](int c) { return narrow(req.colDefs[c].title); };
    auto cell = [&](int r, int c) { return narrow(req.cell(r, c)); };
    std::string reportTitle = narrow(req.reportTitle);

    switch (format)
    {
    case ExportFormat::Text:
    {
        const std::string sep(50, '=');
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += sep + "\r\n";
            for (int c : cols)
            {
                std::string t = title(c);
                t.resize(std::max<size_t>(t.size(), 18), ' ');
                out += t + ": " + cell(r, c) + "\r\n";
            }
            out += "\r\n";
        }
        break;
    }
    case ExportFormat::Tab:
    {
        if (req.addHeaderLine)
        {
            for (int c : cols) out += title(c) + "\t";
            out += "\r\n";
        }
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (int c : cols) out += cell(r, c) + "\t";
            out += "\r\n";
        }
        break;
    }
    case ExportFormat::Comma:
    {
        if (req.addHeaderLine)
        {
            for (size_t i = 0; i < cols.size(); ++i) out += (i ? "," : "") + csvField(title(cols[i]));
            out += "\r\n";
        }
        for (int r = 0; r < req.rowCount; ++r)
        {
            for (size_t i = 0; i < cols.size(); ++i) out += (i ? "," : "") + csvField(cell(r, cols[i]));
            out += "\r\n";
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
                std::string v = cell(r, c);
                if ((int)v.size() > w) v.resize(w);
                v.resize(w, ' ');
                out += v + " ";
            }
            out += "\r\n";
        }
        break;
    }
    case ExportFormat::HtmlHorizontal:
    {
        htmlPreamble(out, reportTitle);
        out += "<table border=\"1\" cellpadding=\"5\"><tr bgcolor=\"E0E0E0\">\r\n";
        for (int c : cols) out += "<th>" + htmlEscape(title(c)) + "\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += "<tr>";
            for (int c : cols)
            {
                std::string v = htmlEscape(cell(r, c));
                if (v.empty()) v = "&nbsp;";
                out += "<td bgcolor=#FFFFFF nowrap>" + v;
            }
            out += "\r\n";
        }
        out += "</table>\r\n\r\n</body></html>";
        break;
    }
    case ExportFormat::HtmlVertical:
    {
        htmlPreamble(out, reportTitle);
        out += "\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += "<table border=\"1\" cellpadding=\"5\">\r\n";
            for (int c : cols)
            {
                std::string v = htmlEscape(cell(r, c));
                if (v.empty()) v = "&nbsp;";
                out += "<tr><td nowrap><b>" + htmlEscape(title(c)) + "</b><td bgcolor=#FFFFFF>" + v + "\r\n";
            }
            out += "</table><p>\r\n";
        }
        out += "</body></html>";
        break;
    }
    case ExportFormat::Xml:
    {
        std::string root = xmlTag(reportTitle);
        out += "<?xml version=\"1.0\"  encoding=\"ISO-8859-1\" ?>\r\n<" + root + ">\r\n";
        for (int r = 0; r < req.rowCount; ++r)
        {
            out += "<item>\r\n";
            for (int c : cols)
            {
                std::string tag = xmlTag(title(c));
                out += "<" + tag + ">" + htmlEscape(cell(r, c)) + "</" + tag + ">\r\n";
            }
            out += "</item>\r\n";
        }
        out += "</" + root + ">\r\n";
        break;
    }
    }
    return out;
}

bool WriteReport(const std::wstring& path, const ExportRequest& req, ExportFormat format)
{
    std::string body = BuildReport(req, format);
    if (path.empty()) return true;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(body.data(), (std::streamsize)body.size());
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
