// Export.h -- the save formats behind File > Save, the HTML Report, and the /s* headless
// switches. Format and filter order come from STRINGTABLE 501..507. Generic over a row
// provider so the list and a selection export through the same builder. The original is
// an ANSI build and writes single byte ANSI files with no byte order mark, so this does
// the same. The Tab and Comma formats carry a header line only when Add Header Line is on.
#pragma once
#include "Columns.h"
#include <string>
#include <vector>
#include <functional>

enum class ExportFormat
{
    Text,            // /stext
    Tab,             // /stab
    Tabular,         // /stabular
    HtmlHorizontal,  // /shtml
    HtmlVertical,    // /sverhtml
    Xml,             // /sxml
    Comma,           // /scomma
};

struct ExportRequest
{
    int rowCount = 0;
    const ColumnDef* colDefs = kColumns;
    std::vector<int> columns;                            // visible column indices, in order
    std::function<std::wstring(int row, int col)> cell;
    const wchar_t* reportTitle = L"Sniffed Passwords";   // STRINGTABLE 8
    bool addHeaderLine = false;                           // Add Header Line To CSV/Tab-Delimited File
};

// Build the report as an ANSI byte string, matching the original's templates.
std::string BuildReport(const ExportRequest& req, ExportFormat format);
bool WriteReport(const std::wstring& path, const ExportRequest& req, ExportFormat format);

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);
ExportFormat FormatFromFilterIndex(int index1);
