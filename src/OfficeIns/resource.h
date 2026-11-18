// resource.h -- resource identifiers for the OfficeIns recreation.
// IDs mirror the original binary so the extracted res/OfficeIns.rc binds unchanged.
#pragma once

// ---- Icons / bitmaps / cursor / menus / accelerators (original numeric IDs) ----
#define IDI_APP              101   // main window + class icon
#define IDI_SMALL            102   // secondary icon (also the list item image)
#define IDB_TOOLBAR          104   // toolbar bitmap (112x16, 7 buttons)
#define IDR_MAIN_MENU        102
#define IDR_ACCEL            103
#define IDR_CONTEXT_MENU     104
#define IDC_HAND_CURSOR      103

// ---- Dialog templates ----
#define IDD_PROPERTIES       105
#define IDD_EXCEPTION        107
#define IDD_ABOUT            112
#define IDD_COLUMNS          1096

// ---- Child control IDs (chosen for the recreation's own WM_CREATE controls) ----
#define IDC_STATUSBAR        0x101  // 257
#define IDC_TOOLBAR          0x102  // 258
#define IDC_LISTVIEW         0x103  // 259

// ---- Dialog control IDs (from the extracted templates) ----
#define IDC_PROP_LABEL       1001
#define IDC_PROP_EDIT        1000
#define IDC_ABOUT_TEXT       1002
#define IDC_ABOUT_SUB        1004
#define IDC_ABOUT_TRANS      1006
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008
#define IDC_COL_WIDTH        1005

// ---- Menu / toolbar command IDs (original values) ----
// Several names also live in WTL's atlres.h as standard command IDs. Undefine
// them first so our original numeric values take effect in both C++ and the map.
#undef ID_FILE_SAVE
#undef ID_FILE_EXIT
#undef ID_APP_EXIT
#undef ID_APP_ABOUT
#undef ID_VIEW_REFRESH
#undef ID_EDIT_SELECT_ALL
#undef ID_EDIT_COPY
#undef ID_EDIT_FIND
#define ID_FILE_SAVE         40001   // Save Selected Items
#define ID_FILE_EXIT         40002   // Exit
#define ID_HELP_ABOUT        40003   // About
#define ID_VIEW_REFRESH      40004   // Refresh (F5)
#define ID_VIEW_GRIDLINES    40005   // Show Grid Lines
#define ID_VIEW_HTML_ALL     40006   // HTML Report - All Items
#define ID_EDIT_SELECTALL    40007   // Select All (Ctrl+A)
#define ID_EDIT_COPY         40008   // Copy Selected Items (Ctrl+C)
#define ID_FILE_PROPERTIES   40009   // Properties (Alt+Enter)
#define ID_VIEW_HTML_SEL     40010   // HTML Report - Selected Items
#define ID_VIEW_COLUMNS      40011   // Choose Columns
#define ID_VIEW_AUTOSIZE     40012   // Auto Size Columns (Ctrl+Plus)
#define ID_EDIT_FIND         40014   // Find (Ctrl+F)
#define ID_EDIT_DESELECTALL  40015   // Deselect All (Ctrl+D)
#define ID_FILE_FILEPROPS    40022   // File Properties
#define ID_FILE_ADDIN_REGEDIT 40023  // Open AddIn In RegEdit (F7)
#define ID_FILE_CLSID_REGEDIT 40025  // Open CLSID In RegEdit (F8)

// ---- "Change Start Mode" submenu (original 42000..42003) ----
#define ID_MODE_DISABLED     42000
#define ID_MODE_STARTUP      42001
#define ID_MODE_DEMAND       42002
#define ID_MODE_FIRSTTIME    42003
// ---- "Change Connected Mode" submenu (original 42100..42101) ----
#define ID_CONNECTED_NO      42100
#define ID_CONNECTED_YES     42101
