// resource.h -- resource and command IDs for the DebugView recreation.
// Command values match the original binary's LISTMENU and ACCELERATORS
// resources recovered with Resource Hacker. Dialog and bitmap ids were
// renumbered to plain integers so the WTL dialog classes can name them.
#pragma once

// WTL atlres.h predefines several of these names. Drop its versions so the
// originals below win without a redefinition warning.
#ifdef ID_FILE_OPEN
#undef ID_FILE_OPEN
#endif
#ifdef ID_FILE_SAVE
#undef ID_FILE_SAVE
#endif
#ifdef ID_FILE_PRINT
#undef ID_FILE_PRINT
#endif
#ifdef ID_APP_EXIT
#undef ID_APP_EXIT
#endif
#ifdef ID_EDIT_COPY
#undef ID_EDIT_COPY
#endif
#ifdef ID_EDIT_FIND
#undef ID_EDIT_FIND
#endif
#ifdef ID_EDIT_CLEAR
#undef ID_EDIT_CLEAR
#endif

// ---- resources carried from the original .rsrc -------------------------
#define IDR_MENU 128    // LISTMENU
#define IDR_ACCEL 129   // ACCELERATORS
#define IDB_TOOLBAR 400 // BITMAP 400, the 16px toolbar strip
#define IDI_APPICON 101 // numeric app icon

#define IDD_ABOUT 2000
#define IDD_FILTER 2001
#define IDD_HISTORY 2002

// About dialog controls
#define IDC_ABOUT_OK 1005
#define IDC_ABOUT_NAME 1040
#define IDC_ABOUT_CREDIT 1041
#define IDC_ABOUT_AUTHOR 1042

// Filter dialog controls (reuses the original FILTER template)
#define IDC_FILTER_INCLUDE 1003
#define IDC_FILTER_EXCLUDE 1004
#define IDC_FILTER_HIGHLIGHT 1005
#define IDC_FILTER_RESET 3

// History Depth dialog controls
#define IDC_HISTORY_EDIT 1005
#define IDC_HISTORY_SPIN 1022
#define IDC_HISTORY_RESET 3

// ---- child windows created at runtime ----------------------------------
#define IDC_LISTVIEW 1500
#define IDC_STATUSBAR 1501
#define IDC_TOOLBAR 1

// ---- message from the capture thread to the main window ----------------
#define WM_DBG_LINE (WM_APP + 1) // lParam = heap DbgRow*

// ---- command ids (match the original menu / accelerator values) --------
#define ID_FILE_NEWWINDOW 40050
#define ID_FILE_OPEN 40057
#define ID_FILE_SAVE 40007
#define ID_FILE_SAVEAS 40012
#define ID_FILE_LOGTOFILE 40046
#define ID_FILE_LOGTOFILEAS 40048
#define ID_FILE_PRINT 40042
#define ID_FILE_PRINTRANGE 40043
#define ID_FILE_CRASHDUMP 40051
#define ID_APP_EXIT 104

#define ID_EDIT_APPENDCMT 40059
#define ID_EDIT_COPY 40039
#define ID_EDIT_FIND 40024
#define ID_EDIT_FILTER 40022
#define ID_EDIT_CLEAR 40020

#define ID_CAPTURE_WIN32 40028
#define ID_CAPTURE_KERNEL 40031
#define ID_CAPTURE_PASSTHRU 40026
#define ID_CAPTURE_EVENTS 40018

#define ID_OPT_WIN32PIDS 40045
#define ID_OPT_FORCECR 40052
#define ID_OPT_HISTORY 40027
#define ID_OPT_CLOCKTIME 40029
#define ID_OPT_MILLISECONDS 40055
#define ID_OPT_FONT 40032
#define ID_OPT_HIDEMIN 40053
#define ID_OPT_HIDETOOLBAR 40041
#define ID_OPT_AUTOSCROLL 40019
#define ID_OPT_ALWAYSONTOP 40033

#define ID_COMP_CONNECT 40034
#define ID_COMP_LOCAL 40049
#define ID_COMP_DISCONNECT 40035

#define ID_HELP_HELP 40017
#define ID_HELP_ABOUT 300

#define ID_EDIT_FINDNEXT 40038
#define ID_EDIT_FINDPREV 40058
