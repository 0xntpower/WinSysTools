// resource.h -- resource identifiers for the FileTypesMan recreation.
// IDs mirror the original binary so the extracted res/FileTypesMan.rc binds unchanged.
#pragma once

// ---- Icons / bitmaps / cursors / menus / accelerators (original numeric IDs) ----
#define IDI_APP              101   // main window + class icon
#define IDI_SMALL            110   // secondary icon (list item image)
#define IDB_TOOLBAR          104   // toolbar bitmap (128x16, 8 images)
#define IDB_SORT_UP          133   // header sort-up arrow
#define IDB_SORT_DOWN        134   // header sort-down arrow
#define IDR_MAIN_MENU        102
#define IDR_ACCEL            103
#define IDR_CONTEXT_TYPES    104   // upper-pane context menu
#define IDR_CONTEXT_ACTIONS  111   // lower-pane context menu
#define IDC_HAND_CURSOR      103

// ---- Dialog templates (original numeric IDs) ----
#define IDD_PROPERTIES       105
#define IDD_EXCEPTION        107
#define IDD_EDIT_FILETYPE    109
#define IDD_EDIT_ACTION      110
#define IDD_ABOUT            112
#define IDD_REPLACE_TYPE     113
#define IDD_SELECT_PROCESS   114
#define IDD_SEND_REPORT      115
#define IDD_COLUMNS          1096

// ---- Child control IDs (recreation's own WM_CREATE controls) ----
#define IDC_STATUSBAR        0x101  // 257
#define IDC_TOOLBAR          0x102  // 258
#define IDC_LIST_TYPES       0x103  // 259 upper pane
#define IDC_LIST_ACTIONS     0x104  // 260 lower pane

// ---- Dialog control IDs (from the extracted templates) ----
#define IDC_PROP_EDIT        1000
#define IDC_PROP_LABEL       1001
#define IDC_ABOUT_TEXT       1002
#define IDC_ABOUT_SUB        1004
#define IDC_ABOUT_TRANS      1006
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008
#define IDC_COL_WIDTH        1005
// Edit File Type (109)
#define IDC_EFT_EXT          1011
#define IDC_EFT_TYPENAME     1012
#define IDC_EFT_DESC         1006
#define IDC_EFT_ICON         1017
#define IDC_EFT_ICON_BTN     1044
#define IDC_EFT_MIME         1008
#define IDC_EFT_PERCEIVED    1020
#define IDC_EFT_USERCHOICE   1013
#define IDC_EFT_SHELLNEW     1014
// Edit Action (110)
#define IDC_EA_NAME          1022
#define IDC_EA_CAPTION       1024
#define IDC_EA_COMMAND       1026
#define IDC_EA_ICON          1049
#define IDC_EA_DEFAULT       1029
#define IDC_EA_DISABLED      1030
#define IDC_EA_SFA           1040
#define IDC_EA_EXTENDED      1041
#define IDC_EA_DDE_MSG       1035
#define IDC_EA_DDE_APP       1037
#define IDC_EA_DDE_IFEXEC    1036
#define IDC_EA_DDE_TOPIC     1038

// ---- Menu / toolbar command IDs (original numeric values) ----
// Several of these names also live in WTL's atlres.h. Undefine any collisions
// first so our original numeric values take effect in both C++ and the map.
#undef ID_FILE_SAVE
#undef ID_FILE_EXIT
#undef ID_APP_EXIT
#undef ID_APP_ABOUT
#undef ID_VIEW_REFRESH
#undef ID_EDIT_SELECT_ALL
#undef ID_EDIT_COPY
#undef ID_EDIT_FIND

#define ID_FILE_SAVE          40001  // Save Selected Items (Ctrl+S)
#define ID_FILE_EXIT          40002  // Exit
#define ID_HELP_ABOUT         40003  // About
#define ID_VIEW_REFRESH       40004  // Refresh (F5)
#define ID_VIEW_GRIDLINES     40005  // Show Grid Lines
#define ID_VIEW_HTML_ALL      40006  // HTML Report - All Items
#define ID_EDIT_SELECTALL     40007  // Select All (Ctrl+A)
#define ID_EDIT_COPY          40008  // Copy Selected Items (Ctrl+C)
#define ID_FILE_PROPERTIES    40009  // Properties (Alt+Enter)
#define ID_VIEW_HTML_SEL      40010  // HTML Report - Selected Items
#define ID_VIEW_COLUMNS       40011  // Choose Columns
#define ID_VIEW_AUTOSIZE      40012  // Auto Size Columns (Ctrl+Plus)
#define ID_EDIT_FIND          40014  // Find (Ctrl+F)
#define ID_EDIT_DESELECTALL   40015  // Deselect All (Ctrl+D)
#define ID_VIEW_TOOLTIPS      40022  // Show Tooltips
#define ID_OPT_DONTLOADICONS  40024  // Don't Load Icons
#define ID_EDIT_FILETYPE      40025  // Edit Selected File Type (F2)
#define ID_ACTION_EDIT        40028  // Edit Selected Action (F3)
#define ID_OPT_MARKDISABLED   40031  // Mark Disabled Actions
#define ID_EDIT_REPLACETYPE   40032  // Replace File Type For Selected Extension (F4)
#define ID_FILE_REFRESHDESK   40033  // Refresh Desktop Now (Ctrl+F5)
#define ID_OPT_AUTODESKREFRESH 40036 // Automatic Desktop Refresh
#define ID_ACTION_DELETE      40037  // Delete Selected Actions (Del)
#define ID_ACTION_NEW         40038  // New Action (Ctrl+N)
#define ID_HELP_WEBPAGE       40040  // Go To FileTypesMan Web Page
#define ID_OPT_SHOWURL        40041  // Show URL Protocols
#define ID_OPT_SELECTFONT     40042  // Select Another Font
#define ID_OPT_DEFAULTFONT    40043  // Use Default Font
#define ID_ACTION_REGEDIT     40044  // Open Action In RegEdit (Ctrl+E)
#define ID_EDIT_FILETYPE_REGEDIT 40045 // Open File Type In RegEdit (Ctrl+R)
#define ID_EDIT_DELETEEXT     40046  // Delete Selected Extensions (Ctrl+Del)
#define ID_EDIT_NEWEXT        40047  // New File Extension (Ctrl+T)
#define ID_OPT_EXTRACTCAPTION 40054  // Extract Resource Captions
#define ID_OPT_SHOWAPPS       40055  // Show Application Types
#define ID_EDIT_JUMPUSERCHOICE 40056 // Jump To User Choice Entry (Ctrl+F8)
#define ID_HELP_SENDREPORT    40060  // Send Report To Extension Database (Ctrl+F9)
#define ID_HELP_VIEWEXT       40063  // View Extension In Extension Database (F9)
#define ID_OPT_HIDEDOT        40066  // Hide Dot In Extension
#define ID_OPT_ADDHEADER      40067  // Add Header Line To CSV/Tab-Delimited File
#define ID_VIEW_MARKODDEVEN   40068  // Mark Odd/Even Rows
#define ID_VIEW_AUTOSIZE_HDR  40070  // Auto Size Columns+Headers (Shift+Plus)
#define ID_HELP_TRUSTEDINST   40076  // Run As TrustedInstaller
#define ID_ACTION_COPY        40077  // Copy Selected Actions
#define ID_ACTION_PASTE       40078  // Paste Actions
#define ID_OPT_DONTLOADHANG   40080  // Don't Load Icons That May Hang/Crash
#define ID_VIEW_BLACKBG       40081  // Black Background
#define ID_TB_SORT_TYPES      49902  // toolbar Sort By (upper pane)
#define ID_TB_SORT_ACTIONS    49903  // toolbar Sort By (lower pane)
