// resource.h -- resource and command IDs for the ShadowCopyView recreation.
// Numeric values match the original binary's menu, accelerator, dialog, bitmap
// and string resources, recovered with Resource Hacker and IDA.
#pragma once

// ---- resources carried over from the original .rsrc --------------------
#define IDI_APP 101   // main / taskbar icon
#define IDI_SMALL 102 // small icon
#define IDR_MAIN_MENU 102
#define IDR_ACCEL 103
#define IDR_CTX_SNAPSHOT 104 // snapshot pane context menu (Popup1)
#define IDR_CTX_FILE 112     // file pane context menu (Popup2)
#define IDD_PROPERTIES 105
#define IDD_EXCEPTION 107
#define IDD_ABOUT 112 // about dialog (shares id 112 with the file menu)
#define IDD_COPYFILES 113
#define IDD_PATHBAND 114
#define IDD_COLUMNS 1096
#define IDB_TOOLBAR 110
#define IDB_SORTUP 133
#define IDB_SORTDOWN 134
#define IDC_CURSOR_SPLIT 111

// ---- control IDs we create at runtime ----------------------------------
#define IDC_SNAPLIST 0x103  // 259, matches the original upper list wID
#define IDC_FILELIST 0x104  // 260, matches the original lower list wID
#define IDC_TOOLBAR 0x102   // 258
#define IDC_STATUSBAR 0x101 // 257
#define IDC_FILEPATH 2001

// Properties dialog (resource 105)
#define IDC_PROP_LABEL 1001
#define IDC_PROP_VALUE 1000

// About dialog (resource 112)
#define IDC_ABOUT_NAME 1002
#define IDC_ABOUT_DESC 1004
#define IDC_ABOUT_TRANS 1006

// Copy Files dialog (resource 113)
#define IDC_COPY_DOIT 1008
#define IDC_COPY_CLOSE 2
#define IDC_COPY_DEST 1006
#define IDC_COPY_BROWSE 1007
#define IDC_COPY_STATUS1 1010
#define IDC_COPY_STATUS2 1011
#define IDC_COPY_STATUS3 1014
#define IDC_COPY_STATUS4 1012
#define IDC_COPY_STOP 1013

// Column Settings dialog (resource 1096)
#define IDC_COL_LIST 1001
#define IDC_COL_MOVEUP 1003
#define IDC_COL_MOVEDOWN 1004
#define IDC_COL_SHOW 1006
#define IDC_COL_HIDE 1007
#define IDC_COL_DEFAULT 1008
#define IDC_COL_WIDTH 1005

// ---- command IDs (match the original menu/accelerator command values) --
#define ID_SCV_SAVE 40001
#define ID_SCV_EXIT 40002
#define ID_SCV_ABOUT 40003
#define ID_SCV_REFRESH 40004
#define ID_SCV_GRIDLINES 40005
#define ID_SCV_HTML_ALL 40006
#define ID_SCV_SELECTALL 40007
#define ID_SCV_COPY 40008
#define ID_SCV_PROPERTIES 40009
#define ID_SCV_HTML_SEL 40010
#define ID_SCV_CHOOSECOLS 40011
#define ID_SCV_AUTOSIZE 40012
#define ID_SCV_FIND 40014
#define ID_SCV_DESELECTALL 40015
#define ID_SCV_TOOLTIPS 40022
#define ID_SCV_AUTOSIZE_HDR 40024
#define ID_SCV_MARKODDEVEN 40028
#define ID_SCV_OPEN_EXPLORER 40029
#define ID_SCV_SHOWTIMEGMT 40031
#define ID_SCV_GOTOROOT 40032
#define ID_SCV_LEVELUP 40033
#define ID_SCV_COPYFILESTO 40035
