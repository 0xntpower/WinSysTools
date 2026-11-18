// Export.h -- the seven save formats behind File>Save and the /s* switches. Format
// order and filter text come from STRINGTABLE 501-507. Generic over a row provider
// so the GUI selection and the command-line export share one builder.
#pragma once
#include "Columns.h"
#include <string>
#include <vector>
#include <functional>

enum class ExportFormat
{
    Text,           // /stext     (code 1)
    Tab,            // /stab      (code 2)
    Tabular,        // /stabular  (code 3)
    Comma,          // /scomma    (code 4)
    HtmlHorizontal, // /shtml     (code 5)
    HtmlVertical,   // /sverhtml  (code 6)
    Xml,            // /sxml      (code 7)
};

struct ExportRequest
{
    int rowCount = 0;
    std::vector<int> columns; // visible column indices, in order
    std::function<std::wstring(int row, int col)> cell;
    const wchar_t* reportTitle = L"Files List"; // STRINGTABLE 8
    int encoding = 0;                           // 0 default, 1 ANSI, 2 UTF-16, 3 UTF-8
};

std::wstring BuildReport(const ExportRequest& req, ExportFormat format);
bool WriteReport(const std::wstring& path, const ExportRequest& req, ExportFormat format);

// Map a /s* switch to a format. Returns false for anything else.
bool FormatFromSwitch(const std::wstring& sw, ExportFormat& out);

// File>Save dialog filter index (1-based, STRINGTABLE 501-507 order) to a format.
ExportFormat FormatFromFilterIndex(int index1);
