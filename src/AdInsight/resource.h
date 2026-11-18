// resource.h -- resource and command identifiers for the AdInsight recreation.
// The icon is loaded by the name the original uses (APPICON). Command IDs for
// the in-scope menu items are given the tool's own values. Some original menu
// numbers were not all recovered from the binary and are noted in
// analysis/AdInsight.md.
#pragma once

// ---- top-level resources -------------------------------------------------
#define IDR_MAINFRAME 101
#define IDI_APPICON 101

// The embedded hook DLL, matching the original's BINRES resource id 1170.
#define IDR_HOOKDLL 1170

// ---- dialogs -------------------------------------------------------------
#define IDD_ABOUT 250
#define IDD_FIND 251

// ---- About controls ------------------------------------------------------
#define IDC_ABOUT_ICON 1100
#define IDC_ABOUT_NAME 1131
#define IDC_ABOUT_DESC 1132
#define IDC_ABOUT_LINK 1034

// ---- Find controls -------------------------------------------------------
#define IDC_FIND_EDIT 1200
#define IDC_FIND_PROMPT 1201

// ---- child controls ------------------------------------------------------
#define IDC_LIST 2100   // captured-call report list
#define IDC_DETAIL 2101 // detail pane
#define IDC_TOOLBAR 2102
#define IDC_STATUSBAR 2103

// ---- WTL predefines some of these, so undefine before our own values -----
#undef ID_FILE_SAVE
#undef ID_FILE_SAVE_AS
#undef ID_FILE_OPEN
#undef ID_APP_EXIT
#undef ID_EDIT_COPY
#undef ID_EDIT_FIND
#undef ID_EDIT_CLEAR
#undef ID_EDIT_SELECT_ALL
#undef ID_APP_ABOUT
#undef ID_HELP

// ---- commands ------------------------------------------------------------
#define ID_FILE_OPEN 40001
#define ID_FILE_SAVE 40002
#define ID_FILE_SAVE_AS 40003
#define ID_FILE_EXPORT 40004
#define ID_APP_EXIT 40005

#define ID_EDIT_COPY 40010
#define ID_EDIT_FIND 40011
#define ID_EDIT_CLEAR 40012
#define ID_EDIT_SELECT_ALL 40013

#define ID_CAPTURE_EVENTS 40020     // Capture Events toggle (Ctrl+E)
#define ID_CAPTURE_AUTOSCROLL 40021 // Autoscroll toggle (Ctrl+A)

#define ID_OPT_FILTER 40030
#define ID_OPT_SELECTCOLS 40031
#define ID_OPT_FONT 40032
#define ID_OPT_ALWAYSONTOP 40033

#define ID_INJECT_PID 40040 // targeted injection into a test process

#define ID_HELP 40050
#define ID_APP_ABOUT 40051
