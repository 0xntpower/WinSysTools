// resource.h -- resource and command IDs for the SniffPass recreation. Numeric
// values match the original binary's menu, accelerator, dialog, bitmap and string
// resources, recovered with Resource Hacker and IDA from targets/sniffpass-x64.
#pragma once

// ---- resources carried over from the original .rsrc --------------------
#define IDI_APP              101   // main / taskbar icon
#define IDI_SMALL            102   // small icon
#define IDR_MAIN_MENU        102
#define IDR_ACCEL            103
#define IDR_CTX_MENU         104   // list context menu (Popup1)
#define IDB_TOOLBAR          104   // toolbar strip (shares id 104 with the menu, different type)
#define IDB_SORTUP           133
#define IDB_SORTDOWN         134
#define IDC_CURSOR_SPLIT     103

#define IDD_PROPERTIES       105
#define IDD_EXCEPTION        107
#define IDD_CAPTURE          108
#define IDD_ABOUT            112
#define IDD_COLUMNS          1096

// ---- control IDs we create at runtime ----------------------------------
#define IDC_LIST             0x103   // 259, matches the original list wID
#define IDC_TOOLBAR          0x102   // 258
#define IDC_STATUSBAR        0x101   // 257

// Properties dialog (resource 105)
#define IDC_PROP_LABEL       1001
#define IDC_PROP_VALUE       1000

// Capture Options dialog (resource 108)
#define IDC_CAP_RAW          1007
#define IDC_CAP_WINPCAP      1008
#define IDC_CAP_NETMON       1009
#define IDC_CAP_NETMON3      1010
#define IDC_CAP_ADAPTERS     1005
#define IDC_CAP_PROMISC      1012
#define IDC_CAP_WIFIMON      1013
#define IDC_CAP_FIREWALL     1011

// About dialog (resource 112)
#define IDC_ABOUT_NAME       1002
#define IDC_ABOUT_DESC       1004
#define IDC_ABOUT_TRANS      1006

// Column Settings dialog (resource 1096)
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008
#define IDC_COL_WIDTH        1005

// ---- command IDs (match the original menu/accelerator command values) --
#define ID_SP_SAVE           40001
#define ID_SP_EXIT           40002
#define ID_SP_ABOUT          40003
#define ID_SP_GRIDLINES      40005
#define ID_SP_HTML_ALL       40006
#define ID_SP_SELECTALL      40007
#define ID_SP_COPY           40008
#define ID_SP_PROPERTIES     40009
#define ID_SP_HTML_SEL       40010
#define ID_SP_CHOOSECOLS     40011
#define ID_SP_AUTOSIZE       40012
#define ID_SP_FIND           40014
#define ID_SP_DESELECTALL    40015
#define ID_SP_EXCLUDEDUP     40022
#define ID_SP_BEEP           40023
#define ID_SP_STARTCAP       40024
#define ID_SP_STOPCAP        40025
#define ID_SP_CAPOPTIONS     40026
#define ID_SP_IMPORT         40027
#define ID_SP_CLEAR          40028
#define ID_SP_ADDHEADER      40029
