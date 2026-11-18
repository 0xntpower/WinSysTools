// resource.h -- resource and command identifiers for the WinObj recreation.
// The command values match the original's menu and accelerator table (standard
// MFC IDs plus the tool's own 0x80xx range), so the resource templates keep the
// original numbers and this header gives them names for the C++ side.
#pragma once

// ---- top-level resources -------------------------------------------------
#define IDR_MAINFRAME       128
#define IDR_MAIN_MENU       128
#define IDR_ACCEL           128
#define IDR_CTX_MENU        220
#define IDI_WINOBJ          128

// ---- dialogs -------------------------------------------------------------
#define IDD_ABOUT           100
#define IDD_QUICKFIND       103
#define IDD_DETAILS         107
#define IDD_SEARCH          226

// ---- About (100) controls ------------------------------------------------
#define IDC_ABOUT_LINK      1000
#define IDC_ABOUT_NAME      1009
#define IDC_ABOUT_DESC      1010

// ---- Details (107) controls ----------------------------------------------
#define IDC_DET_NAME        111   // read-only edit, full object path
#define IDC_DET_TYPE        104   // type name
#define IDC_DET_PERMANENT   112   // permanent check box
#define IDC_DET_REFS        105   // reference (pointer) count
#define IDC_DET_HANDLES     106   // handle count
#define IDC_DET_PAGED       108   // paged pool quota
#define IDC_DET_NONPAGED    109   // non-paged pool quota
#define IDC_DET_EXTRAGROUP  110   // symbolic-link group box
#define IDC_DET_X1VAL       113   // symbolic-link target value
#define IDC_DET_X2VAL       114   // secondary value (reserved)

// ---- Quick Find (103) / Search (226) controls ---------------------------
#define IDC_FIND_EDIT       1002
#define IDC_SEARCH_BTN      1005
#define IDC_SEARCH_LIST     1007
#define IDC_SEARCH_COUNT    1008

// ---- child window ids ----------------------------------------------------
#define IDC_TREE            0xE900
#define IDC_LIST            0xE901
#define IDC_TOOLBAR         0xE800
#define IDC_STATUSBAR       0xE801

// ---- commands (match the original) ---------------------------------------
// WTL's atlres.h predefines several of these standard MFC command IDs with the
// same values but a different spelling, which trips C4005. Undefine first so the
// tool's own numbers (shared with the resource script) take effect cleanly.
#undef ID_APP_EXIT
#undef ID_APP_ABOUT
#undef ID_EDIT_COPY
#undef ID_EDIT_FIND
#undef ID_VIEW_TOOLBAR
#undef ID_VIEW_REFRESH
#define ID_APP_EXIT         57665
#define ID_EDIT_COPY        57634   // Copy Object Name
#define ID_COPY_FULLNAME    32790
#define ID_COPY_SYMLINK     32791
#define ID_EDIT_FIND        57636   // Quick Find
#define ID_SEARCH           32800   // Search...
#define ID_VIEW_PAUSE       32807
#define ID_VIEW_TOOLBAR     59392
#define ID_VIEW_STATUSBAR   59393
#define ID_VIEW_REFRESH     59395
#define ID_INT_1            32794
#define ID_INT_2            32795
#define ID_INT_5            32796
#define ID_INT_10           32797
#define ID_PROPERTIES       32789
#define ID_ALWAYSONTOP      32775
#define ID_THEME_DEFAULT    32777
#define ID_THEME_DARK       32778
#define ID_FONT             32810
#define ID_APP_ABOUT        57664
#define ID_EXPAND_ALL       32812
#define ID_COLLAPSE_ALL     32813
#define ID_VIEW_DETAILS     32784
#define ID_VIEW_LARGE       32785
#define ID_VIEW_SMALL       32786
#define ID_VIEW_LIST        32787
#define ID_RUNPAUSE         32798
#define ID_RUNASADMIN       101

// Toolbar strip icon resource ids (light theme). The dark variants are these
// plus 100, unused by this light-mode recreation.
#define IDI_TB_LARGE        202
#define IDI_TB_SMALL        203
#define IDI_TB_LIST         204
#define IDI_TB_DETAILS      205
#define IDI_TB_PROPERTIES   223
#define IDI_TB_REFRESH      228
#define IDI_TB_FIND         229
