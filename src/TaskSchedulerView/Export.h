// Export.h -- save-report builders for the TaskSchedulerView export formats.
#pragma once
#include "TaskRecord.h"
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
    Json,            // /sjson
};

struct ExportOptions
{
    bool addHeaderLine = true;   // AddExportHeaderLine (CSV / tab-delimited)
    int  encoding = 0;           // 0 Default, 1 ANSI, 2 UTF-16, 3 UTF-8
};

std::wstring BuildReport(const std::vector<TaskRecord>& items,
                         const std::vector<int>& columns,
                         ExportFormat format,
                         const ExportOptions& opt);

bool WriteReport(const std::wstring& path,
                 const std::vector<TaskRecord>& items,
                 const std::vector<int>& columns,
                 ExportFormat format,
                 const ExportOptions& opt);

// Map a /s* switch to a format. Returns false if the switch is not an export.
bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);
