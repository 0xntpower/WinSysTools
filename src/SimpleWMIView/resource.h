// resource.h -- resource and command IDs for the SimpleWMIView recreation.
// Numeric values match the original binary's menu, accelerator, dialog, string
// table and toolbar resources (recovered with Resource Hacker and IDA).
#pragma once

// ---- resources carried over from the original .rsrc --------------------
#define IDI_APP 101   // main / taskbar icon
#define IDI_SMALL 102 // small icon
#define IDR_MAIN_MENU 102
#define IDR_ACCEL 103
#define IDR_CONTEXT_MENU 104
#define IDR_STOP_MENU 112
#define IDR_TRAY_MENU 113
#define IDD_PROPERTIES 105
#define IDD_EXCEPTION 107
#define IDD_WMIBAR 111 // embedded options bar (namespace / class / computer)
#define IDD_ABOUT 112
#define IDD_QUICKFILTER 114
#define IDD_COLUMNS 1096
#define IDB_TOOLBAR 110
#define IDB_SORT_ASC 133
#define IDB_SORT_DESC 134

// ---- WMI options bar controls (dialog template 111) --------------------
#define IDC_BAR_COMPUTER_COMBO 1016
#define IDC_BAR_COMPUTER_EDIT 1017
#define IDC_BAR_USERNAME_CHK 1018
#define IDC_BAR_USERNAME_EDIT 1020
#define IDC_BAR_PASSWORD_EDIT 1001
#define IDC_BAR_NAMESPACE_COMBO 1006
#define IDC_BAR_SYNCMODE_COMBO 1011
#define IDC_BAR_TIMEOUT_EDIT 1013
#define IDC_BAR_CLASS_COMBO 1008
#define IDC_BAR_UPDATE_BTN 1009

// ---- Column Settings dialog (1096) controls ----------------------------
#define IDC_COL_LIST 1001
#define IDC_COL_MOVEUP 1003
#define IDC_COL_MOVEDOWN 1004
#define IDC_COL_SHOW 1006
#define IDC_COL_HIDE 1007
#define IDC_COL_DEFAULT 1008
#define IDC_COL_WIDTH 1005
#define IDC_COL_SELALL 1009
#define IDC_COL_DESELALL 1010

// ---- Properties dialog (105) controls ----------------------------------
#define IDC_PROP_NAME 1001
#define IDC_PROP_VALUE 1000
#define IDC_PROP_NEXT 1010
#define IDC_PROP_PREV 1011

// ---- About dialog (112) controls ---------------------------------------
#define IDC_ABOUT_TITLE 1002
#define IDC_ABOUT_COPY 1004

// ---- control IDs created at runtime ------------------------------------
#define IDC_LISTVIEW 1500
#define IDC_STATUSBAR 1501
#define IDC_TOOLBAR 1502

// ---- command IDs (match the original menu/accelerator command values) --
#define ID_SWV_SAVE 40001
#define ID_SWV_EXIT 40002
#define ID_SWV_ABOUT 40003
#define ID_SWV_REFRESH 40004
#define ID_SWV_GRIDLINES 40005
#define ID_SWV_HTML_ALL 40006
#define ID_SWV_SELECTALL 40007
#define ID_SWV_COPY 40008
#define ID_SWV_PROPERTIES 40009
#define ID_SWV_HTML_SEL 40010
#define ID_SWV_CHOOSECOLS 40011
#define ID_SWV_AUTOSIZE 40012
#define ID_SWV_FIND 40014
#define ID_SWV_DESELECTALL 40015
#define ID_SWV_TOOLTIPS 40022
#define ID_SWV_AUTOSIZE_HDR 40024
#define ID_SWV_MARKODDEVEN 40028
#define ID_SWV_RUNASADMIN 40029
#define ID_SWV_AUTOSIZELOAD 40033
#define ID_SWV_SHOWGMT 40034
#define ID_SWV_STOP 40036
#define ID_SWV_AUTOFILLCLS 40037
#define ID_SWV_TRAYICON 40038
#define ID_SWV_TRAY_OPEN 40040
#define ID_SWV_COPYSORTED 40041
#define ID_SWV_ALWAYSONTOP 40042
#define ID_SWV_AUTOUPDATE 40043
#define ID_SWV_NEWINSTANCE 40044
#define ID_SWV_SELECTFONT 40055
#define ID_SWV_DEFAULTFONT 40056
#define ID_SWV_SAVEALL 40057
#define ID_SWV_QUICKFILTER 40060
#define ID_SWV_AUTOCOMPLETE 40063
#define ID_SWV_ENUMFULLNS 40064
#define ID_SWV_COPYCELL 40065
#define ID_SWV_STARTHIDDEN 40066
#define ID_SWV_SHOWMS 40067
#define ID_SWV_FULLSCREEN 40068
#define ID_SWV_BLACKBG 40071
#define ID_SWV_ALIGNNUMERIC 40052
#define ID_SWV_SORTBY 49902 // toolbar "Sort By" dropdown

// Save-encoding radio group (Options menu)
#define ID_SWV_ENC_DEFAULT 45700
#define ID_SWV_ENC_ANSI 45701
#define ID_SWV_ENC_UNICODE 45702
#define ID_SWV_ENC_UTF8 45703

// ---- string-table ids (recovered) --------------------------------------
#define IDS_ITEM_COUNT 4 // "%d item(s)"
#define IDS_SELECTED 5   // ", %d Selected"
#define IDS_CREATED_BY 6 // "Created by using"
#define IDS_SAVE_TITLE 7 // "Select a filename to save"
#define IDS_WMI_DATA 8   // "WMI Data"
#define IDS_FALSE 9
#define IDS_TRUE 10
#define IDS_SORT_BY 11
#define IDS_LOADING 14
#define IDS_FILTER_LABEL_BASE 500 // 501..508 save-format labels
#define IDS_CIMTYPE_BASE 2000     // 2000 + CIM type -> type name
