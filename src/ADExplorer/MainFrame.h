// MainFrame.h -- the Active Directory Explorer main window.
//
// The original is a modeless MAIN dialog (sub_14001B780) that hangs a toolbar,
// a path edit, a status bar and a tree/list splitter off a plain frame. The tree
// on the left mirrors the directory hierarchy from the naming contexts in the
// rootDSE, the list on the right shows the selected object's attributes with the
// columns Attribute, Syntax, Count and Value. This rebuilds that over the ad::
// browse engine, pinned to light mode with the shared toolbar fix so the band
// reads light on a dark-mode system.
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
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "AdClient.h"
#include "Dialogs.h"

extern CAppModule _Module;

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
    DECLARE_FRAME_WND_CLASS(L"Active Directory Explorer Main", IDR_MAIN_MENU)

    struct TreeNode
    {
        std::wstring dn; // empty for the connection root
        bool populated = false;
        bool isRoot = false;
    };

    CTreeViewCtrl m_tree;
    CListViewCtrl m_list;
    CEdit m_path;

    ad::Client m_client;
    bool m_connected = false;

    std::vector<TreeNode> m_nodes;      // tree lParam -> node
    std::vector<ad::Attribute> m_attrs; // current object attributes
    std::wstring m_curDn;

    int m_treeW = 240;
    bool m_dragging = false;
    static const int kSplitBar = 5;
    static const int kPathH = 22;

    int m_sortCol = 0;
    bool m_sortAsc = true;

    HACCEL m_hAccel = nullptr;
    CImageList m_treeImages;

    bool m_noSaveLoad = false;
    bool m_havePlacement = false;
    WINDOWPLACEMENT m_placement{};

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
    NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDINGW, OnItemExpanding)
    NOTIFY_CODE_HANDLER(TVN_SELCHANGEDW, OnTreeSelChanged)
    NOTIFY_CODE_HANDLER(LVN_COLUMNCLICK, OnColumnClick)
    NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDblClk)
    NOTIFY_CODE_HANDLER(NM_RCLICK, OnRClick)
    COMMAND_ID_HANDLER(ID_FILE_CONNECT, OnConnect)
    COMMAND_ID_HANDLER(ID_SEARCH_CONTAINER, OnSearch)
    COMMAND_ID_HANDLER(ID_EDIT_PROPERTIES, OnProperties)
    COMMAND_ID_HANDLER(ID_APP_ABOUT, OnAbout)
    COMMAND_ID_HANDLER(ID_APP_EXIT, OnExit)
    COMMAND_ID_HANDLER(ID_FILE_REMOVE, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_FILE_SNAPSHOT, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_FAVORITES_ADD, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_COMPARE_SNAPSHOT, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_COMPARE_SECURITY, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_HISTORY_FORWARD, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_HISTORY_BACK, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_HISTORY_ALL, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_HELP_CONTENTS, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_OBJ_RENAME, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_OBJ_DELETE, OnOutOfScope)
    COMMAND_ID_HANDLER(ID_REFRESH, OnRefresh)
    CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc);

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT);
    void CreateToolbar();
    void BuildImageLists();
    static void AddStock(CImageList& il, SHSTOCKICONID id);
    void BuildListColumns();

    // ---- connect and tree ----------------------------------------------
    LRESULT OnConnect(WORD, WORD, HWND, BOOL&);
    void SeedTree(const std::wstring& server);
    HTREEITEM InsertNode(HTREEITEM parent, const std::wstring& label, const TreeNode& node, int image);
    void AddDummy(HTREEITEM parent);
    TreeNode* NodeOf(HTREEITEM h);
    LRESULT OnItemExpanding(int, LPNMHDR p, BOOL&);
    void RemoveDummies(HTREEITEM parent);
    LRESULT OnTreeSelChanged(int, LPNMHDR p, BOOL&);
    void LoadAttributes(const std::wstring& dn);
    void FillList();
    void SortAttributes();
    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&);

    // ---- commands -------------------------------------------------------
    LRESULT OnSearch(WORD, WORD, HWND, BOOL&);
    LRESULT OnProperties(WORD, WORD, HWND, BOOL&);
    LRESULT OnDblClk(int, LPNMHDR p, BOOL&);
    void ShowAttrProps();
    LRESULT OnRClick(int, LPNMHDR, BOOL&);
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&);
    LRESULT OnAbout(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);
    LRESULT OnOutOfScope(WORD, WORD, HWND, BOOL&);
    bool Require();
    void UpdateStatus();

    // ---- layout ---------------------------------------------------------
    int BandHeight(HWND h);
    void OnSize(UINT, CSize);
    CRect SplitterRect();
    BOOL OnSetCursor(CWindow wnd, UINT hitTest, UINT);
    void OnLButtonDown(UINT, CPoint pt);
    void OnMouseMove(UINT, CPoint pt);
    void OnLButtonUp(UINT, CPoint);

    // ---- config ---------------------------------------------------------
    static std::wstring IniPath();
    void LoadConfig();
    void SaveConfig();
    void OnDestroy();
};
