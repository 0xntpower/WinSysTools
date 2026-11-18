// Columns.h -- the password list columns and the protocol names. Titles come from
// STRINGTABLE 1001..1009, protocol names from STRINGTABLE 1100..1105. The array index
// is the field index used throughout, matching the original. Default widths come from
// the original's Columns blob in SniffPass.cfg (60,60,100,100,80,80,80,80,130).
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <iterator>

enum ColKind { CK_TEXT, CK_NUMERIC };

struct ColumnDef
{
    const wchar_t* title;
    int            width;
    int            format;   // LVCFMT_*
    ColKind        kind;
};

inline constexpr ColumnDef kColumns[] = {
    { L"Index",          60, LVCFMT_LEFT,  CK_NUMERIC },  // 0
    { L"Protocol",       60, LVCFMT_LEFT,  CK_TEXT    },  // 1
    { L"Local Address", 100, LVCFMT_LEFT,  CK_TEXT    },  // 2
    { L"Remote Address",100, LVCFMT_LEFT,  CK_TEXT    },  // 3
    { L"Local Port",     80, LVCFMT_LEFT,  CK_NUMERIC },  // 4
    { L"Remote Port",    80, LVCFMT_LEFT,  CK_NUMERIC },  // 5
    { L"User",           80, LVCFMT_LEFT,  CK_TEXT    },  // 6
    { L"Password",       80, LVCFMT_LEFT,  CK_TEXT    },  // 7
    { L"Capture Time",  130, LVCFMT_LEFT,  CK_TEXT    },  // 8
};
inline constexpr int kColumnCount = static_cast<int>(std::size(kColumns));

// Protocol codes, matching the parser output and STRINGTABLE 1100..1105.
enum Protocol { PROTO_UNKNOWN = 0, PROTO_SMTP = 1, PROTO_POP3 = 2, PROTO_IMAP = 3, PROTO_FTP = 4, PROTO_HTTP = 5 };

inline const wchar_t* ProtocolName(int p)
{
    switch (p)
    {
    case PROTO_SMTP: return L"SMTP";
    case PROTO_POP3: return L"POP3";
    case PROTO_IMAP: return L"IMAP";
    case PROTO_FTP:  return L"FTP";
    case PROTO_HTTP: return L"HTTP";
    default:         return L"Unknown";
    }
}
