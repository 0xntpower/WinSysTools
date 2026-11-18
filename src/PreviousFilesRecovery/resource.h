// resource.h -- resource and command IDs for the PreviousFilesRecovery recreation.
// The numeric values match the original binary's menu, accelerator, dialog, bitmap
// and string-table resources (recovered with Resource Hacker and IDA).
#pragma once

// ---- resources carried over from the original .rsrc --------------------
#define IDI_APP 101   // main window / taskbar icon
#define IDI_SMALL 102 // small icon
#define IDR_MAIN_MENU 102
#define IDR_ACCEL 103
#define IDR_CONTEXT_MENU 104
#define IDR_STOP_MENU 112 // single-item "Stop" menu shown while scanning
#define IDB_TOOLBAR 110   // 112x16 24bpp strip, 7 button images
#define IDD_PROPERTIES 105
#define IDD_EXCEPTION 107
#define IDD_SEARCH 111 // embedded search-options panel (child)
#define IDD_ABOUT 112
#define IDD_RECOVER 113
#define IDD_COLUMNS 1096

// ---- control IDs created at runtime ------------------------------------
#define IDC_LISTVIEW 0x103  // 259, matches the original CreateWindowExW id
#define IDC_TOOLBAR 0x102   // 258
#define IDC_STATUSBAR 0x101 // 257

// Search panel (dialog 111)
#define IDC_SRCH_BASEFOLDER 1015
#define IDC_SRCH_BROWSE 1006
#define IDC_SRCH_DEPTH 1008
#define IDC_SRCH_WILDCARD 1010
#define IDC_SRCH_FINDPREV 1011
#define IDC_SRCH_FINDDELFILE 1012
#define IDC_SRCH_FINDDELDIR 1013
#define IDC_SRCH_MODE 1014
#define IDC_SRCH_SHADOW 1019
#define IDC_SRCH_START 1016
#define IDC_SRCH_STATUS 1017

// Recover dialog (113)
#define IDC_REC_DEST 1006
#define IDC_REC_BROWSE 1007
#define IDC_REC_DOIT 1008
#define IDC_REC_STOP 1013
#define IDC_REC_LINE1 1010
#define IDC_REC_LINE2 1011
#define IDC_REC_LINE3 1014
#define IDC_REC_LINE4 1012

// Properties dialog (105)
#define IDC_PROP_LABEL 1001
#define IDC_PROP_VALUE 1000

// Column Settings dialog (1096)
#define IDC_COL_LIST 1001
#define IDC_COL_MOVEUP 1003
#define IDC_COL_MOVEDOWN 1004
#define IDC_COL_SHOW 1006
#define IDC_COL_HIDE 1007
#define IDC_COL_DEFAULT 1008
#define IDC_COL_WIDTH 1005

// About dialog (112)
#define IDC_ABOUT_TITLE 1002
#define IDC_ABOUT_COPYRIGHT 1004

// ---- command IDs (match the original menu/accelerator command values) --
#define ID_PFR_SAVE 40001
#define ID_PFR_EXIT 40002
#define ID_PFR_ABOUT 40003
#define ID_PFR_REFRESH 40004
#define ID_PFR_GRIDLINES 40005
#define ID_PFR_HTML_ALL 40006
#define ID_PFR_SELECTALL 40007
#define ID_PFR_COPY 40008
#define ID_PFR_PROPERTIES 40009
#define ID_PFR_HTML_SEL 40010
#define ID_PFR_CHOOSECOLS 40011
#define ID_PFR_AUTOSIZE 40012
#define ID_PFR_FIND 40014
#define ID_PFR_DESELECTALL 40015
#define ID_PFR_TOOLTIPS 40022
#define ID_PFR_AUTOSIZE_HDR 40024
#define ID_PFR_MARKODDEVEN 40028
#define ID_PFR_STOP 40029
#define ID_PFR_SHOWTIMEGMT 40031
#define ID_PFR_RECOVER 40032
#define ID_PFR_STARTSCAN 40035

// Sort-by popup command base (runtime popup over the visible columns)
#define ID_PFR_SORTBASE 50000
