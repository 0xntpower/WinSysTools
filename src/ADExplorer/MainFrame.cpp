// MainFrame.cpp -- the Active Directory Explorer main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the connect flow that seeds the tree from the naming
// contexts, the lazy child enumeration on expand, the attribute read and list
// fill, the draggable splitter, the config load and save, and the toolbar-light
// fix (PinToolbarLight after the toolbar is made, OnEraseBkgnd filling the frame
// with button-face).
#include "MainFrame.h"

#include "AdClient.h"
#include "Dialogs.h"
#include "../common/UiUtil.h"

#include <algorithm>
#include <string>
#include <vector>

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
    if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg))
    {
        return TRUE;
    }
    return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
}

LRESULT CMainFrame::OnEraseBkgnd(CDCHandle dc)
{
    return FillFrameBtnFace(m_hWnd, dc);
}

// ---- creation -------------------------------------------------------
int CMainFrame::OnCreate(LPCREATESTRUCT)
{
    if (!m_noSaveLoad)
    {
        LoadConfig();
    }

    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

    CreateToolbar();

    m_hWndStatusBar =
        ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"Not connected", m_hWnd, IDC_STATUSBAR);
    int parts[] = {520, -1};
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));

    m_path.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE,
                  IDC_PATH);
    m_path.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));

    m_tree.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                      TVS_SHOWSELALWAYS,
                  0, IDC_TREE);
    m_list.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, IDC_LIST);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

    BuildImageLists();
    BuildListColumns();

    SetWindowTextW(L"Active Directory Explorer - Sysinternals: www.sysinternals.com");
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APPICON)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    if (m_havePlacement)
    {
        SetWindowPlacement(&m_placement);
    }

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    return 0;
}

void CMainFrame::CreateToolbar()
{
    CToolBarCtrl tb;
    tb.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER, 0,
              IDC_TOOLBAR);
    tb.SetButtonStructSize(sizeof(TBBUTTON));
    PinToolbarLight(tb);
    tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));

    struct Def
    {
        int bmp;
        int cmd;
        bool enabled;
    };
    const Def defs[] = {
        {TB_IMG_CONNECT, ID_FILE_CONNECT, true},
        {-1, 0, false},
        {TB_IMG_SEARCH, ID_SEARCH_CONTAINER, true},
        {TB_IMG_COMPARE, ID_COMPARE_SNAPSHOT, true},
        {-1, 0, false},
        {TB_IMG_SNAPSHOT, ID_FILE_SNAPSHOT, true},
        {-1, 0, false},
        {TB_IMG_PROPERTIES, ID_EDIT_PROPERTIES, true},
        {-1, 0, false},
        {TB_IMG_BACK, ID_HISTORY_BACK, false},
        {TB_IMG_FORWARD, ID_HISTORY_FORWARD, false},
        {TB_IMG_HISTORY, ID_HISTORY_ALL, false},
    };
    std::vector<TBBUTTON> b;
    for (const Def& d : defs)
    {
        TBBUTTON t{};
        if (d.bmp < 0)
        {
            t.fsStyle = TBSTYLE_SEP;
        }
        else
        {
            t.iBitmap = d.bmp;
            t.idCommand = d.cmd;
            t.fsState = d.enabled ? TBSTATE_ENABLED : 0;
            t.fsStyle = TBSTYLE_BUTTON;
        }
        b.push_back(t);
    }
    tb.AddButtons(static_cast<int>(b.size()), b.data());
    tb.AutoSize();
    m_hWndToolBar = tb;
}

void CMainFrame::BuildImageLists()
{
    m_treeImages.Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 0);
    AddStock(m_treeImages, SIID_WORLD);      // 0 connection root
    AddStock(m_treeImages, SIID_FOLDER);     // 1 container
    AddStock(m_treeImages, SIID_FOLDEROPEN); // 2 container open
    m_tree.SetImageList(m_treeImages, TVSIL_NORMAL);
}

void CMainFrame::AddStock(CImageList& il, SHSTOCKICONID id)
{
    SHSTOCKICONINFO sii{};
    sii.cbSize = sizeof(sii);
    if (SUCCEEDED(::SHGetStockIconInfo(id, SHGSI_ICON | SHGSI_SMALLICON, &sii)) && sii.hIcon)
    {
        il.AddIcon(sii.hIcon);
        ::DestroyIcon(sii.hIcon);
    }
}

void CMainFrame::BuildListColumns()
{
    m_list.InsertColumn(0, L"Attribute", LVCFMT_LEFT, 200);
    m_list.InsertColumn(1, L"Syntax", LVCFMT_LEFT, 150);
    m_list.InsertColumn(2, L"Count", LVCFMT_RIGHT, 50);
    m_list.InsertColumn(3, L"Value", LVCFMT_LEFT, 360);
}

// ---- connect and tree ----------------------------------------------
LRESULT CMainFrame::OnConnect(WORD, WORD, HWND, BOOL&)
{
    CConnectDlg dlg;
    if (dlg.DoModal(m_hWnd) != IDOK)
    {
        return 0;
    }

    ScopedWait wc;
    std::wstring error;
    ad::Client fresh;
    if (!fresh.Connect(dlg.m_server, dlg.m_user, dlg.m_password, error))
    {
        ::MessageBoxW(m_hWnd, error.empty() ? L"Unable to connect to the directory." : error.c_str(),
                      L"Active Directory Explorer", MB_OK | MB_ICONERROR);
        return 0;
    }
    m_client = std::move(fresh);
    m_connected = true;
    SeedTree(dlg.m_server);
    return 0;
}

void CMainFrame::SeedTree(const std::wstring& server)
{
    m_tree.SetRedraw(FALSE);
    m_tree.DeleteAllItems();
    m_nodes.clear();

    std::wstring rootLabel = m_client.ServerName();
    if (rootLabel.empty())
    {
        rootLabel = server.empty() ? L"Active Directory" : server;
    }

    HTREEITEM root = InsertNode(TVI_ROOT, rootLabel, TreeNode{L"", true, true}, 0);
    for (const std::wstring& nc : m_client.NamingContexts())
    {
        HTREEITEM child = InsertNode(root, nc, TreeNode{nc, false, false}, 1);
        AddDummy(child);
    }
    m_tree.Expand(root);
    m_tree.SetRedraw(TRUE);
    m_tree.SelectItem(root);
}

HTREEITEM CMainFrame::InsertNode(HTREEITEM parent, const std::wstring& label, const TreeNode& node, int image)
{
    const size_t idx = m_nodes.size();
    m_nodes.push_back(node);

    TVINSERTSTRUCTW tis{};
    tis.hParent = parent;
    tis.hInsertAfter = TVI_SORT;
    tis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tis.item.pszText = const_cast<LPWSTR>(label.c_str());
    tis.item.lParam = static_cast<LPARAM>(idx);
    tis.item.iImage = image;
    tis.item.iSelectedImage = (image == 1) ? 2 : image;
    return m_tree.InsertItem(&tis);
}

void CMainFrame::AddDummy(HTREEITEM parent)
{
    TVINSERTSTRUCTW tis{};
    tis.hParent = parent;
    tis.hInsertAfter = TVI_LAST;
    tis.item.mask = TVIF_TEXT | TVIF_PARAM;
    tis.item.pszText = const_cast<LPWSTR>(L"");
    tis.item.lParam = static_cast<LPARAM>(-1);
    m_tree.InsertItem(&tis);
}

CMainFrame::TreeNode* CMainFrame::NodeOf(HTREEITEM h)
{
    if (!h)
    {
        return nullptr;
    }
    const LPARAM idx = m_tree.GetItemData(h);
    if (idx < 0 || static_cast<size_t>(idx) >= m_nodes.size())
    {
        return nullptr;
    }
    return &m_nodes[static_cast<size_t>(idx)];
}

LRESULT CMainFrame::OnItemExpanding(int, LPNMHDR p, BOOL&)
{
    auto* nmtv = reinterpret_cast<LPNMTREEVIEWW>(p);
    if (nmtv->action != TVE_EXPAND)
    {
        return 0;
    }
    TreeNode* node = NodeOf(nmtv->itemNew.hItem);
    if (node == nullptr || node->populated)
    {
        return 0;
    }
    node->populated = true;

    ScopedWait wc;
    RemoveDummies(nmtv->itemNew.hItem);
    std::vector<ad::Child> kids = m_client.EnumerateChildren(node->dn);
    for (const ad::Child& c : kids)
    {
        HTREEITEM child = InsertNode(nmtv->itemNew.hItem, c.rdn, TreeNode{c.dn, false, false}, 1);
        AddDummy(child);
    }
    return 0;
}

void CMainFrame::RemoveDummies(HTREEITEM parent)
{
    for (HTREEITEM c = m_tree.GetChildItem(parent); c;)
    {
        HTREEITEM next = m_tree.GetNextSiblingItem(c);
        if (static_cast<LPARAM>(m_tree.GetItemData(c)) == static_cast<LPARAM>(-1))
        {
            m_tree.DeleteItem(c);
        }
        c = next;
    }
}

LRESULT CMainFrame::OnTreeSelChanged(int, LPNMHDR p, BOOL&)
{
    auto* nmtv = reinterpret_cast<LPNMTREEVIEWW>(p);
    TreeNode* node = NodeOf(nmtv->itemNew.hItem);
    if (node == nullptr)
    {
        return 0;
    }
    if (node->isRoot)
    {
        m_curDn.clear();
        m_attrs.clear();
        m_list.DeleteAllItems();
        m_path.SetWindowTextW(m_client.ServerName().c_str());
        UpdateStatus();
        return 0;
    }
    LoadAttributes(node->dn);
    return 0;
}

void CMainFrame::LoadAttributes(const std::wstring& dn)
{
    ScopedWait wc;
    m_curDn = dn;
    m_attrs = m_client.ReadAttributes(dn);
    m_path.SetWindowTextW(dn.c_str());
    SortAttributes();
    FillList();
    UpdateStatus();
}

void CMainFrame::FillList()
{
    m_list.SetRedraw(FALSE);
    m_list.DeleteAllItems();
    for (int i = 0; i < static_cast<int>(m_attrs.size()); ++i)
    {
        const ad::Attribute& a = m_attrs[i];
        m_list.InsertItem(i, a.name.c_str());
        m_list.SetItemText(i, 1, a.syntax.c_str());
        wchar_t count[16] = {};
        _snwprintf_s(count, _TRUNCATE, L"%d", a.count);
        m_list.SetItemText(i, 2, count);
        m_list.SetItemText(i, 3, a.value.c_str());
    }
    m_list.SetRedraw(TRUE);
}

void CMainFrame::SortAttributes()
{
    const int col = m_sortCol;
    const bool asc = m_sortAsc;
    std::stable_sort(m_attrs.begin(), m_attrs.end(),
                     [col, asc](const ad::Attribute& a, const ad::Attribute& b)
                     {
                         int c = 0;
                         if (col == 1)
                         {
                             c = _wcsicmp(a.syntax.c_str(), b.syntax.c_str());
                         }
                         else if (col == 2)
                         {
                             c = (a.count - b.count);
                         }
                         else if (col == 3)
                         {
                             c = _wcsicmp(a.value.c_str(), b.value.c_str());
                         }
                         else
                         {
                             c = _wcsicmp(a.name.c_str(), b.name.c_str());
                         }
                         return asc ? (c < 0) : (c > 0);
                     });
}

LRESULT CMainFrame::OnColumnClick(int, LPNMHDR p, BOOL&)
{
    auto* nv = reinterpret_cast<LPNMLISTVIEW>(p);
    if (nv->iSubItem == m_sortCol)
    {
        m_sortAsc = !m_sortAsc;
    }
    else
    {
        m_sortCol = nv->iSubItem;
        m_sortAsc = true;
    }
    SortAttributes();
    FillList();
    return 0;
}

// ---- commands -------------------------------------------------------
LRESULT CMainFrame::OnSearch(WORD, WORD, HWND, BOOL&)
{
    if (!Require())
    {
        return 0;
    }
    CSearchDlg dlg;
    dlg.m_client = &m_client;
    dlg.m_baseDn = m_curDn.empty() && !m_client.NamingContexts().empty() ? m_client.NamingContexts().front() : m_curDn;
    if (dlg.DoModal(m_hWnd) == IDOK && !dlg.m_selectedDn.empty())
    {
        LoadAttributes(dlg.m_selectedDn);
    }
    return 0;
}

LRESULT CMainFrame::OnProperties(WORD, WORD, HWND, BOOL&)
{
    ShowAttrProps();
    return 0;
}

LRESULT CMainFrame::OnDblClk(int, LPNMHDR p, BOOL&)
{
    if (p->idFrom == IDC_LIST)
    {
        ShowAttrProps();
    }
    return 0;
}

void CMainFrame::ShowAttrProps()
{
    const int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(m_attrs.size()))
    {
        return;
    }
    CAttrPropsDlg dlg;
    dlg.m_object = m_curDn;
    dlg.m_attr = m_attrs[sel];
    dlg.DoModal(m_hWnd);
}

LRESULT CMainFrame::OnRClick(int, LPNMHDR, BOOL&)
{
    HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CTX_MENU));
    if (!menu)
    {
        return 0;
    }
    HMENU pop = ::GetSubMenu(menu, 0);
    POINT pt;
    ::GetCursorPos(&pt);
    ::TrackPopupMenu(pop, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);
    return 0;
}

LRESULT CMainFrame::OnRefresh(WORD, WORD, HWND, BOOL&)
{
    if (m_connected && !m_curDn.empty())
    {
        LoadAttributes(m_curDn);
    }
    return 0;
}

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&)
{
    CAboutDlg().DoModal(m_hWnd);
    return 0;
}
LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&)
{
    PostMessage(WM_CLOSE);
    return 0;
}

LRESULT CMainFrame::OnOutOfScope(WORD, WORD, HWND, BOOL&)
{
    ::MessageBoxW(m_hWnd,
                  L"This command is out of scope in this recreation, which reproduces the "
                  L"browse and read path. Snapshots, compare, favorites, history, create, "
                  L"delete and modify are not implemented.",
                  L"Active Directory Explorer", MB_OK | MB_ICONINFORMATION);
    return 0;
}

bool CMainFrame::Require()
{
    if (m_connected)
    {
        return true;
    }
    ::MessageBoxW(m_hWnd, L"Connect to a directory first (File, Connect).", L"Active Directory Explorer",
                  MB_OK | MB_ICONINFORMATION);
    return false;
}

void CMainFrame::UpdateStatus()
{
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(m_curDn.c_str()));
    wchar_t buf[64] = {};
    _snwprintf_s(buf, _TRUNCATE, L"%zu attribute(s)", m_attrs.size());
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(buf));
}

// ---- layout ---------------------------------------------------------
int CMainFrame::BandHeight(HWND h)
{
    if (!h || !::IsWindowVisible(h))
    {
        return 0;
    }
    CRect rc;
    ::GetWindowRect(h, &rc);
    return rc.Height();
}

void CMainFrame::OnSize(UINT, CSize)
{
    if (m_hWndToolBar)
    {
        ::SendMessageW(m_hWndToolBar, TB_AUTOSIZE, 0, 0);
    }
    if (m_hWndStatusBar)
    {
        ::SendMessageW(m_hWndStatusBar, WM_SIZE, 0, 0);
    }

    CRect rc;
    GetClientRect(&rc);
    int top = BandHeight(m_hWndToolBar);
    const int bottom = rc.bottom - BandHeight(m_hWndStatusBar);

    if (m_path.IsWindow())
    {
        m_path.SetWindowPos(nullptr, 0, top, rc.right, kPathH, SWP_NOZORDER);
        top += kPathH;
    }

    const int usable = bottom - top;
    if (usable < 40)
    {
        return;
    }

    int treeW = std::clamp(m_treeW, 80, static_cast<int>(rc.right) - 120);
    m_treeW = treeW;
    m_tree.SetWindowPos(nullptr, 0, top, treeW, usable, SWP_NOZORDER);
    const int listX = treeW + kSplitBar;
    m_list.SetWindowPos(nullptr, listX, top, rc.right - listX, usable, SWP_NOZORDER);
}

CRect CMainFrame::SplitterRect()
{
    CRect rc;
    GetClientRect(&rc);
    const int top = BandHeight(m_hWndToolBar) + kPathH;
    const int bottom = rc.bottom - BandHeight(m_hWndStatusBar);
    return CRect(m_treeW, top, m_treeW + kSplitBar, bottom);
}

BOOL CMainFrame::OnSetCursor(CWindow wnd, UINT hitTest, UINT)
{
    if (wnd.m_hWnd == m_hWnd && hitTest == HTCLIENT)
    {
        POINT pt;
        ::GetCursorPos(&pt);
        ScreenToClient(&pt);
        if (SplitterRect().PtInRect(pt))
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEWE));
            return TRUE;
        }
    }
    SetMsgHandled(FALSE);
    return FALSE;
}

void CMainFrame::OnLButtonDown(UINT, CPoint pt)
{
    if (SplitterRect().PtInRect(pt))
    {
        m_dragging = true;
        SetCapture();
    }
    else
    {
        SetMsgHandled(FALSE);
    }
}

void CMainFrame::OnMouseMove(UINT, CPoint pt)
{
    if (!m_dragging)
    {
        SetMsgHandled(FALSE);
        return;
    }
    m_treeW = pt.x;
    OnSize(0, CSize());
}

void CMainFrame::OnLButtonUp(UINT, CPoint)
{
    if (m_dragging)
    {
        m_dragging = false;
        ReleaseCapture();
    }
    else
    {
        SetMsgHandled(FALSE);
    }
}

// ---- config ---------------------------------------------------------
std::wstring CMainFrame::IniPath()
{
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p = exe;
    const size_t slash = p.rfind(L'\\');
    if (slash != std::wstring::npos)
    {
        p.resize(slash + 1);
    }
    return p + L"ADExplorer.ini";
}

void CMainFrame::LoadConfig()
{
    const std::wstring ini = IniPath();
    const wchar_t* f = ini.c_str();
    m_treeW = static_cast<int>(::GetPrivateProfileIntW(L"Settings", L"TreeWidth", 240, f));
    WINDOWPLACEMENT wp{};
    if (::GetPrivateProfileStructW(L"Settings", L"WinPos", &wp, sizeof(wp), f) && wp.length == sizeof(wp))
    {
        m_placement = wp;
        m_havePlacement = true;
    }
}

void CMainFrame::SaveConfig()
{
    const std::wstring ini = IniPath();
    const wchar_t* f = ini.c_str();
    wchar_t b[16] = {};
    _snwprintf_s(b, _TRUNCATE, L"%d", m_treeW);
    ::WritePrivateProfileStringW(L"Settings", L"TreeWidth", b, f);
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(&wp))
    {
        ::WritePrivateProfileStructW(L"Settings", L"WinPos", &wp, sizeof(wp), f);
    }
}

void CMainFrame::OnDestroy()
{
    if (!m_noSaveLoad)
    {
        SaveConfig();
    }
    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->RemoveMessageFilter(this);
    }
    PostQuitMessage(0);
}
