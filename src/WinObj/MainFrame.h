// MainFrame.h -- the WinObj main window.
//
// The original hangs a toolbar, a status bar, a quick-find band and a
// tree/list splitter off a plain frame. The tree on the left mirrors the
// object-directory hierarchy from the namespace root, the list on the right
// shows the selected directory's objects with Name, Type and SymLink columns.
// This rebuilds that over the nt:: namespace walker, pinned to light mode with
// the shared toolbar fix so the band reads light on a dark-mode system.
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
#include <atlmisc.h>
#include <shlobj.h>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "NtObjMgr.h"
#include "Dialogs.h"

extern CAppModule _Module;

// Show the wait cursor for the duration of a scope.
struct ScopedWait
{
    HCURSOR prev;
    ScopedWait()
        : prev(::SetCursor(::LoadCursorW(nullptr, IDC_WAIT)))
    {
    }
    ~ScopedWait()
    {
        ::SetCursor(prev);
    }
};

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
  public:
    DECLARE_FRAME_WND_CLASS(L"WinObj", IDR_MAIN_MENU)

    CTreeViewCtrl m_tree;
    CListViewCtrl m_list;
    CEdit m_find;

    std::vector<std::wstring> m_treePaths; // tree item lParam -> full path
    std::vector<nt::ObjEntry> m_listEntries;
    std::wstring m_curPath = L"\\";

    int m_treeW = 240;
    bool m_dragging = false;
    static const int kSplitBar = 5;

    bool m_findVisible = false;
    static const int kFindH = 24;

    int m_sortCol = 0;
    bool m_sortAsc = true;

    bool m_paused = true; // auto-refresh starts paused in this recreation
    UINT m_interval = 5;  // seconds
    bool m_alwaysOnTop = false;
    static const UINT_PTR kRefreshTimer = 1;

    HACCEL m_hAccel = nullptr;
    CImageList m_listImages;
    CImageList m_treeImages;
    CFont m_font;
    LOGFONTW m_logFont{};
    bool m_haveFont = false;

    bool m_noSaveLoad = false;
    bool m_havePlacement = false;
    WINDOWPLACEMENT m_placement{};

    // ---- message filter -------------------------------------------------
    BOOL PreTranslateMessage(MSG* pMsg) override;

    BEGIN_MSG_MAP(CMainFrame)
    MSG_WM_ERASEBKGND(OnEraseBkgnd)
    MSG_WM_CREATE(OnCreate)
    MSG_WM_DESTROY(OnDestroy)
    MSG_WM_SIZE(OnSize)
    MSG_WM_SETCURSOR(OnSetCursor)
    MSG_WM_LBUTTONDOWN(OnLButtonDown)
    MSG_WM_MOUSEMOVE(OnMouseMove)
    MSG_WM_LBUTTONUP(OnLButtonUp)
    MSG_WM_TIMER(OnTimer)
    COMMAND_CODE_HANDLER(EN_CHANGE, OnFindChange)
    NOTIFY_CODE_HANDLER(TVN_SELCHANGEDW, OnTreeSelChanged)
    NOTIFY_CODE_HANDLER(NM_RCLICK, OnRClick)
    NOTIFY_CODE_HANDLER(LVN_GETDISPINFOW, OnGetDispInfo)
    NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDblClk)
    NOTIFY_CODE_HANDLER(LVN_KEYDOWN, OnListKeyDown)
    COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
    COMMAND_ID_HANDLER(ID_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopyName)
    COMMAND_ID_HANDLER(ID_COPY_FULLNAME, OnCopyFull)
    COMMAND_ID_HANDLER(ID_COPY_SYMLINK, OnCopySymlink)
    COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
    COMMAND_ID_HANDLER(ID_SEARCH, OnFind)
    COMMAND_ID_HANDLER(ID_EXPAND_ALL, OnExpandAll)
    COMMAND_ID_HANDLER(ID_COLLAPSE_ALL, OnCollapseAll)
    COMMAND_ID_HANDLER(ID_VIEW_DETAILS, OnViewDetails)
    COMMAND_ID_HANDLER(ID_VIEW_LARGE, OnViewLarge)
    COMMAND_ID_HANDLER(ID_VIEW_SMALL, OnViewSmall)
    COMMAND_ID_HANDLER(ID_VIEW_LIST, OnViewList)
    COMMAND_ID_HANDLER(ID_VIEW_TOOLBAR, OnToggleToolbar)
    COMMAND_ID_HANDLER(ID_VIEW_STATUSBAR, OnToggleStatusbar)
    COMMAND_ID_HANDLER(ID_VIEW_PAUSE, OnPause)
    COMMAND_ID_HANDLER(ID_RUNPAUSE, OnPause)
    COMMAND_ID_HANDLER(ID_INT_1, OnInterval)
    COMMAND_ID_HANDLER(ID_INT_2, OnInterval)
    COMMAND_ID_HANDLER(ID_INT_5, OnInterval)
    COMMAND_ID_HANDLER(ID_INT_10, OnInterval)
    COMMAND_ID_HANDLER(ID_ALWAYSONTOP, OnAlwaysOnTop)
    COMMAND_ID_HANDLER(ID_FONT, OnChooseFont)
    COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();
    void BuildImageLists();
    static void addStock(CImageList& il, SHSTOCKICONID id);
    void BuildListColumns();

    // ---- layout ---------------------------------------------------------
    int bandHeight(HWND h);
    void OnSize(UINT, CSize);
    CRect splitterRect();
    BOOL OnSetCursor(CWindow wnd, UINT hitTest, UINT);
    void OnLButtonDown(UINT, CPoint pt);
    void OnMouseMove(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);
    void OnTimer(UINT_PTR id);

    // ---- tree build -----------------------------------------------------
    void BuildTree();
    HTREEITEM InsertNode(HTREEITEM parent, const std::wstring& label, const std::wstring& path);
    void AddChildren(HTREEITEM parent, const std::wstring& path, int depth);
    std::wstring pathOf(HTREEITEM h);

    // ---- list load ------------------------------------------------------
    void LoadList(const std::wstring& path);
    void SortList();
    LRESULT OnTreeSelChanged(int, LPNMHDR p, BOOL&);
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&);
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);
    LRESULT OnDblClk(int, LPNMHDR p, BOOL&);
    LRESULT OnListKeyDown(int, LPNMHDR p, BOOL&);
    void NavigateInto(const std::wstring& name);
    LRESULT OnRClick(int, LPNMHDR p, BOOL&);

    // ---- status ---------------------------------------------------------
    void UpdateStatus();

    // ---- selection helpers ----------------------------------------------
    const nt::ObjEntry* selectedEntry();
    std::wstring fullPathOf(const nt::ObjEntry& e);
    void ShowProps();

    // ---- quick find -----------------------------------------------------
    void ShowFindBar(bool show);
    LRESULT OnFindChange(WORD, WORD id, HWND, BOOL& handled);

    // ---- clipboard ------------------------------------------------------
    void CopyText(const std::wstring& text);

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopyName(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopyFull(WORD, WORD, HWND, BOOL&);
    LRESULT OnCopySymlink(WORD, WORD, HWND, BOOL&);
    LRESULT OnFind(WORD, WORD, HWND, BOOL&);
    LRESULT OnExpandAll(WORD, WORD, HWND, BOOL&);
    LRESULT OnCollapseAll(WORD, WORD, HWND, BOOL&);
    void ExpandRecursive(HTREEITEM h, UINT action);
    void SetListView(DWORD style);
    LRESULT OnViewDetails(WORD, WORD, HWND, BOOL&);
    LRESULT OnViewLarge(WORD, WORD, HWND, BOOL&);
    LRESULT OnViewSmall(WORD, WORD, HWND, BOOL&);
    LRESULT OnViewList(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleToolbar(WORD, WORD, HWND, BOOL&);
    LRESULT OnToggleStatusbar(WORD, WORD, HWND, BOOL&);
    LRESULT OnPause(WORD, WORD, HWND, BOOL&);
    LRESULT OnInterval(WORD, WORD id, HWND, BOOL&);
    LRESULT OnAlwaysOnTop(WORD, WORD, HWND, BOOL&);
    LRESULT OnChooseFont(WORD, WORD, HWND, BOOL&);
    void ApplyFont();
    void SyncMenu();

    // ---- config ---------------------------------------------------------
    static std::wstring IniPath();
    void LoadConfig();
    void SaveConfig();
    void OnDestroy();
};
