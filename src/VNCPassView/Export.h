// Export.h -- the save formats behind File>Save and the /s* command-line switches.
// Formats and switches recovered from the STRINGTABLE (501-506) and the CLI strings
// (/stext /stab /stabular /shtml /sverhtml /sxml).
#pragma once
#include "VncPassword.h"
#include <string>
#include <vector>

enum class ExportFormat
{
    Text,           // /stext    -- "Title : Value" blocks
    Tab,            // /stab     -- tab-delimited
    Tabular,        // /stabular -- fixed-width columns
    HtmlHorizontal, // /shtml    -- one table, one row per item
    HtmlVertical,   // /sverhtml -- one small table per item
    Xml,            // /sxml     -- one <item> per record
};

std::wstring BuildReport(const std::vector<VncPassword>& items,
                         const std::vector<int>& columns,
                         ExportFormat format);

bool WriteReport(const std::wstring& path,
                 const std::vector<VncPassword>& items,
                 const std::vector<int>& columns,
                 ExportFormat format);

[[nodiscard]] bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);
