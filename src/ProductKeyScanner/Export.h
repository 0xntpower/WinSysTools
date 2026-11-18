// Export.h -- the seven save formats behind File>Save and the recreation's /s*
// command-line switches. Formats and order recovered from the STRINGTABLE
// (501 Text, 502 Tab, 503 JSON, 504 HTML-Horizontal, 505 HTML-Vertical, 506 XML,
// 507 Comma Delimited) and the save-dialog filter masks.
#pragma once
#include "ProductKey.h"
#include <string>
#include <vector>

enum class ExportFormat
{
    Text,            // /stext    "Title : Value" blocks
    Tab,             // /stab     tab-delimited
    Json,            // /sjson    JSON array
    HtmlHorizontal,  // /shtml    one table, one row per item
    HtmlVertical,    // /sverhtml one small table per item
    Xml,             // /sxml     one <item> per record
    Csv,             // /scomma   comma-delimited
};

struct ExportOptions
{
    bool addHeaderLine = false;  // "Add Header Line To CSV/Tab-Delimited File"
};

std::wstring BuildReport(const std::vector<ProductKeyRecord>& items,
                         const std::vector<int>& columns,
                         ExportFormat format,
                         const ExportOptions& opts = {});

bool WriteReport(const std::wstring& path,
                 const std::vector<ProductKeyRecord>& items,
                 const std::vector<int>& columns,
                 ExportFormat format,
                 const ExportOptions& opts = {});

bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);
