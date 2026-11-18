// HexDump.h -- byte formatting for the Data Preview column (sub_140002728) and the
// lower pane hex view that fills the read-only Courier New edit control.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>

enum class PreviewMode
{
    None = 0,
    Ascii = 1,
    Hex = 2
};

// One-line Data Preview. ASCII mode renders each byte as its character, control
// bytes and (unless above127 is set) high bytes as '.'. Hex mode renders "%2.2X ".
inline std::wstring BuildPreview(const std::vector<BYTE>& data, PreviewMode mode, bool above127)
{
    std::wstring out;
    if (mode == PreviewMode::Hex)
    {
        wchar_t b[8];
        for (BYTE c : data)
        {
            swprintf(b, 8, L"%02X ", c);
            out += b;
        }
    }
    else if (mode == PreviewMode::Ascii)
    {
        for (BYTE c : data)
        {
            wchar_t ch = L'.';
            if (c >= 0x20 && (c < 0x80 || above127))
            {
                ch = static_cast<wchar_t>(c);
            }
            out += ch;
        }
    }
    return out;
}

// Multi-line hex view for the lower pane. Each line is an optional address, the hex
// bytes, then the ASCII gutter, bytesPerLine bytes per line.
inline std::wstring BuildHexDump(const std::vector<BYTE>& data, uint64_t baseAddress, int bytesPerLine,
                                 bool showAddress, bool above127)
{
    if (bytesPerLine < 1)
    {
        bytesPerLine = 16;
    }
    std::wstring out;
    wchar_t tmp[32];

    for (size_t off = 0; off < data.size(); off += bytesPerLine)
    {
        if (showAddress)
        {
            swprintf(tmp, 32, L"%016llx  ", static_cast<unsigned long long>(baseAddress + off));
            out += tmp;
        }

        std::wstring ascii;
        for (int i = 0; i < bytesPerLine; ++i)
        {
            size_t idx = off + i;
            if (idx < data.size())
            {
                BYTE c = data[idx];
                swprintf(tmp, 32, L"%02X ", c);
                out += tmp;
                wchar_t ch = L'.';
                if (c >= 0x20 && (c < 0x80 || above127))
                {
                    ch = static_cast<wchar_t>(c);
                }
                ascii += ch;
            }
            else
            {
                out += L"   ";
            }
        }
        out += L' ';
        out += ascii;
        out += L"\r\n";
    }
    return out;
}
