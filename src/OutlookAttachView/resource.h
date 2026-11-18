// resource.h -- resource and command IDs for the OutlookAttachView recreation.
// The numeric values match the original binary's menu, accelerator, dialog,
// bitmap and string-table resources (recovered with Resource Hacker and IDA).
#pragma once

// ---- resources carried over from the original .rsrc --------------------
#define IDI_APP              101   // main / taskbar icon
#define IDI_SMALL            102   // small icon
#define IDR_MAIN_MENU        102
#define IDR_CONTEXT_MENU     104
#define IDR_STOP_MENU        109
#define IDR_TRAY_MENU        112
#define IDB_TOOLBAR          104   // 160x16 4-bit strip, ten 16x16 images
#define IDB_HDR_SORT_UP      133
#define IDB_HDR_SORT_DOWN    134
#define IDR_ACCEL            103

#define IDD_PROPERTIES       105
#define IDD_EXCEPTION        107
#define IDD_COPYFILES        110
#define IDD_SELECT_FOLDERS   111
#define IDD_ABOUT            112
#define IDD_QUICKFILTER      114
#define IDD_COLUMNS          1096
#define IDD_SCANOPTIONS      1100

// ---- control IDs we create at runtime ----------------------------------
#define IDC_LISTVIEW         1500
#define IDC_STATUSBAR        1501
#define IDC_TOOLBAR          0x102   // 258, matches CreateToolbarEx wID

// Properties dialog (resource 105)
#define IDC_PROP_LABEL       1001
#define IDC_PROP_VALUE       1000

// Column Settings dialog (resource 1096)
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_WIDTH        1005
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008
#define IDC_COL_DESELALL     1018
#define IDC_COL_SELALL       1019

// Copy Selected Files To dialog (resource 110)
#define IDC_CF_FOLDER        1005
#define IDC_CF_BROWSE        1006
#define IDC_CF_NEWNAME       1007
#define IDC_CF_DONTCOPY      1008
#define IDC_CF_FORMAT        1025
#define IDC_CF_CREATESUB     1038

// About dialog (resource 112)
#define IDC_ABOUT_VERSION    1002
#define IDC_ABOUT_COPYRIGHT  1004

// ---- command IDs (match the original menu/accelerator command values) --
#define ID_OAV_EXPORT        40001   // Export Attachments Information (Ctrl+S)
#define ID_OAV_EXIT          40002
#define ID_OAV_ABOUT         40003
#define ID_OAV_REFRESH       40004   // F5
#define ID_OAV_GRIDLINES     40005
#define ID_OAV_HTML_ALL      40006
#define ID_OAV_SELECTALL     40007   // Ctrl+A
#define ID_OAV_COPY          40008   // Ctrl+C
#define ID_OAV_PROPERTIES    40009   // Alt+Enter
#define ID_OAV_HTML_SEL      40010
#define ID_OAV_CHOOSECOLS    40011
#define ID_OAV_AUTOSIZE      40012   // Ctrl+Plus
#define ID_OAV_FIND          40014   // Ctrl+F
#define ID_OAV_DESELECTALL   40015   // Ctrl+D
#define ID_OAV_TOOLTIPS      40022
#define ID_OAV_SHOWINLINE    40023   // Ctrl+I
#define ID_OAV_SHOWTIMEGMT   40024   // Ctrl+G
#define ID_OAV_STOP          40026
#define ID_OAV_COPYTO        40027   // F4
#define ID_OAV_DELETE        40030   // Del
#define ID_OAV_CLEAR         40034   // Ctrl+Del
#define ID_OAV_OPENMSG       40037   // F9
#define ID_OAV_SCANOPTIONS   40038   // F8
#define ID_OAV_SHOWOPTBEFORE 40039
#define ID_OAV_ADDHEADER     40042
#define ID_OAV_OPENATTACH    40043   // F7
#define ID_OAV_OPENWITH      40050   // Ctrl+F7
#define ID_OAV_MARKODDEVEN   40053
#define ID_OAV_MARKINLINE    40054
#define ID_OAV_TRAYICON      40057
#define ID_OAV_OPENOAV       40059
#define ID_OAV_AUTOSIZE_HDR  40061   // Shift+Plus
#define ID_OAV_SELECTFONT    40066
#define ID_OAV_DEFAULTFONT   40067
#define ID_OAV_SAVECONFIG    40072
#define ID_OAV_LOADCONFIG    40073
#define ID_OAV_RECENT1       40079
#define ID_OAV_ALWAYSONTOP   40081
#define ID_OAV_QUICKFILTER   40087   // Ctrl+Q
#define ID_OAV_PREVIEWPANE   40092   // Ctrl+P
#define ID_OAV_PREVIEWCOPY   40093   // Ctrl+M
#define ID_OAV_COPYCELL      40096
#define ID_OAV_ALIGNNUMERIC  40097
#define ID_OAV_BLACKBG       40098

// Double-Click Action submenu (42100..42105)
#define ID_OAV_DCLICK_BASE   42100
// File Size Unit submenu (42201..42205)
#define ID_OAV_SIZEUNIT_BASE 42201
// Enter Key Action submenu (43100..43105)
#define ID_OAV_ENTER_BASE    43100
// Hash Selected Files submenu (44101..44104)
#define ID_OAV_HASH_MD5      44101
#define ID_OAV_HASH_SHA1     44102
#define ID_OAV_HASH_SHA256   44103
#define ID_OAV_HASH_SHA512   44104
// Save File Encoding submenu (45700..45703)
#define ID_OAV_ENC_DEFAULT   45700
#define ID_OAV_ENC_ANSI      45701
#define ID_OAV_ENC_UTF16     45702
#define ID_OAV_ENC_UTF8      45703

#define ID_OAV_SORTBY        49902   // toolbar Sort By dropdown

// ---- string-table bases (recovered) ------------------------------------
#define IDS_COLUMN_BASE      1000    // 1001..1022 column titles
#define IDS_ATTACHTYPE_BASE  100     // 101 By Value, 104 By Reference, 105 Embedded Item, 106 OLE
#define IDS_DIRECTION_BASE   1200    // 1201 Incoming, 1202 Outgoing
