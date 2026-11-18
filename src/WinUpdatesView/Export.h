// Export.h -- save-report builders for the WinUpdatesView export formats.
#pragma once
#include "UpdateRecord.h"
#include <vector>
#include <string>

enum class ExportFormat
{
    Text,            // /stext
    Tab,             // /stab
    Csv,             // /scomma
    HtmlHorizontal,  // /shtml
    HtmlVertical,    // /sverhtml
    Xml,             // /sxml
    Json,            // /sjson
};

struct ExportOptions
{
    bool addHeaderLine = true;   // AddExportHeaderLine (CSV / tab-delimited)
    DisplayOptions display;      // ShowTimeInGMT etc.
};

// Build the report body as UTF-16 text.
std::wstring BuildReport(const std::vector<UpdateRecord>& items,
                         const std::vector<int>& columns,
                         ExportFormat format,
                         const ExportOptions& opt);

// Write the report to a UTF-8 (BOM-less) file. Returns false on I/O failure.
bool WriteReport(const std::wstring& path,
                 const std::vector<UpdateRecord>& items,
                 const std::vector<int>& columns,
                 ExportFormat format,
                 const ExportOptions& opt);

// Map a /s* switch to a format. Returns false if the switch is not an export.
bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);
