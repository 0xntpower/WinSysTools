// resource.h -- resource and command identifiers for the VMMap recreation.
//
// The command values match the original's menu and accelerator table so the
// resource script keeps the original numbers and the C++ side gets names for
// them. The control IDs on the main window match the original MAIN_WINDOW
// dialog template.
#pragma once

// ---- top-level resources -------------------------------------------------
#define IDR_MAIN_MENU        1
#define IDR_ACCEL            1
#define IDI_APPICON          200

// ---- dialogs -------------------------------------------------------------
#define IDD_ABOUT            100
#define IDD_SELECT_PROCESS   101
#define IDD_USAGE            102
#define IDD_QUICK_HELP       103

// ---- About (100) controls ------------------------------------------------
#define IDC_ABOUT_NAME       1131   // product and version line
#define IDC_ABOUT_DESC       1132   // recreation credit
#define IDC_ABOUT_LINK       1034   // sysinternals link

// ---- Select Process (101) controls --------------------------------------
#define IDC_PROC_LIST        1003
#define IDC_PROC_REFRESH     1023
#define IDC_PROC_SHOWALL     1030

// ---- main window child controls (match the original template) ------------
#define IDC_SUMMARY          1016   // summary category list
#define IDC_DETAIL           1003   // detail region list
#define IDC_PROCESS_NAME     1007
#define IDC_PROCESS_PID      1017
#define IDC_COMMITTED_VAL    1019
#define IDC_PRIVATE_VAL      1026
#define IDC_WS_VAL           1020
#define IDC_APP_ICON         1021
#define IDC_BTN_TIMELINE     1022   // out of scope, disabled
#define IDC_BTN_HEAPALLOC    1046   // out of scope, disabled
#define IDC_BTN_CALLTREE     1036   // out of scope, disabled
#define IDC_BTN_TRACE        1035   // out of scope, disabled
#define IDC_STATUSBAR        0xE801

// ---- commands (match the original menu and accelerators) -----------------
// WTL's atlres.h predefines several of these standard IDs with different
// values. Undefine first so the tool's own numbers (shared with the resource
// script) take effect cleanly.
#undef ID_FILE_OPEN
#undef ID_FILE_SAVE
#undef ID_EDIT_FIND
#undef ID_VIEW_REFRESH
#undef ID_PANE_NEXT
#define ID_FILE_SELECTPROC   40072
#define ID_FILE_OPEN         40049
#define ID_FILE_SAVE         40025
#define ID_FILE_SAVEAS       40031
#define ID_FILE_EXIT         40002
#define ID_EDIT_COPYADDR     40038
#define ID_EDIT_COPYALL      40041
#define ID_EDIT_FIND         40018
#define ID_FIND_NEXT         40022
#define ID_FIND_PREV         40021
#define ID_VIEW_EXPANDALL    40020
#define ID_VIEW_COLLAPSEALL  40030
#define ID_VIEW_STRINGS      40058
#define ID_VIEW_FRAGMENT     40074
#define ID_VIEW_REFRESH      40011
#define ID_TOOLS_EMPTYWS     40010
#define ID_OPT_SHOWFREE      40051
#define ID_OPT_INT_1         40079
#define ID_OPT_INT_2         40080
#define ID_OPT_INT_5         40082
#define ID_OPT_INT_10        40083
#define ID_OPT_PAUSED        40084
#define ID_OPT_FONT          40027
#define ID_OPT_COLORS        40100
#define ID_OPT_SYMBOLS       40067
#define ID_HELP_HELP         40037
#define ID_HELP_QUICK        40055
#define ID_HELP_CMDLINE      40035
#define ID_HELP_ABOUT        40007
#define ID_PANE_NEXT         40076
#define ID_PANE_PREV         40077
#define ID_VIEW_DETAILSFOCUS 40075
