// resource.h -- resource identifiers for the IconsExtract recreation.
// IDs mirror the original binary so the extracted res/iconsext.rc binds unchanged.
#pragma once

// ---- Icons / bitmaps / cursor / menus / accelerators (original numeric IDs) ----
#define IDI_APP              101   // main window + class icon
#define IDR_MAIN_MENU        102
#define IDR_ACCEL            103   // accelerator table (LoadAccelerators 0x67)
#define IDR_CONTEXT_MENU     104   // right-click popup
#define IDR_STOP_MENU        106   // in-search "Stop" menu
#define IDB_TOOLBAR          104   // toolbar bitmap (5 x 16x16)
#define IDB_TOOLBAR_HOT      133
#define IDB_TOOLBAR_DIS      134
#define IDC_HAND_CURSOR      103

// ---- Dialog templates ----
#define IDD_SEARCH           105
#define IDD_SAVE             107
#define IDD_PROPERTIES       108
#define IDD_IMAGELIST        109
#define IDD_ABOUT            112

// ---- Child control IDs of the main window ----
#define IDC_STATUSBAR        0x101  // 257
#define IDC_TOOLBAR          0x102  // 258
#define IDC_LISTVIEW         0x103  // 259

// ---- Search dialog (105) control IDs ----
#define IDC_SRCH_GROUP_FILES 1025
#define IDC_SRCH_RADIO_FILES 0xA029  // -24535: "Scan icons in files"
#define IDC_SRCH_RADIO_PROC  0xA02A  // -24534: "Scan icons in the selected process"
#define IDC_SRCH_WILDCARD    1000   // combobox: filename / wildcard
#define IDC_SRCH_BROWSEFOLD  1001
#define IDC_SRCH_SUBDIRS     1002
#define IDC_SRCH_BROWSEFILE  1003
#define IDC_SRCH_ICONS       1004
#define IDC_SRCH_CURSORS     1005
#define IDC_SRCH_PROCLIST    1029
#define IDC_SRCH_SIZEFILTER  1012
#define IDC_SRCH_WIDTH       1014
#define IDC_SRCH_HEIGHT      1015
#define IDC_SRCH_X           1026
#define IDC_SRCH_COLORFILTER 1013
#define IDC_SRCH_COLORS      1016

// ---- Save dialog (107) control IDs ----
#define IDC_SAVE_FOLDER      1003
#define IDC_SAVE_BROWSE      1001
#define IDC_SAVE_ASICO       1010

// ---- Properties dialog (108) control IDs ----
#define IDC_PROP_FILENAME    1006
#define IDC_PROP_ICONID      1008
#define IDC_PROP_SIZE        1007
#define IDC_PROP_TYPE        1010
#define IDC_PROP_IMAGES      1005
#define IDC_PROP_COPYIMAGE   1009
#define IDC_PROP_PREVIEW     1011

// ---- About dialog (112) control IDs ----
#define IDC_ABOUT_TITLE      1002
#define IDC_ABOUT_COPYRIGHT  1004
#define IDC_ABOUT_TRANSLABEL 1005
#define IDC_ABOUT_TRANS      1006

// ---- Menu / toolbar / accelerator command IDs (original 40001..40025) ----
// Several names also live in WTL's atlres.h as standard command IDs. Undefine
// them first so the original numeric values take effect in both C++ and the map.
#undef ID_FILE_SAVE
#undef ID_EDIT_COPY
#undef ID_EDIT_SELECT_ALL
#define ID_FILE_SAVE         40001   // Save Selected Icons   (Ctrl+S)
#define ID_FILE_EXIT         40002
#define ID_HELP_ABOUT        40003
#define ID_VIEW_LARGE        40004
#define ID_VIEW_SMALL        40005
#define ID_EDIT_SELECTALL    40007   // Select All            (Ctrl+A)
#define ID_FILE_PROPERTIES   40009   // Properties            (Alt+Enter)
#define ID_FILE_SEARCH       40011   // Search For Icons      (Ctrl+I)
#define ID_VIEW_LIST         40015
#define ID_VIEW_DETAILS      40016
#define ID_EDIT_COPY         40022   // Copy Selected Icon    (Ctrl+C)
#define ID_FILE_FILEPROPS    40025   // File Properties       (F8)
