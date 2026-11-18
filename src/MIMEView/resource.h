// resource.h -- resource identifiers for the MIMEView recreation.
// IDs mirror the original binary so the extracted res/MIMEView.rc binds unchanged.
#pragma once

// ---- Icons / bitmaps / cursor / menus / accelerators (original numeric IDs) ----
#define IDI_APP              101   // main window + class icon
#define IDI_DEFAULT_TYPE     102   // fallback type icon (also the "102 MENU" id)
#define IDI_TYPE_A           113   // image-list icon 0
#define IDI_TYPE_B           203   // image-list icon 1
#define IDR_MAIN_MENU        102
#define IDR_ACCEL            103
#define IDR_CONTEXT_MENU     104
#define IDB_TOOLBAR          104   // toolbar bitmap (4 x 16x16)
#define IDC_HAND_CURSOR      103

// ---- Dialog templates ----
#define IDD_PROPERTIES       105
#define IDD_ABOUT            112
#define IDD_COLUMNS          1096

// ---- Child control IDs ----
#define IDC_STATUSBAR        0x101  // 257
#define IDC_TOOLBAR          0x102  // 258
#define IDC_LISTVIEW         0x103  // 259

// ---- Dialog control IDs (from the extracted templates) ----
#define IDC_PROP_LABEL       1001
#define IDC_PROP_EDIT        1000
#define IDC_ABOUT_TEXT       1002
#define IDC_ABOUT_LINK       1004
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008
#define IDC_COL_WIDTH        1005

// ---- Menu / toolbar command IDs (original 40001..40029) ----
// Several names also exist in WTL's atlres.h as standard command IDs. Undefine
// them first so our original numeric values take effect in both C++ and the map.
#undef ID_FILE_SAVE
#undef ID_FILE_EXIT
#undef ID_HELP_ABOUT
#undef ID_VIEW_REFRESH
#undef ID_EDIT_SELECTALL
#undef ID_EDIT_COPY
#undef ID_EDIT_FIND
#define ID_FILE_SAVE         40001
#define ID_FILE_EXIT         40002
#define ID_HELP_ABOUT        40003
#define ID_VIEW_REFRESH      40004
#define ID_FILE_ENABLE_DLG   40006
#define ID_EDIT_SELECTALL    40007
#define ID_EDIT_COPY         40008
#define ID_FILE_PROPERTIES   40009
#define ID_FILE_DISABLE_DLG  40010
#define ID_EDIT_FIND         40011
#define ID_VIEW_GRIDLINES    40012
#define ID_VIEW_COLUMNS      40013
#define ID_VIEW_AUTOSIZE     40015
#define ID_VIEW_HTML_HORZ    40028
#define ID_VIEW_HTML_VERT    40029
