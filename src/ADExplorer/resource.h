// resource.h -- resource and command identifiers for the Active Directory
// Explorer recreation.
//
// The command values match the original binary's menu and toolbar (sub_14001B780
// in ADExplorer64.exe), recovered through Resource Hacker and the IDA decompile,
// so the resource templates keep the original numbers and this header names them
// for the C++ side. The named dialog templates (ABOUT, CONNECT, ...) are given
// numeric ids here so WTL CDialogImpl can carry them, while control ids and
// layout stay identical to the original.
#pragma once

// ---- top-level resources -------------------------------------------------
#define IDR_MAINFRAME 128
#define IDR_MAIN_MENU 128
#define IDR_ACCEL 128
#define IDR_CTX_MENU 220
#define IDI_APPICON 200
#define IDB_TOOLBAR 400

// ---- dialogs -------------------------------------------------------------
#define IDD_ABOUT 100
#define IDD_CONNECT 101
#define IDD_CREDENTIALS 102
#define IDD_ATTR_PROPS 103
#define IDD_SEARCH 104

// ---- About (100) controls ------------------------------------------------
#define IDC_ABOUT_NAME 1009
#define IDC_ABOUT_VERSION 1193
#define IDC_ABOUT_DESC 1010
#define IDC_ABOUT_LINK 1170

// ---- Connect (101) controls ----------------------------------------------
#define IDC_CONN_SERVER 1163
#define IDC_CONN_USER 1110
#define IDC_CONN_PASSWORD 1111
#define IDC_CONN_SNAPPATH 1090
#define IDC_CONN_RADIO_AD 1108
#define IDC_CONN_RADIO_SNAP 1109
#define IDC_CONN_SAVE 1164
#define IDC_CONN_NAME 1097
#define IDC_CONN_BROWSE 1093

// ---- Credentials (102) controls ------------------------------------------
#define IDC_CRED_SERVER 1167
#define IDC_CRED_USER 1112
#define IDC_CRED_PASSWORD 1115

// ---- Attribute Properties (103) controls ---------------------------------
#define IDC_AP_ATTR 1089
#define IDC_AP_OBJECT 1081
#define IDC_AP_SYNTAX 1091
#define IDC_AP_SCHEMA 1092
#define IDC_AP_VALUES 1077
#define IDC_AP_GOTO 1119

// ---- Search (104) controls -----------------------------------------------
#define IDC_SEARCH_BASE 1200
#define IDC_SEARCH_FILTER 1201
#define IDC_SEARCH_SCOPE 1202
#define IDC_SEARCH_LIST 1203
#define IDC_SEARCH_COUNT 1204

// ---- main window child ids (match the original dialog template) ----------
#define IDC_TREE 1002
#define IDC_LIST 1070
#define IDC_PATH 1090
#define IDC_TOOLBAR 0x138B
#define IDC_STATUSBAR 0x1389

// ---- commands (match the original menu and toolbar) ----------------------
// WTL's atlres.h predefines several standard MFC command ids with the same
// values but a different spelling, which trips C4005. Undefine first so the
// tool's own numbers (shared with the resource script) take effect cleanly.
#undef ID_APP_EXIT
#undef ID_APP_ABOUT
#undef ID_EDIT_PROPERTIES
#undef ID_FILE_OPEN
#define ID_FILE_CONNECT 40072
#define ID_FILE_REMOVE 1114
#define ID_FILE_SNAPSHOT 40073
#define ID_APP_EXIT 1071
#define ID_EDIT_PROPERTIES 40057
#define ID_FAVORITES_ADD 40086
#define ID_SEARCH_CONTAINER 117
#define ID_COMPARE_SNAPSHOT 40058
#define ID_COMPARE_SECURITY 40075
#define ID_HISTORY_FORWARD 40076
#define ID_HISTORY_BACK 40077
#define ID_HISTORY_ALL 40078
#define ID_HELP_CONTENTS 40085
#define ID_APP_ABOUT 300
#define ID_OBJ_RENAME 40070
#define ID_OBJ_DELETE 40069
#define ID_REFRESH 40083

// ---- toolbar strip image indices (bitmap 400, recovered from the TBBUTTON
// array at 0x140084B20) --------------------------------------------------
#define TB_IMG_CONNECT 6
#define TB_IMG_SEARCH 3
#define TB_IMG_COMPARE 5
#define TB_IMG_SNAPSHOT 4
#define TB_IMG_PROPERTIES 1
#define TB_IMG_BACK 9
#define TB_IMG_FORWARD 10
#define TB_IMG_HISTORY 11
