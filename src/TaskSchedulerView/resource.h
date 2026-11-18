// resource.h -- resource and command IDs for the TaskSchedulerView recreation.
// The numeric values match the original binary's menu, accelerator, dialog and
// string-table resources (recovered with Resource Hacker).
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
#define IDD_ADVANCED         111
#define IDD_ABOUT            112
#define IDD_QUICKFILTER      114
#define IDD_COLUMNS          1096
#define IDB_TOOLBAR          110

// ---- control IDs we create at runtime ----------------------------------
#define IDC_LISTVIEW         1500
#define IDC_STATUSBAR        1501
#define IDC_TOOLBAR          0x102   // 258, matches CreateToolbarEx wID

// Advanced Options dialog controls (resource 111)
#define IDC_ADV_LOADFROM     1006
#define IDC_ADV_COMPUTER     1008
#define IDC_ADV_STARTREMOTE  1009

// Properties dialog (resource 105)
#define IDC_PROP_LABEL       1001
#define IDC_PROP_VALUE       1000
#define IDC_PROP_NEXT        1017
#define IDC_PROP_PREV        1018

// Column Settings dialog (resource 1096)
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008
#define IDC_COL_SELALL       1009
#define IDC_COL_DESELALL     1010
#define IDC_COL_WIDTH        1005

// ---- command IDs (match the original menu/accelerator command values) --
#define ID_TSV_SAVE          40001
#define ID_TSV_EXIT          40002
#define ID_TSV_ABOUT         40003
#define ID_TSV_REFRESH       40004
#define ID_TSV_GRIDLINES     40005
#define ID_TSV_HTML_ALL      40006
#define ID_TSV_SELECTALL     40007
#define ID_TSV_COPY          40008
#define ID_TSV_PROPERTIES    40009
#define ID_TSV_HTML_SEL      40010
#define ID_TSV_CHOOSECOLS    40011
#define ID_TSV_AUTOSIZE      40012
#define ID_TSV_FIND          40014
#define ID_TSV_DESELECTALL   40015
#define ID_TSV_TOOLTIPS      40022
#define ID_TSV_AUTOSIZE_HDR  40024
#define ID_TSV_MARKODDEVEN   40028
#define ID_TSV_SHOWTIMEGMT   40030
#define ID_TSV_DISABLE       40031
#define ID_TSV_ENABLE        40032
#define ID_TSV_RUNASADMIN    40033
#define ID_TSV_HIDE_DISABLED 40036
#define ID_TSV_ADVANCED      40037
#define ID_TSV_OPEN_TASKSCH  40040
#define ID_TSV_HIDE_MSOFT    40041
#define ID_TSV_ALWAYSONTOP   40042
#define ID_TSV_AUTOREFRESH   40043
#define ID_TSV_AUTOSIZE_LOAD 40044
#define ID_TSV_SELECTFONT    40046
#define ID_TSV_DEFAULTFONT   40048
#define ID_TSV_COPY_XML      40049
#define ID_TSV_SAVEALL       40057
#define ID_TSV_ALIGNNUMERIC  40060
#define ID_TSV_QUICKFILTER   40061
#define ID_TSV_STOPTASK      40064
#define ID_TSV_TRAYICON      40065
#define ID_TSV_HIDE_HIDDEN   40066
#define ID_TSV_RUNNOW        40067
#define ID_TSV_ONLYRUNNING   40068
#define ID_TSV_COPYCELL      40069
#define ID_TSV_OPENFOLDER    40070
#define ID_TSV_ADDHEADER     40071
#define ID_TSV_BLACKBG       40072
#define ID_TSV_ENC_DEFAULT   45700
#define ID_TSV_ENC_ANSI      45701
#define ID_TSV_ENC_UTF16     45702
#define ID_TSV_ENC_UTF8      45703
#define ID_TSV_SORTBY        49902

// ---- string-table bases (recovered) ------------------------------------
#define IDS_STATE_BASE       600   // 600 Unknown .. 604 Running
#define IDS_MULTINST_BASE    660   // 660 Parallel, 661 Queue, 662 Ignore New, 663 Stop Existing
#define IDS_DAY_BASE         670   // 670 Sunday .. 676 Saturday
#define IDS_ACTION_BASE      680   // 680 Run EXE, 681 COM Handler, 682 Send Email, 683 Show Message
#define IDS_TRIGGER_BASE     700   // 700 Event .. 711 Session State Change
#define IDS_COLUMN_BASE      1000  // 1001..1037 column titles
