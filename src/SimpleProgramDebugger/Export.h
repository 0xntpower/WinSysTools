// Export.h -- save-report builders for the SimpleProgramDebugger export formats.
#pragma once
#include "DebugEvent.h"
#include <vector>
#include <string>

enum class ExportFormat
{
    Text,            // /stext
    Tab,             // /stab
    Tabular,         // /stabular
    HtmlHorizontal,  // /shtml
    HtmlVertical,    // /sverhtml
    Xml,             // /sxml
    Csv,             // /scomma
};

struct ExportOptions
{
    bool addHeaderLine = true;   // AddExportHeaderLine, CSV and tab-delimited
};

std::wstring BuildReport(const std::vector<DbgRow>& items,
                         const std::vector<int>& columns,
                         ExportFormat format,
                         const ExportOptions& opt);

bool WriteReport(const std::wstring& path,
                 const std::vector<DbgRow>& items,
                 const std::vector<int>& columns,
                 ExportFormat format,
                 const ExportOptions& opt);

// Map a /s* switch to a format. Returns false if the switch is not an export.
bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);

// Map a save-dialog filter index (1-based) to a format.
ExportFormat FormatFromFilterIndex(int idx);
