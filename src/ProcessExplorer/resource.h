// resource.h -- resource and command identifiers for the Process Explorer
// recreation. The menu and accelerator command values match the original
// procexp64.exe menu (resource 101) so the extracted layout keeps the original
// numbers and the C++ side gets names for them.
#pragma once

// ---- top-level resources (the original loads menu, accelerators and icon
//      under the single id 101 for its main window) ------------------------
#define IDR_MAINFRAME        101
#define IDI_APPICON          101

// ---- dialogs -------------------------------------------------------------
#define IDD_ABOUT            250
#define IDD_PROP_IMAGE       251
#define IDD_PROP_THREADS     252
#define IDD_PROP_TCPIP       253
#define IDD_FINDHANDLE       254

// ---- About controls ------------------------------------------------------
#define IDC_ABOUT_ICON       101
#define IDC_ABOUT_NAME       1131   // product and version line
#define IDC_ABOUT_DESC       1132   // recreation credit
#define IDC_ABOUT_LINK       1034   // sysinternals link

// ---- Image property page controls ----------------------------------------
#define IDC_IMG_ICON         2000
#define IDC_IMG_DESC         2001
#define IDC_IMG_COMPANY      2002
#define IDC_IMG_VERSION      2003
#define IDC_IMG_PATH         2004
#define IDC_IMG_CMDLINE      2005
#define IDC_IMG_CURDIR       2006
#define IDC_IMG_PARENT       2007
#define IDC_IMG_USER         2008
#define IDC_IMG_STARTED      2009
#define IDC_IMG_PID          2010

// ---- Threads property page controls --------------------------------------
#define IDC_THR_LIST         2020

// ---- TCP/IP property page controls ---------------------------------------
#define IDC_TCP_LIST         2030

// ---- Find Handle or DLL controls -----------------------------------------
#define IDC_FIND_EDIT        2040
#define IDC_FIND_LIST        2041
#define IDC_FIND_SEARCH      2042

// ---- main window child controls ------------------------------------------
#define IDC_PROCTREE         2100   // process tree-list (upper pane)
#define IDC_LOWERPANE        2101   // DLL / handle list (lower pane)
#define IDC_TOOLBAR          2102
#define IDC_STATUSBAR        2103

// ---- commands (match the original menu and accelerators) -----------------
// WTL's atlres.h predefines some of these standard IDs with other values, so
// undefine first and give the tool's own numbers (shared with the .rc).
#undef ID_FILE_SAVE
#undef ID_FILE_SAVE_AS
#undef ID_APP_EXIT
#undef ID_VIEW_REFRESH
#undef ID_APP_ABOUT
#undef ID_HELP

#define ID_FILE_RUN          40061
#define ID_FILE_SAVE         40009
#define ID_FILE_SAVE_AS      40010
#define ID_FILE_LOGOFF       40068
#define ID_FILE_SHUTDOWN     40069
#define ID_FILE_RESTARTPC    40070
#define ID_APP_EXIT          40001

#define ID_OPT_RUNATLOGON    40752
#define ID_OPT_VERIFYSIGS    40114
#define ID_OPT_VT_CHECK      40115
#define ID_OPT_VT_SUBMIT     40758
#define ID_OPT_ALWAYSONTOP   40066
#define ID_OPT_HIDEMIN       40053
#define ID_OPT_ONEINSTANCE   40067
#define ID_OPT_CONFIRMKILL   40086
#define ID_OPT_HILITERELOC   40016
#define ID_OPT_TRAY_CPU      40644
#define ID_OPT_TRAY_IO       40719
#define ID_OPT_TRAY_COMMIT   40726
#define ID_OPT_TRAY_PHYS     40727
#define ID_OPT_DIFFDURATION  40065
#define ID_OPT_FONT          40015
#define ID_OPT_THEME_LIGHT   40767
#define ID_OPT_THEME_DARK    40768
#define ID_OPT_THEME_SYSTEM  40769

#define ID_VIEW_PROCTREE     40039
#define ID_VIEW_HEATMAPS     40089
#define ID_VIEW_SCROLLNEW    40717
#define ID_VIEW_UNNAMED      40007
#define ID_VIEW_ALLUSERS     40055
#define ID_VIEW_LOWERPANE    40046
#define ID_VIEW_DLLS         40011
#define ID_VIEW_HANDLES      40006
#define ID_VIEW_THREADS      40770
#define ID_VIEW_REFRESH      40002
#define ID_SPEED_HALF        40023
#define ID_SPEED_1           40021
#define ID_SPEED_2           40019
#define ID_SPEED_5           40020
#define ID_SPEED_10          40022
#define ID_SPEED_PAUSED      40018
#define ID_VIEW_ORGCOLSETS   40661
#define ID_VIEW_SAVECOLSET   40662
#define ID_VIEW_SELECTCOLS   40041

#define ID_PROC_BRINGFRONT   40084
#define ID_PROC_WIN_RESTORE  40641
#define ID_PROC_WIN_MIN      40638
#define ID_PROC_WIN_MAX      40639
#define ID_PROC_WIN_CLOSE    40640
#define ID_PRI_REALTIME      40129
#define ID_PRI_HIGH          40128
#define ID_PRI_NORMAL        40126
#define ID_PRI_IDLE          40123
#define ID_PROC_KILL         40013
#define ID_PROC_KILLTREE     40044
#define ID_PROC_RESTART      40646
#define ID_PROC_SUSPEND      40042
#define ID_PROC_MINIDUMP     40736
#define ID_PROC_FULLDUMP     40730
#define ID_PROC_VT_CHECK     40761
#define ID_PROC_PROPERTIES   40038
#define ID_PROC_SEARCHONLINE 40108

#define ID_FIND_FILTER       40062
#define ID_FIND_HANDLEDLL    40765

#define ID_HELP              40005
#define ID_APP_ABOUT         40004
