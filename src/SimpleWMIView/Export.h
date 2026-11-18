// Export.h -- report builders for the SimpleWMIView save formats.
#pragma once
#include "WmiResult.h"
#include "Config.h"
#include <vector>
#include <string>

enum class ExportFormat
{
    Text,            // /stext
    Tab,             // /stab
    Tabular,         // /stabular
    Csv,             // /scomma
    HtmlHorizontal,  // /shtml
    HtmlVertical,    // /sverhtml
    Xml,             // /sxml
    Json,            // /sjson
};

struct ExportOptions
{
    DisplayOptions display;              // Show Time In GMT, milliseconds
    int encoding = Enc_Default;          // SaveFileEncoding
    std::vector<int> columns;            // visible columns, display order (empty = all)
};

// Build the report body as UTF-16 text.
std::wstring BuildReport(const WmiResult& res, ExportFormat format, const ExportOptions& opt);

// Write the report to a file, honouring the encoding. Empty path sends it to
// stdout. Returns false on I/O failure.
bool WriteReport(const std::wstring& path, const WmiResult& res,
                 ExportFormat format, const ExportOptions& opt);

// Map a /s* switch to a format. Returns false if the switch is not an export.
bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);
