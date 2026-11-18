// resource.h -- resource and command identifiers for the RamMap recreation.
//
// The command values match the original's menu and accelerator table so the
// resource script keeps the original numbers and the C++ side gets names for
// them. The main window is class RamMapClass with a single tab control, as in
// the original MAIN_WINDOW template.
#pragma once

// ---- top-level resources -------------------------------------------------
#define IDR_MAIN_MENU        1
#define IDR_ACCEL            1

// ---- dialogs -------------------------------------------------------------
#define IDD_ABOUT            100
#define IDD_USAGE            102

// ---- About (100) controls ------------------------------------------------
#define IDC_ABOUT_NAME       1132   // product and version line
#define IDC_ABOUT_DESC       1131   // recreation credit
#define IDC_ABOUT_LINK       1034   // sysinternals link

// ---- main window child controls ------------------------------------------
#define IDC_TABS             1037   // the SysTabControl32 (matches original)
#define IDC_USECOUNTS_LIST   2001
#define IDC_PROCESS_LIST     2002
#define IDC_PRIORITY_LIST    2003
#define IDC_RANGES_LIST      2004
#define IDC_FILESUMMARY_LIST 2005
#define IDC_FILEDETAILS_TREE 2006
#define IDC_PHYSPAGES_NOTE   2007

// ---- commands (match the original menu and accelerators) -----------------
// WTL's atlres.h predefines several standard IDs with different values.
// Undefine first so the tool's own numbers take effect cleanly.
#undef ID_FILE_OPEN
#undef ID_FILE_SAVE
#undef ID_EDIT_FIND
#undef ID_VIEW_REFRESH
#undef ID_APP_EXIT
#undef ID_APP_ABOUT
#undef ID_PANE_NEXT
#undef ID_PANE_PREVIOUS

#define ID_FILE_OPEN         40081
#define ID_FILE_SAVE         40082
#define ID_FILE_FIND         40070
#define ID_FILE_REFRESH      40068
#define ID_FILE_EXIT         1030
#define ID_EMPTY_WORKINGSETS 40083
#define ID_EMPTY_SYSTEMWS    40087
#define ID_EMPTY_MODIFIED    40084
#define ID_EMPTY_STANDBY     40085
#define ID_EMPTY_PRIORITY0   40086
#define ID_SETTINGS_COLORS   1133
#define ID_HELP_USAGE        40089
#define ID_HELP_ABOUT        40007

// accelerator-only ids kept for fidelity with the original table
#define ID_FIND_NEXT         40072
#define ID_FIND_PREV         40073
#define ID_VIEW_SELECTALL    40075
#define ID_PANE_PREV         40076
#define ID_PANE_NEXT         40077
