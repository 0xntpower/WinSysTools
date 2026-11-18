// Export.h -- the Save Packet Summaries formats and the /s* command-line switches.
// The format set and filter order come from STRINGTABLE 501..507. The CLI switches were
// recovered from the command-line handler, which accepts /stext /stab /stabular /shtml
// /sverhtml /sxml (this build has no /scomma, though the Save dialog still offers comma).
// The output is single-byte text with CRLF line ends and no byte-order mark, the way the
// original ANSI build writes it. The XML declaration carries encoding="ISO-8859-1".
#pragma once
#include "Columns.h"
#include <string>
#include <vector>
#include <functional>

enum class ExportFormat
{
    Text,           // /stext      filter 1
    Tab,            // /stab       filter 2
    Tabular,        // /stabular   filter 3
    HtmlHorizontal, // /shtml      filter 4
    HtmlVertical,   // /sverhtml   filter 5
    Xml,            // /sxml       filter 6
    Comma,          //             filter 7 (Save dialog only)
};

struct ExportRequest
{
    int rowCount = 0;
    const ColumnDef* colDefs = nullptr;
    std::vector<int> columns; // visible column indices, in order
    std::function<std::wstring(int row, int col)> cell;
    const wchar_t* reportTitle = L"Packets Summary"; // STRINGTABLE 8
    bool addHeaderLine = false;                      // AddExportHeaderLine, tab and comma
};

// Build the report as single-byte (ANSI) bytes, matching the original's encoding.
std::string BuildReportA(const ExportRequest& req, ExportFormat format);

// Write the report to a file with no byte-order mark. An empty path writes to stdout.
bool WriteReport(const std::wstring& path, const ExportRequest& req, ExportFormat format);

// Map a /s* switch to a format. Returns false for anything else.
bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);

// Save dialog filter index (1-based, STRINGTABLE 501..507 order) to a format.
ExportFormat FormatFromFilterIndex(int index1);
