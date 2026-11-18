// MainFrame.h -- WTL main window for the FileTypesMan recreation.
// Two stacked list panes (the upper lists every file type / extension, the lower lists
// the actions of the selected type), a menu (102), an 8-image toolbar (104), a status
// bar and the extracted dialogs. The data model comes from EnumerateFileTypes /
// EnumerateActions rather than the framework code. Config lives in FileTypesMan.cfg.
//
// The method bodies live in MainFrame.cpp. This header keeps the class shape:
// members, the WTL macros, the message map and the method declarations.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atlsplit.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "FileType.h"
#include "FileAction.h"
#include "Columns.h"
#include "Export.h"
#include "Dialogs.h"

extern CAppModule _Module;

struct AppOptions
{
    bool showGridLines = false;   // ShowGridLines 0
    bool showInfoTip = true;      // ShowInfoTip 1
    bool dontLoadIcons = false;   // DontLoadIcons 0
    bool blackBackground = false; // BlackBackground 0
    bool dontLoadHang = true;     // DontLoadHangIcons 1
    bool markDisabled = true;     // MarkDisabledActions 1
    bool autoDeskRefresh = true;  // AutoDesktopRefresh 1
    bool showUrl = false;         // ShowURLProtocols 0
    bool extractCaption = true;   // ExtractResourceCaption 1
    bool showApps = true;         // ShowApplicationsTypes 1
    bool removeDot = false;       // RemoveExtensionDot 0
    bool addHeaderLine = false;   // AddExportHeaderLine 0
    bool markOddEven = false;     // MarkOddEvenRows 0
    int saveFilterIndex = 1;      // SaveFilterIndex
};

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"FileTypesMan", IDR_MAIN_MENU)

    CHorSplitterWindow m_split;
    CListViewCtrl m_types, m_actions;
    CImageList m_images;

    std::vector<FileType> m_items;
    std::vector<FileAction> m_acts;
    std::vector<int> m_typeVis, m_actVis;
    int m_typeW[kTypeColumnCount];
    int m_actW[kActionColumnCount];
    int m_typeSort = 0, m_actSort = 0;
    bool m_typeAsc = true, m_actAsc = true;

    AppOptions m_opt;
    bool m_noSaveLoad = false;
    int m_lastFocus = IDC_LIST_TYPES;
    int m_selectedType = -1;
    std::wstring m_startSelectExt;
    std::wstring m_cfgPath;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_findMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    CMainFrame();

    static std::wstring ConfigPath();

    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MESSAGE_HANDLER(m_findMsg, OnFindReplace)
    NOTIFY_HANDLER(IDC_LIST_TYPES, LVN_GETDISPINFO, OnTypesDispInfo)
    NOTIFY_HANDLER(IDC_LIST_TYPES, LVN_COLUMNCLICK, OnTypesColumnClick)
    NOTIFY_HANDLER(IDC_LIST_TYPES, LVN_ITEMCHANGED, OnTypesChanged)
    NOTIFY_HANDLER(IDC_LIST_TYPES, NM_SETFOCUS, OnTypesFocus)
    NOTIFY_HANDLER(IDC_LIST_TYPES, NM_RCLICK, OnTypesRClick)
    NOTIFY_HANDLER(IDC_LIST_TYPES, NM_DBLCLK, OnTypesDblClk)
    NOTIFY_HANDLER(IDC_LIST_ACTIONS, LVN_GETDISPINFO, OnActionsDispInfo)
    NOTIFY_HANDLER(IDC_LIST_ACTIONS, LVN_COLUMNCLICK, OnActionsColumnClick)
    NOTIFY_HANDLER(IDC_LIST_ACTIONS, NM_SETFOCUS, OnActionsFocus)
    NOTIFY_HANDLER(IDC_LIST_ACTIONS, NM_RCLICK, OnActionsRClick)
    COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
    COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
    COMMAND_ID_HANDLER(ID_EDIT_SELECTALL, OnSelectAll)
    COMMAND_ID_HANDLER(ID_EDIT_DESELECTALL, OnDeselectAll)
    COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_VIEW_GRIDLINES, OnGridLines)
    COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE, OnAutoSize)
    COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE_HDR, OnAutoSize)
    COMMAND_ID_HANDLER(ID_VIEW_COLUMNS, OnChooseColumns)
    COMMAND_ID_HANDLER(ID_VIEW_HTML_ALL, OnHtmlAll)
    COMMAND_ID_HANDLER(ID_VIEW_HTML_SEL, OnHtmlSel)
    COMMAND_ID_HANDLER(ID_FILE_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_EDIT_FILETYPE_REGEDIT, OnTypeRegEdit)
    COMMAND_ID_HANDLER(ID_ACTION_REGEDIT, OnActionRegEdit)
    COMMAND_ID_HANDLER(ID_EDIT_DELETEEXT, OnDeleteExt)
    COMMAND_ID_HANDLER(ID_EDIT_NEWEXT, OnNewExt)
    COMMAND_ID_HANDLER(ID_ACTION_DELETE, OnDeleteActions)
    COMMAND_ID_HANDLER(ID_HELP_WEBPAGE, OnWebPage)
    COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
    COMMAND_ID_HANDLER(ID_VIEW_TOOLTIPS, OnToggleTooltips)
    COMMAND_ID_HANDLER(ID_OPT_DONTLOADICONS, OnToggleDontLoadIcons)
    COMMAND_ID_HANDLER(ID_OPT_DONTLOADHANG, OnToggleDontLoadHang)
    COMMAND_ID_HANDLER(ID_OPT_MARKDISABLED, OnToggleMarkDisabled)
    COMMAND_ID_HANDLER(ID_OPT_AUTODESKREFRESH, OnToggleAutoDesk)
    COMMAND_ID_HANDLER(ID_OPT_SHOWURL, OnToggleShowUrl)
    COMMAND_ID_HANDLER(ID_OPT_EXTRACTCAPTION, OnToggleExtractCaption)
    COMMAND_ID_HANDLER(ID_OPT_SHOWAPPS, OnToggleShowApps)
    COMMAND_ID_HANDLER(ID_OPT_HIDEDOT, OnToggleHideDot)
    COMMAND_ID_HANDLER(ID_OPT_ADDHEADER, OnToggleAddHeader)
    COMMAND_ID_HANDLER(ID_VIEW_MARKODDEVEN, OnToggleMarkOddEven)
    COMMAND_ID_HANDLER(ID_VIEW_BLACKBG, OnToggleBlackBg)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();
    void UpdateMenuChecks();

    // ---- columns --------------------------------------------------------
    static void RebuildColumns(CListViewCtrl& lv, const ColumnDef* defs, const std::vector<int>& vis, const int* w);
    void ApplyGridLines();

    // ---- cell text ------------------------------------------------------
    std::wstring TypeCell(int row, int col) const;
    std::wstring ActionCell(int row, int col) const;

    // ---- data -----------------------------------------------------------
    EnumOptions enumOpts() const;
    void Refresh();
    void RefreshActions(int typeIndex);
    void SortTypes();
    void SortActions();
    void SelectExt(const std::wstring& ext);

    // ---- notifications --------------------------------------------------
    LRESULT OnTypesDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnActionsDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT FeedDispInfo(LPNMHDR p, bool types);
    LRESULT OnTypesChanged(int, LPNMHDR p, BOOL&);
    LRESULT OnTypesFocus(int, LPNMHDR, BOOL&);
    LRESULT OnActionsFocus(int, LPNMHDR, BOOL&);
    LRESULT OnTypesColumnClick(int, LPNMHDR p, BOOL&);
    LRESULT OnActionsColumnClick(int, LPNMHDR p, BOOL&);
    LRESULT OnTypesDblClk(int, LPNMHDR, BOOL&);
    LRESULT OnTypesRClick(int, LPNMHDR, BOOL&);
    LRESULT OnActionsRClick(int, LPNMHDR, BOOL&);
    void ShowContext(UINT menuId);
    void UpdateStatus();

    // ---- focused pane helpers -------------------------------------------
    bool typesFocused() const;
    CListViewCtrl& activeList();
    std::vector<int> SelectedIndices(CListViewCtrl& lv);
    int FocusedIndex(CListViewCtrl& lv);
    ExportRequest TypesRequest(bool selectedOnly);
    ExportRequest ActionsRequest(bool selectedOnly);

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&);
    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&);
    LRESULT OnSave(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&);
    void ShowHtml(const ExportRequest& r);
    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnTypeRegEdit(WORD, WORD, HWND, BOOL&);
    LRESULT OnActionRegEdit(WORD, WORD, HWND, BOOL&);
    static void OpenRegEdit(const std::wstring& key);

    // ---- writes (manage associations) -----------------------------------
    LRESULT OnDeleteExt(WORD, WORD, HWND, BOOL&);
    LRESULT OnNewExt(WORD, WORD, HWND, BOOL&);
    LRESULT OnDeleteActions(WORD, WORD, HWND, BOOL&);
    bool PromptText(const wchar_t* prompt, wchar_t* buf, int cch);
    LRESULT OnWebPage(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);

    // ---- options toggles ------------------------------------------------
    LRESULT OnToggleTooltips(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleDontLoadIcons(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleDontLoadHang(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleMarkDisabled(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleAutoDesk(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleShowUrl(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleExtractCaption(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleShowApps(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleHideDot(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleAddHeader(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleMarkOddEven(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleBlackBg(WORD, WORD, HWND, BOOL&);
    void ApplyColors();

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    LRESULT OnFindReplace(UINT, WPARAM, LPARAM lParam, BOOL&);
    void FindNext(const std::wstring& needle, bool down);
    void CopyToClipboard(const std::wstring& text);

    // ---- config (.cfg INI [General]) ------------------------------------
    int iniInt(const wchar_t* key, int def);
    void iniSetInt(const wchar_t* key, int v);
    std::wstring iniStr(const wchar_t* key);
    void LoadConfig();
    void SaveConfig();
    void LoadColumns(const std::wstring& blob, std::vector<int>& vis, int* w, int count);
    void SaveColumns(const wchar_t* key, CListViewCtrl& lv, std::vector<int>& vis, int* w);
    static std::vector<BYTE> parseHex(const std::wstring& s);
    static std::wstring hexBytes(const void* data, size_t n);

    void OnDestroy();
};
