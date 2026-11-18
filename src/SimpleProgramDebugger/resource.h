// resource.h -- resource and command IDs for the SimpleProgramDebugger recreation.
// The numeric values match the original binary's menu, accelerator, dialog and
// string-table resources recovered with Resource Hacker.
#pragma once

// ---- resources carried over from the original .rsrc --------------------
#define IDI_APP              101   // main / taskbar icon
#define IDI_SMALL            102   // small icon
#define IDR_MAIN_MENU        102
#define IDR_ACCEL            103
#define IDR_CONTEXT_MENU     104
#define IDR_TRAY_MENU        116
#define IDD_PROPERTIES       105
#define IDD_EXCEPTION        107
#define IDD_ABOUT            112
#define IDD_SELECTPROC       113
#define IDD_STARTPROC        114
#define IDD_COLUMNS          1096
#define IDB_TOOLBAR          110

// ---- control IDs we create at runtime ----------------------------------
#define IDC_LISTVIEW         1500
#define IDC_STATUSBAR        1501
#define IDC_TOOLBAR          0x102   // 258, matches the original CreateToolbarEx wID

// ---- custom messages from the debug thread to the window ---------------
#define WM_SPD_EVENT         (WM_APP + 1)   // lParam = heap DbgRow*
#define WM_SPD_STATUS        (WM_APP + 2)   // wParam = SpdStatus, lParam = pid or error

// Start New Process dialog controls (resource 114)
#define IDC_START_PROCESS    1008
#define IDC_START_PARAMS     1001
#define IDC_START_FOLDER     1010
#define IDC_START_BROWSE     1006

// Select Process dialog controls (resource 113)
#define IDC_SELECT_LIST      1006

// Properties dialog controls (resource 105)
#define IDC_PROP_LABEL       1001
#define IDC_PROP_VALUE       1000

// Column Settings dialog controls (resource 1096)
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_WIDTH        1005
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008

// ---- command IDs (match the original menu / accelerator command values) --
#define ID_SPD_SAVE          40001
#define ID_SPD_EXIT          40002
#define ID_SPD_ABOUT         40003
#define ID_SPD_REFRESH       40004
#define ID_SPD_GRIDLINES     40005
#define ID_SPD_HTML_ALL      40006
#define ID_SPD_SELECTALL     40007
#define ID_SPD_COPY          40008
#define ID_SPD_PROPERTIES    40009
#define ID_SPD_HTML_SEL      40010
#define ID_SPD_CHOOSECOLS    40011
#define ID_SPD_AUTOSIZE      40012
#define ID_SPD_FIND          40014
#define ID_SPD_DESELECTALL   40015
#define ID_SPD_TOOLTIPS      40022
#define ID_SPD_AUTOSIZE_HDR  40024
#define ID_SPD_MARKODDEVEN   40028
#define ID_SPD_ATTACH        40029
#define ID_SPD_STARTNEW      40030
#define ID_SPD_DETACH        40033
#define ID_SPD_SCROLLDOWN    40037
#define ID_SPD_CLEARALL      40038
#define ID_SPD_SORTUPDATE    40040
#define ID_SPD_RUNASADMIN    40041
#define ID_SPD_TRAYICON      40044
#define ID_SPD_STARTHIDDEN   40045
#define ID_SPD_ADDHEADER     40046
#define ID_SPD_TRAY_OPEN     40060

// ---- string-table bases (recovered) ------------------------------------
#define IDS_COLUMN_BASE      1000  // 1001..1006 main list column titles
#define IDS_EVENTTYPE_BASE   1500  // 1501..1508 event-type names
