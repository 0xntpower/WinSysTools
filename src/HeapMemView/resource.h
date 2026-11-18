// resource.h -- resource identifiers for the HeapMemView recreation.
// IDs mirror the original binary so the extracted res/HeapMemView.rc binds unchanged.
#pragma once

// ---- Icons / bitmaps / cursors / menus / accelerators (original numeric IDs) ----
#define IDI_APP 101          // main window + class icon (0x65)
#define IDI_ITEM 102         // list item image icon (0x66)
#define IDR_MAIN_MENU 102    // main menu (0x66)
#define IDR_ACCEL 103        // accelerator table (0x67)
#define IDB_TOOLBAR 104      // toolbar bitmap, 8 images 128x16 (0x68)
#define IDR_CONTEXT_MENU 104 // right-click menu (0x68)
#define IDI_SMALL 110
#define IDC_SPLIT_CURSOR 111 // vertical splitter cursor (0x6F)
#define IDB_SORT_UP 133
#define IDB_SORT_DOWN 134

// ---- Dialog templates ----
#define IDD_PROPERTIES 105
#define IDD_EXCEPTION 107
#define IDD_SELECT_PROCESS 109
#define IDD_FILTER 111
#define IDD_ABOUT 112
#define IDD_ADVANCED 113
#define IDD_COLUMNS 1096

// ---- Child control IDs for the main window (match the original WM_CREATE ids) ----
#define IDC_STATUSBAR 0x101 // 257
#define IDC_TOOLBAR 0x102   // 258
#define IDC_LISTVIEW 0x103  // 259
#define IDC_HEXEDIT 0x104   // 260

// ---- Dialog control IDs (from the extracted templates) ----
// Properties (105)
#define IDC_PROP_EDIT 1000
#define IDC_PROP_LABEL 1001
// Select Process (109)
#define IDC_PROC_LIST 1005
// Filter (111)
#define IDC_FILTER_USEFROM 1006
#define IDC_FILTER_FROM 1007
#define IDC_FILTER_USETO 1009
#define IDC_FILTER_TO 1010
// About (112)
#define IDC_ABOUT_TEXT 1002
#define IDC_ABOUT_SUB 1004
#define IDC_ABOUT_TRANS 1006
// Advanced Options (113)
#define IDC_ADV_BYTES 1012     // number of bytes to display
#define IDC_ADV_STARTBYTE 1013 // start with byte number
#define IDC_ADV_PERLINE 1016   // bytes per line
#define IDC_ADV_SHOWADDR 1017  // display memory address for each line
// Column Settings (1096)
#define IDC_COL_LIST 1001
#define IDC_COL_MOVEUP 1003
#define IDC_COL_MOVEDOWN 1004
#define IDC_COL_WIDTH 1005
#define IDC_COL_SHOW 1006
#define IDC_COL_HIDE 1007
#define IDC_COL_DEFAULT 1008

// ---- Menu / toolbar command IDs (original values) ----
// Several names also live in WTL's atlres.h. Undefine them first so our original
// numeric values take effect in both the C++ code and the message map.
#undef ID_FILE_SAVE
#undef ID_FILE_EXIT
#undef ID_APP_EXIT
#undef ID_APP_ABOUT
#undef ID_VIEW_REFRESH
#undef ID_EDIT_SELECT_ALL
#undef ID_EDIT_COPY
#undef ID_EDIT_FIND
#define ID_FILE_SAVE 40001          // Save Selected Items (Ctrl+S)
#define ID_FILE_EXIT 40002          // Exit
#define ID_HELP_ABOUT 40003         // About
#define ID_VIEW_REFRESH 40004       // Refresh (F5)
#define ID_VIEW_GRIDLINES 40005     // Show Grid Lines
#define ID_VIEW_HTML_ALL 40006      // HTML Report - All Items
#define ID_EDIT_SELECTALL 40007     // Select All (Ctrl+A)
#define ID_EDIT_COPY 40008          // Copy Selected Items (Ctrl+C)
#define ID_FILE_PROPERTIES 40009    // Properties (Alt+Enter)
#define ID_VIEW_HTML_SEL 40010      // HTML Report - Selected Items
#define ID_VIEW_COLUMNS 40011       // Choose Columns
#define ID_VIEW_AUTOSIZE 40012      // Auto Size Columns (Ctrl+Plus)
#define ID_EDIT_FIND 40014          // Find (Ctrl+F)
#define ID_EDIT_DESELECTALL 40015   // Deselect All (Ctrl+D)
#define ID_OPT_DISPLAY_HEADER 40022 // Display Memory Block Header
#define ID_FILE_SELECTPROC 40023    // Select Process (Ctrl+P)
#define ID_FILE_EXPORTDATA 40027    // Export Memory Data To File (Ctrl+E)
#define ID_OPT_MARK_NEW 40030       // Mark New Memory Blocks In Color
#define ID_OPT_CLEAR_NEW 40031      // Clear New Memory Blocks Mark (F6)
#define ID_OPT_FILTER 40032         // Memory Blocks Filter (F8)
#define ID_OPT_ABOVE127 40033       // Display Characters Above ASCII 127
#define ID_PREVIEW_NONE 40035       // Data Preview Mode -> None
#define ID_PREVIEW_ASCII 40036      // Data Preview Mode -> ASCII
#define ID_PREVIEW_HEX 40037        // Data Preview Mode -> Hexadecimal
#define ID_OPT_ADVANCED 40038       // Advanced Options
#define ID_OPT_AUTOSORT 40040       // Automatically Sort On Refresh
