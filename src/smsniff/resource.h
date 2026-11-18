// resource.h -- resource and command IDs for the SmartSniff recreation.
// Numeric values match the original binary's menu, accelerator, dialog, bitmap,
// cursor and string resources, recovered with Resource Hacker and IDA.
#pragma once

// ---- icons / bitmaps / cursors carried over from the original .rsrc ------
#define IDI_APP              101   // main / taskbar icon
#define IDI_SMALL            102   // small icon (shares numeric id 102 with the main menu, different type)
#define IDI_TRAY1            206
#define IDI_TRAY2            207
#define IDB_TOOLBAR          104   // 180x16 4bpp strip, 11 icons
#define IDB_SORTUP           133
#define IDB_SORTDOWN         134
#define IDC_CURSOR_SPLIT     103

// ---- menus and accelerators ---------------------------------------------
#define IDR_MAIN_MENU        102
#define IDR_CTX_MENU         104   // Popup1 upper pane, Popup2 lower pane
#define IDR_TRAY_MENU        117
#define IDR_ACCEL            103

// ---- dialog templates ----------------------------------------------------
#define IDD_PROPERTIES       105
#define IDD_EXCEPTION        107
#define IDD_CAPTURE_OPTIONS  108
#define IDD_ABOUT            112
#define IDD_ADVANCED         113
#define IDD_FILTER           114
#define IDD_EXTRACTHTTP      116
#define IDD_COLUMNS          1096

// ---- control IDs we create at runtime (match the original wIDs) ----------
#define IDC_STATUSBAR        0x101   // 257
#define IDC_TOOLBAR          0x102   // 258
#define IDC_LISTVIEW         0x103   // 259, upper connection list
#define IDC_LOWERPANE        0x201   // lower packet / hex view (read-only edit)

// Capture Options dialog (resource 108)
#define IDC_CAP_RAW          1007
#define IDC_CAP_WINPCAP      1008
#define IDC_CAP_NETMON       1009
#define IDC_CAP_NETMON3      1010
#define IDC_CAP_ADAPTERS     1005
#define IDC_CAP_PROMISC      1042
#define IDC_CAP_INFO         1049
#define IDC_CAP_FIREWALL     1050

// Properties dialog (resource 105)
#define IDC_PROP_LABEL       1001
#define IDC_PROP_VALUE       1000

// About dialog (resource 112)
#define IDC_ABOUT_NAME       1002
#define IDC_ABOUT_DESC       1004

// Filter dialog (resource 114)
#define IDC_FILTER_EDIT      1036
#define IDC_FILTER_EXAMPLES  1038
#define IDC_FILTER_CLEAR     3

// Column Settings dialog (resource 1096)
#define IDC_COL_LIST         1001
#define IDC_COL_MOVEUP       1003
#define IDC_COL_MOVEDOWN     1004
#define IDC_COL_SHOW         1006
#define IDC_COL_HIDE         1007
#define IDC_COL_DEFAULT      1008
#define IDC_COL_WIDTH        1005

// ---- command IDs (match the original menu / accelerator command values) --
#define ID_SAVE_SUMMARIES    40001
#define ID_EXIT              40002
#define ID_ABOUT             40003
#define ID_GRIDLINES         40005
#define ID_SELECT_ALL        40007
#define ID_PROPERTIES        40009
#define ID_HTML_REPORT       40010
#define ID_CHOOSE_COLUMNS    40011
#define ID_AUTOSIZE          40012
#define ID_FIND              40014
#define ID_DESELECT_ALL      40015
#define ID_START_CAPTURE     40024
#define ID_STOP_CAPTURE      40025
#define ID_CAPTURE_OPTIONS   40026
#define ID_DISPLAY_HIGHCHARS 40027
#define ID_TOOLTIPS          40028
#define ID_COPY              40031
#define ID_MOVE_NEXT         40032
#define ID_MOVE_PREV         40033
#define ID_SAVE_PACKETS      40034
#define ID_LOAD_PACKETS      40035
#define ID_EXPORT_STREAMS    40039
#define ID_ADVANCED          40040
#define ID_DISPLAY_CAPTIME   40041
#define ID_RESOLVE_IP        40042
#define ID_CAPTURE_FILTER    40043
#define ID_DISPLAY_FILTER    40044
#define ID_SAVE_CONFIG       40045
#define ID_LOAD_CONFIG       40046
#define ID_IPNETINFO         40047
#define ID_OUTGOING_INCOMING 40050
#define ID_AUTOSCROLL        40051
#define ID_ADD_HEADER_LINE   40052
#define ID_HIDE_LOWER        40053
#define ID_DECOMPRESS_HTTP   40054
#define ID_RESTART_CAPTURE   40055
#define ID_EXTRACT_HTTP      40058
#define ID_USE_DNS           40059
#define ID_TRAY_ICON         40060
#define ID_OPEN_MAIN         40062
#define ID_AUTOSIZE_HDR      40068
#define ID_CAPTURE_ONSTART   40071
#define ID_MARK_ODDEVEN      40073
#define ID_ALWAYS_ON_TOP     40074
#define ID_CAPTURE_ACTIVE    40075
#define ID_FIND_UPPER        40078
#define ID_MODE_AUTO         41101
#define ID_MODE_ASCII        41102
#define ID_MODE_HEX          41103
#define ID_MODE_URL          41104
#define ID_PROTO_TCP         41201
#define ID_PROTO_UDP         41202
#define ID_PROTO_ICMP        41203
