// Export.h -- the save formats behind File>Save and the /s* command-line switches.
// Formats and filter order come from STRINGTABLE 501-507. CLI switches were recovered
// from the command-line handler. Generic over row providers so both the types pane and
// the actions pane export through the same builder.
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
    const ColumnDef* colDefs = nullptr;
    std::vector<int> columns;                        // visible column indices, in order
    std::function<std::wstring(int row, int col)> cell;
    const wchar_t* reportTitle = L"File Types List"; // STRINGTABLE 8
    bool headerLine = false;                          // AddExportHeaderLine
};

std::wstring BuildReport(const ExportRequest& req, ExportFormat format);
bool WriteReport(const std::wstring& path, const ExportRequest& req, ExportFormat format);

// Map a /s* switch to a format. Returns false for anything else.
bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);

// File>Save dialog filter index (1-based, STRINGTABLE 501-507 order) to a format.
ExportFormat FormatFromFilterIndex(int index1);
