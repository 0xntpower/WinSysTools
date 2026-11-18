// resource.h -- resource and command identifiers for the Autoruns recreation.
//
// The standard File, Edit, View and App commands keep the same numeric values
// the original MFC build used, so the extracted numeric menu binds to the
// handlers. In the C++ translation units these ids come from WTL's atlres.h
// (pulled in by atlapp.h before this header), so the definitions below are
// guarded and only fill in anything atlres.h did not provide. The resource
// compiler, which does not see atlres.h, gets them from here.
#pragma once

#ifndef ID_FILE_OPEN
#define ID_FILE_OPEN             0xE101
#endif
#ifndef ID_FILE_SAVE
#define ID_FILE_SAVE             0xE103
#endif
#ifndef ID_APP_ABOUT
#define ID_APP_ABOUT             0xE140
#endif
#ifndef ID_APP_EXIT
#define ID_APP_EXIT              0xE141
#endif
#ifndef ID_EDIT_CLEAR
#define ID_EDIT_CLEAR            0xE120
#endif
#ifndef ID_EDIT_COPY
#define ID_EDIT_COPY             0xE122
#endif
#ifndef ID_EDIT_FIND
#define ID_EDIT_FIND             0xE124
#endif
#ifndef ID_VIEW_REFRESH
#define ID_VIEW_REFRESH          0xE803
#endif

// Main icon, menu, accelerators and string share id 128 (the IDR_MAINFRAME
// convention of the original).
#define IDR_MAINFRAME            128

// Dialogs.
#define IDD_ABOUT                200

// Child control identifiers.
#define IDC_TABS                 1100
#define IDC_LIST                 1101
#define IDC_TOOLBAR              1102
#define IDC_STATUSBAR            1103

// About dialog controls.
#define IDC_ABOUT_NAME           1018
#define IDC_ABOUT_DESC           1019
#define IDC_ABOUT_LINK           1002

// Autoruns-specific commands (match the extracted menu's numeric ids).
#define ID_ANALYZE_OFFLINE       32775
#define ID_THEME_LIGHT           32776
#define ID_THEME_DARK            32777
#define ID_THEME_SYSTEM          32778
#define ID_HELP_AUTORUNS         32797
#define ID_JUMP_ENTRY            32804
#define ID_JUMP_IMAGE            32805
#define ID_VERIFY_IMAGE          32806
#define ID_PROCEXP               32807
#define ID_SEARCH_ONLINE         32808
#define ID_COMPARE               32809
#define ID_ALWAYS_ONTOP          32810
#define ID_HIDE_EMPTY            32811
#define ID_HIDE_MICROSOFT        32812
#define ID_HIDE_WINDOWS          32813
#define ID_HIDE_VTCLEAN          32814
#define ID_SCAN_OPTIONS          32815
#define ID_FONT                  32816
#define ID_PROPERTIES            32818
#define ID_SCAN_CANCEL           32819
#define ID_QUICK_FIND            32823
#define ID_FIND_NEXT             32829
#define ID_FIND_PREVIOUS         32833
#define ID_CHECK_VT              32835
