// Export.h -- the save formats behind File>Save and the /s* command-line switches.
// Formats recovered from the STRINGTABLE (501-506) and the CLI handler sub_14000B8EC
// (/stext /stab /stabular /shtml /sverhtml /sxml).
#pragma once
#include "Addin.h"
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

std::wstring BuildReport(const std::vector<Addin>& items,
                         const std::vector<int>& columns,
                         ExportFormat format);

bool WriteReport(const std::wstring& path,
                 const std::vector<Addin>& items,
                 const std::vector<int>& columns,
                 ExportFormat format);

// Map a /s* switch to a format. Returns false for anything else.
[[nodiscard]] bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);
