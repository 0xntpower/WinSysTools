// Export.h -- the list export formats offered by Save Selected Items.
// The filter order matches STRINGTABLE 501..507 in the original resources.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "HeapBlock.h"

enum class ExportFormat
{
    Text = 0,       // 501 Text File
    Tab,            // 502 Tab delimited text file
    Tabular,        // 503 Tabular Text File
    HtmlHorizontal, // 504 HTML File - Horizontal
    HtmlVertical,   // 505 HTML File - Vertical
    Xml,            // 506 XML File
    Comma           // 507 Comma delimited text file
};

std::wstring BuildReport(const std::vector<HeapBlock>& items, const std::vector<int>& cols, ExportFormat fmt);

void WriteReport(const std::wstring& path, const std::vector<HeapBlock>& items, const std::vector<int>& cols,
                 ExportFormat fmt);

// Map the 1-based save-dialog filter index to a format and vice versa.
bool FormatFromFilterIndex(int index, ExportFormat& out);
