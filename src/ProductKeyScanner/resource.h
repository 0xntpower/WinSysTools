// resource.h -- resource identifiers for the ProductKeyScanner recreation.
// IDs mirror the original binary so the extracted res/ProductKeyScanner.rc binds
// unchanged (menus 102/104/111, accel 103, dialogs 105/107/112/113/1096).
#pragma once

// ---- Icons / bitmaps / cursor / menus / accelerators (original numeric IDs) ----
#define IDI_APP              101   // main window + class icon (also about-box icon)
#define IDI_TYPE             102   // list item image
#define IDB_TOOLBAR          110   // toolbar bitmap
#define IDR_MAIN_MENU        102
#define IDR_ACCEL            103
#define IDR_CONTEXT_MENU     104
#define IDR_STOP_MENU        111
#define IDC_HAND_CURSOR      103

// ---- Dialog templates ----
#define IDD_PROPERTIES       105
#define IDD_EXCEPTION        107
#define IDD_ABOUT            112
#define IDD_SCANOPTIONS      113
#define IDD_COLUMNS          1096

// ---- Child control IDs (the recreation's own WM_CREATE controls) ----
#define IDC_STATUSBAR        0x101  // 257
#define IDC_TOOLBAR          0x102  // 258
#define IDC_LISTVIEW         0x103  // 259

// ---- Dialog control IDs (from the extracted templates) ----
#define IDC_PROP_LABEL       1001
#define IDC_PROP_EDIT        1000
#define IDC_ABOUT_TEXT       1002
#define IDC_ABOUT_LINK       1004
#define IDC_ABOUT_TRANS      1006
// Scan Options (113)
#define IDC_SCAN_SOURCE      1005   // combo: current system / external hive
#define IDC_SCAN_WMI         1011   // "Search product key with WMI"
#define IDC_SCAN_BIOS        1012   // "Search product key in BIOS"
#define IDC_SCAN_PATH        1008   // edit: software hive path
#define IDC_SCAN_AUTOFILL    1009   // "Automatic Fill"
#define IDC_SCAN_BROWSE      1010   // "Browse..."
// Column Settings (1096)
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008
#define IDC_COL_SELECTALL    1009
#define IDC_COL_DESELECTALL  1010
#define IDC_COL_WIDTH        1005

// ---- Menu / toolbar command IDs (original 40001..40041) ----
// Several names also live in WTL's atlres.h as standard command IDs. Undefine
// them first so the original numeric values take effect in both C++ and the map.
#undef ID_FILE_SAVE
#undef ID_FILE_OPEN
#undef ID_APP_EXIT
#undef ID_APP_ABOUT
#undef ID_VIEW_REFRESH
#undef ID_EDIT_SELECT_ALL
#undef ID_EDIT_COPY
#undef ID_EDIT_FIND

#define ID_FILE_SCANOPTIONS  40032   // Scan Options (F9)
#define ID_FILE_SAVE         40001   // Save Selected Items (Ctrl+S)
#define ID_FILE_PROPERTIES   40009   // Properties (Alt+Enter)
#define ID_FILE_EXIT         40002   // Exit
#define ID_EDIT_FIND         40014   // Find (Ctrl+F)
#define ID_EDIT_COPY         40008   // Copy Selected Items (Ctrl+C)
#define ID_EDIT_COPYKEY      40035   // Copy Product Key (Ctrl+K)
#define ID_EDIT_SELECTALL    40007   // Select All (Ctrl+A)
#define ID_EDIT_DESELECTALL  40015   // Deselect All (Ctrl+D)
#define ID_VIEW_GRIDLINES    40005   // Show Grid Lines
#define ID_VIEW_TOOLTIPS     40022   // Show Tooltips
#define ID_VIEW_MARKODDEVEN  40028   // Mark Odd/Even Rows
#define ID_VIEW_HTML_ALL     40006   // HTML Report - All Items
#define ID_VIEW_HTML_SEL     40010   // HTML Report - Selected Items
#define ID_VIEW_COLUMNS      40011   // Choose Columns
#define ID_VIEW_AUTOSIZE     40012   // Auto Size Columns (Ctrl+Plus)
#define ID_VIEW_AUTOSIZE_HDR 40024   // Auto Size Columns+Headers (Shift+Plus)
#define ID_VIEW_REFRESH      40004   // Refresh (F5)
#define ID_OPTIONS_ADDHEADER 40030   // Add Header Line To CSV/Tab-Delimited File
#define ID_OPTIONS_FONT      40040   // Select Another Font
#define ID_OPTIONS_DEFFONT   40041   // Use Default Font
#define ID_HELP_ABOUT        40003   // About
#define ID_STOP              40031   // Stop (during scan)
#define ID_SORTBY_BASE       40100   // dynamic "Sort By" submenu base (recreation)
