// Export.h -- the save formats behind File>Save and the /s* command-line switches.
#pragma once
#include "MimeType.h"
#include <string>
#include <vector>

enum class ExportFormat
{
    Text,           // /stext    -- "Title : Value" blocks
    Tab,            // /stab     -- tab-delimited with a header row
    Tabular,        // /stabular -- fixed-width columns
    HtmlHorizontal, // /shtml    -- one table, one row per item
    HtmlVertical,   // /sverhtml -- one small table per item
};

// Build the report text for the given items over the visible column indices.
std::wstring BuildReport(const std::vector<MimeType>& items,
                         const std::vector<int>& columns,
                         ExportFormat format);

// Write a report to a file as UTF-8. Returns false on I/O failure.
bool WriteReport(const std::wstring& path,
                 const std::vector<MimeType>& items,
                 const std::vector<int>& columns,
                 ExportFormat format);

// Map a command-line switch such as L"/shtml" to a format. Returns false if unknown.
[[nodiscard]] bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);
