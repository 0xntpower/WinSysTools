// resource.h -- resource and command IDs for the CredentialsFileView recreation.
// The numeric values match the original binary's menu, accelerator, dialog and
// string-table resources, recovered with Resource Hacker.
#pragma once

// ---- resources carried over from the original .rsrc --------------------
#define IDI_APP 101   // main / taskbar icon
#define IDI_SMALL 102 // small icon
#define IDR_MAIN_MENU 102
#define IDR_ACCEL 103
#define IDR_CONTEXT_MENU 104
#define IDD_PROPERTIES 105
#define IDD_EXCEPTION 107
#define IDD_ABOUT 112
#define IDD_DECRYPT_OPTIONS 113
#define IDD_COLUMNS 1096
#define IDB_TOOLBAR 110

// Decryption Options dialog (resource 113) controls
#define IDC_DEC_SOURCE 1005     // data source combo
#define IDC_DEC_USERNAME 1017   // logged-in user name combo
#define IDC_DEC_ROOTFOLDER 1006 // root folder combo
#define IDC_DEC_AUTOFILL 1007
#define IDC_DEC_CREDFOLDER1 1014
#define IDC_DEC_CREDFOLDER2 1001
#define IDC_DEC_CREDFOLDER3 1002
#define IDC_DEC_PROTECT1 1003
#define IDC_DEC_PROTECT2 1004
#define IDC_DEC_REGHIVE 1011
#define IDC_DEC_PASSWORD 1013

// Properties dialog (resource 105)
#define IDC_PROP_LABEL 1001
#define IDC_PROP_VALUE 1000

// Column Settings dialog (resource 1096)
#define IDC_COL_LIST 1001
#define IDC_COL_MOVEUP 1003
#define IDC_COL_MOVEDOWN 1004
#define IDC_COL_SHOW 1006
#define IDC_COL_HIDE 1007
#define IDC_COL_DEFAULT 1008
#define IDC_COL_WIDTH 1005

// About dialog (resource 112)
#define IDC_ABOUT_PRODUCT 1002
#define IDC_ABOUT_COPYRIGHT 1004

// ---- control IDs created at runtime ------------------------------------
#define IDC_LISTVIEW 1500
#define IDC_LOWERPANE 1501
#define IDC_STATUSBAR 1502
#define IDC_TOOLBAR 0x102 // 258, matches CreateToolbarEx wID

// ---- command IDs (match the original menu/accelerator command values) --
#define ID_CFV_SAVE 40001
#define ID_CFV_EXIT 40002
#define ID_CFV_ABOUT 40003
#define ID_CFV_REFRESH 40004
#define ID_CFV_GRIDLINES 40005
#define ID_CFV_HTML_ALL 40006
#define ID_CFV_SELECTALL 40007
#define ID_CFV_COPY 40008
#define ID_CFV_PROPERTIES 40009
#define ID_CFV_HTML_SEL 40010
#define ID_CFV_CHOOSECOLS 40011
#define ID_CFV_AUTOSIZE 40012
#define ID_CFV_FIND 40014
#define ID_CFV_DESELECTALL 40015
#define ID_CFV_TOOLTIPS 40022
#define ID_CFV_AUTOSIZE_HDR 40024
#define ID_CFV_MARKODDEVEN 40028
#define ID_CFV_DECRYPT_OPTIONS 40030
#define ID_CFV_SHOWTIMEGMT 40036
#define ID_CFV_RUNASADMIN 40037
#define ID_CFV_LOWERPANE_HEX 42501
#define ID_CFV_LOWERPANE_STR 42502
#define ID_CFV_ENC_DEFAULT 45700
#define ID_CFV_ENC_ANSI 45701
#define ID_CFV_ENC_UTF16 45702
#define ID_CFV_ENC_UTF8 45703

// ---- string-table bases (recovered) ------------------------------------
#define IDS_VERSION_BASE 550 // 551 XP, 552 Vista/7/8/10
#define IDS_PERSIST_BASE 600 // 601 Session, 602 Local Machine, 603 Enterprise
#define IDS_TYPE_BASE 650    // 651 Generic .. 656 Domain Extended
#define IDS_SOURCE_BASE 800  // 801..804 data-source combo items
#define IDS_COLUMN_BASE 1000 // 1001..1013 column titles
