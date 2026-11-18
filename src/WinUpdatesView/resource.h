// resource.h -- resource and command IDs for the WinUpdatesView recreation.
// The numeric values match the original binary's menu, accelerator and string
// table resources (recovered with Resource Hacker).
#pragma once

// ---- resources carried over from the original .rsrc --------------------
#define IDI_APP              101   // main / taskbar icon
#define IDI_MENU             102   // frame-class icon (menu id doubles as icon)
#define IDR_MAIN_MENU        102
#define IDR_ACCEL            103
#define IDR_CONTEXT_MENU     104
#define IDD_PROPERTIES       105
#define IDD_ADVANCED         111
#define IDD_ABOUT            112
#define IDD_COLUMNS          1096
#define IDB_TOOLBAR          110

// ---- control IDs we create at runtime ----------------------------------
#define IDC_LISTVIEW         1500
#define IDC_STATUSBAR        1501
#define IDC_TOOLBAR          1502

// Advanced Options dialog controls (resource 111)
#define IDC_ADV_LOADFROM     1006
#define IDC_ADV_STOPSVC      1007
#define IDC_ADV_EXTFILE      1011
#define IDC_ADV_COMPUTER     1010

// Properties dialog (resource 105) item list control
#define IDC_PROP_LIST        1000

// ---- command IDs (match the original menu/accelerator command values) --
#define ID_WUV_SAVE          40001   // Save Selected Items
#define ID_WUV_EXIT          40002
#define ID_WUV_ABOUT         40003
#define ID_WUV_REFRESH       40004
#define ID_WUV_GRIDLINES     40005
#define ID_WUV_HTML_ALL      40006
#define ID_WUV_SELECTALL     40007
#define ID_WUV_COPY          40008
#define ID_WUV_PROPERTIES    40009
#define ID_WUV_HTML_SEL      40010
#define ID_WUV_CHOOSECOLS    40011
#define ID_WUV_AUTOSIZE      40012
#define ID_WUV_FIND          40014
#define ID_WUV_DESELECTALL   40015
#define ID_WUV_TOOLTIPS      40022
#define ID_WUV_AUTOSIZE_HDR  40024
#define ID_WUV_MARKODDEVEN   40028
#define ID_WUV_ADVANCED      40030
#define ID_WUV_RUNASADMIN    40033
#define ID_WUV_QUICKFILTER   40036
#define ID_WUV_OPEN_SUPPORT  40039
#define ID_WUV_OPEN_INFO     40040
#define ID_WUV_HIDE_APICOMPAT 40041
#define ID_WUV_SHOWTIMEGMT   40042
#define ID_WUV_SELECTFONT    40045
#define ID_WUV_DEFAULTFONT   40046
#define ID_WUV_HIDE_DEFENDER 40047
#define ID_WUV_SAVEALL       40048
#define ID_WUV_COPYCELL      40051
#define ID_WUV_ADDHEADER     40052

// ---- string-table bases (enum lookups) ---------------------------------
#define IDS_OPERATION_BASE   900   // 901 Install, 902 Uninstall
#define IDS_RESULT_BASE      910   // 910..915
#define IDS_SERVERSEL_BASE   920   // 920..923
#define IDS_LOADFROM_BASE    980   // 981..984
#define IDS_COLUMN_BASE      1000  // 1001..1017 column titles
