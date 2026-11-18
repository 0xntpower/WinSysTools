// MainFrame.cpp -- the WinObj main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the namespace walk that fills the tree and list, the
// draggable splitter, the quick-find band, the clipboard and view commands, the
// config load and save, and the toolbar-light fix (PinToolbarLight after the
// toolbar is made, OnEraseBkgnd filling the frame with button-face).
#include "MainFrame.h"

#include "NtObjMgr.h"
#include "Security.h"
#include "Dialogs.h"
#include "../common/UiUtil.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// ---- message filter -----------------------------------------------------
BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
    if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg))
    {
        return TRUE;
    }
    if (m_findVisible && pMsg->hwnd == m_find.m_hWnd && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE)
    {
        ShowFindBar(false);
        return TRUE;
    }
    return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
}

LRESULT CMainFrame::OnEraseBkgnd(CDCHandle dc)
{
    return FillFrameBtnFace(m_hWnd, dc);
}

// ---- creation -----------------------------------------------------------
int CMainFrame::OnCreate(LPCREATESTRUCT)
{
    if (!m_noSaveLoad)
    {
        LoadConfig();
    }

    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

    CreateToolbar();

    m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"Ready", m_hWnd, IDC_STATUSBAR);
    int parts[] = {500, -1};
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));

    m_tree.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                      TVS_SHOWSELALWAYS,
                  0, IDC_TREE);
    m_list.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, IDC_LIST);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

    m_find.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_FIND_EDIT);
    m_find.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
    m_find.SetCueBannerText(L"Quick Find");

    BuildImageLists();
    BuildListColumns();

    SetWindowTextW(nt::IsElevated() ? L"WinObj - (Administrator)" : L"WinObj");
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_WINOBJ)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    if (m_haveFont)
    {
        ApplyFont();
    }

    if (m_alwaysOnTop)
    {
        SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    if (m_havePlacement)
    {
        SetWindowPlacement(&m_placement);
    }

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    BuildTree();
    SyncMenu();

    if (!m_paused)
    {
        SetTimer(kRefreshTimer, m_interval * 1000);
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

    CImageList il;
    il.Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 2);
    const int ids[] = {IDI_TB_REFRESH, IDI_TB_PROPERTIES, IDI_TB_FIND};
    for (int id : ids)
    {
        if (HICON ic = static_cast<HICON>(::LoadImageW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(id),
                                                       IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR)))
        {
            il.AddIcon(ic);
        }
    }
    tb.SetImageList(il.Detach());

    struct
    {
        int bmp, cmd;
    } btns[] = {
        {0, ID_VIEW_REFRESH}, {-1, 0}, {1, ID_PROPERTIES}, {-1, 0}, {2, ID_EDIT_FIND},
    };
    std::vector<TBBUTTON> b;
    for (auto& x : btns)
    {
        TBBUTTON t{};
        if (x.bmp < 0)
        {
            t.fsStyle = TBSTYLE_SEP;
        }
        else
        {
            t.iBitmap = x.bmp;
            t.idCommand = x.cmd;
            t.fsState = TBSTATE_ENABLED;
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
    m_listImages.Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 2);
    addStock(m_listImages, SIID_APPLICATION); // 0 generic object
    addStock(m_listImages, SIID_FOLDER);      // 1 directory
    addStock(m_listImages, SIID_LINK);        // 2 symbolic link
    m_list.SetImageList(m_listImages, LVSIL_SMALL);

    m_treeImages.Create(16, 16, ILC_COLOR32 | ILC_MASK, 2, 0);
    addStock(m_treeImages, SIID_FOLDER);     // 0 closed
    addStock(m_treeImages, SIID_FOLDEROPEN); // 1 open
    m_tree.SetImageList(m_treeImages, TVSIL_NORMAL);
}

void CMainFrame::addStock(CImageList& il, SHSTOCKICONID id)
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
    m_list.InsertColumn(0, L"Name", LVCFMT_LEFT, 220);
    m_list.InsertColumn(1, L"Type", LVCFMT_LEFT, 150);
    m_list.InsertColumn(2, L"Symbolic Link Target", LVCFMT_LEFT, 320);
}

// ---- layout -------------------------------------------------------------
int CMainFrame::bandHeight(HWND h)
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
    int top = bandHeight(m_hWndToolBar);
    int bottom = rc.bottom - bandHeight(m_hWndStatusBar);

    if (m_findVisible)
    {
        m_find.SetWindowPos(nullptr, 0, top, rc.right, kFindH, SWP_NOZORDER);
        top += kFindH;
    }

    int usable = bottom - top;
    if (usable < 40)
    {
        return;
    }

    int treeW = std::clamp(m_treeW, 80, static_cast<int>(rc.right) - 120);
    m_treeW = treeW;

    m_tree.SetWindowPos(nullptr, 0, top, treeW, usable, SWP_NOZORDER);
    int listX = treeW + kSplitBar;
    m_list.SetWindowPos(nullptr, listX, top, rc.right - listX, usable, SWP_NOZORDER);
}

CRect CMainFrame::splitterRect()
{
    CRect rc;
    GetClientRect(&rc);
    int top = bandHeight(m_hWndToolBar) + (m_findVisible ? kFindH : 0);
    int bottom = rc.bottom - bandHeight(m_hWndStatusBar);
    return CRect(m_treeW, top, m_treeW + kSplitBar, bottom);
}

BOOL CMainFrame::OnSetCursor(CWindow wnd, UINT hitTest, UINT)
{
    if (wnd.m_hWnd == m_hWnd && hitTest == HTCLIENT)
    {
        POINT pt;
        ::GetCursorPos(&pt);
        ScreenToClient(&pt);
        if (splitterRect().PtInRect(pt))
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
    if (splitterRect().PtInRect(pt))
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

void CMainFrame::OnTimer(UINT_PTR id)
{
    if (id == kRefreshTimer && !m_paused)
    {
        LoadList(m_curPath);
    }
}

// ---- tree build ---------------------------------------------------------
void CMainFrame::BuildTree()
{
    ScopedWait wc;
    m_tree.SetRedraw(FALSE);
    m_tree.DeleteAllItems();
    m_treePaths.clear();
    m_treePaths.reserve(4096);

    HTREEITEM root = InsertNode(TVI_ROOT, L"\\", L"\\");
    AddChildren(root, L"\\", 0);
    m_tree.Expand(root);
    m_tree.SetRedraw(TRUE);
    m_tree.SelectItem(root);
}

HTREEITEM CMainFrame::InsertNode(HTREEITEM parent, const std::wstring& label, const std::wstring& path)
{
    size_t idx = m_treePaths.size();
    m_treePaths.push_back(path);

    TVINSERTSTRUCTW tis{};
    tis.hParent = parent;
    tis.hInsertAfter = TVI_SORT;
    tis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tis.item.pszText = const_cast<LPWSTR>(label.c_str());
    tis.item.lParam = static_cast<LPARAM>(idx);
    tis.item.iImage = 0;
    tis.item.iSelectedImage = 1;
    return m_tree.InsertItem(&tis);
}

void CMainFrame::AddChildren(HTREEITEM parent, const std::wstring& path, int depth)
{
    if (depth > 32)
    {
        return;
    }
    std::vector<nt::ObjEntry> entries = nt::EnumerateDirectory(path, false);
    for (const nt::ObjEntry& e : entries)
    {
        if (!e.isDirectory)
        {
            continue;
        }
        std::wstring childPath = (path == L"\\") ? (L"\\" + e.name) : (path + L"\\" + e.name);
        HTREEITEM h = InsertNode(parent, e.name, childPath);
        AddChildren(h, childPath, depth + 1);
    }
}

std::wstring CMainFrame::pathOf(HTREEITEM h)
{
    if (!h)
    {
        return std::wstring();
    }
    size_t idx = static_cast<size_t>(m_tree.GetItemData(h));
    return (idx < m_treePaths.size()) ? m_treePaths[idx] : std::wstring();
}

// ---- list load ----------------------------------------------------------
void CMainFrame::LoadList(const std::wstring& path)
{
    ScopedWait wc;
    m_curPath = path;
    m_listEntries = nt::EnumerateDirectory(path, true);
    SortList();
    m_list.SetItemCount(static_cast<int>(m_listEntries.size()));
    m_list.Invalidate();
    UpdateStatus();
}

void CMainFrame::SortList()
{
    int col = m_sortCol;
    bool asc = m_sortAsc;
    std::stable_sort(m_listEntries.begin(), m_listEntries.end(),
                     [col, asc](const nt::ObjEntry& a, const nt::ObjEntry& b)
                     {
                         const std::wstring& ka = (col == 1) ? a.type : (col == 2) ? a.symlink : a.name;
                         const std::wstring& kb = (col == 1) ? b.type : (col == 2) ? b.symlink : b.name;
                         int c = _wcsicmp(ka.c_str(), kb.c_str());
                         return asc ? (c < 0) : (c > 0);
                     });
}

LRESULT CMainFrame::OnTreeSelChanged(int, LPNMHDR p, BOOL&)
{
    auto* nmtv = reinterpret_cast<LPNMTREEVIEWW>(p);
    std::wstring path = pathOf(nmtv->itemNew.hItem);
    if (!path.empty())
    {
        LoadList(path);
    }
    return 0;
}

LRESULT CMainFrame::OnGetDispInfo(int, LPNMHDR p, BOOL&)
{
    auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
    int item = di->item.iItem;
    if (item < 0 || item >= static_cast<int>(m_listEntries.size()))
    {
        return 0;
    }
    const nt::ObjEntry& e = m_listEntries[item];

    if (di->item.mask & LVIF_TEXT)
    {
        static std::wstring text;
        switch (di->item.iSubItem)
        {
        case 0:
            text = e.name;
            break;
        case 1:
            text = e.type;
            break;
        case 2:
            text = e.symlink;
            break;
        default:
            text.clear();
            break;
        }
        di->item.pszText = const_cast<LPWSTR>(text.c_str());
    }
    if (di->item.mask & LVIF_IMAGE)
    {
        di->item.iImage = e.isDirectory ? 1 : (_wcsicmp(e.type.c_str(), L"SymbolicLink") == 0 ? 2 : 0);
    }
    return 0;
}

LRESULT CMainFrame::OnColumnClick(int, LPNMHDR p, BOOL&)
{
    auto* nv = reinterpret_cast<LPNMLISTVIEW>(p);
    int col = nv->iSubItem;
    if (col == m_sortCol)
    {
        m_sortAsc = !m_sortAsc;
    }
    else
    {
        m_sortCol = col;
        m_sortAsc = true;
    }
    SortList();
    m_list.Invalidate();
    return 0;
}

LRESULT CMainFrame::OnDblClk(int, LPNMHDR p, BOOL&)
{
    if (p->idFrom == IDC_LIST)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i >= 0 && i < static_cast<int>(m_listEntries.size()))
        {
            if (m_listEntries[i].isDirectory)
            {
                NavigateInto(m_listEntries[i].name);
            }
            else
            {
                ShowProps();
            }
        }
    }
    return 0;
}

LRESULT CMainFrame::OnListKeyDown(int, LPNMHDR p, BOOL&)
{
    auto* kd = reinterpret_cast<LPNMLVKEYDOWN>(p);
    if (kd->wVKey == VK_RETURN)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i >= 0 && i < static_cast<int>(m_listEntries.size()) && m_listEntries[i].isDirectory)
        {
            NavigateInto(m_listEntries[i].name);
        }
    }
    return 0;
}

void CMainFrame::NavigateInto(const std::wstring& name)
{
    HTREEITEM sel = m_tree.GetSelectedItem();
    if (!sel)
    {
        return;
    }
    for (HTREEITEM c = m_tree.GetChildItem(sel); c; c = m_tree.GetNextSiblingItem(c))
    {
        wchar_t buf[512] = L"";
        m_tree.GetItemText(c, buf, 512);
        if (_wcsicmp(buf, name.c_str()) == 0)
        {
            m_tree.Expand(sel);
            m_tree.SelectItem(c);
            m_tree.EnsureVisible(c);
            return;
        }
    }
}

LRESULT CMainFrame::OnRClick(int, LPNMHDR p, BOOL&)
{
    int sub = (p->idFrom == IDC_LIST) ? 1 : 0;
    HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CTX_MENU));
    if (!menu)
    {
        return 0;
    }
    HMENU pop = ::GetSubMenu(menu, sub);
    POINT pt;
    ::GetCursorPos(&pt);
    ::TrackPopupMenu(pop, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);
    return 0;
}

// ---- status -------------------------------------------------------------
void CMainFrame::UpdateStatus()
{
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(m_curPath.c_str()));
    wchar_t buf[64];
    swprintf(buf, 64, L"%d objects", static_cast<int>(m_listEntries.size()));
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(buf));
}

// ---- selection helpers --------------------------------------------------
const nt::ObjEntry* CMainFrame::selectedEntry()
{
    int i = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (i >= 0 && i < static_cast<int>(m_listEntries.size()))
    {
        return &m_listEntries[i];
    }
    return nullptr;
}

std::wstring CMainFrame::fullPathOf(const nt::ObjEntry& e)
{
    return (m_curPath == L"\\") ? (L"\\" + e.name) : (m_curPath + L"\\" + e.name);
}

void CMainFrame::ShowProps()
{
    // Prefer a focused list selection, otherwise fall back to the selected
    // tree node so the command always acts on something.
    if (::GetFocus() != m_tree.m_hWnd)
    {
        if (const nt::ObjEntry* e = selectedEntry())
        {
            ShowObjectProperties(m_hWnd, fullPathOf(*e), e->type);
            return;
        }
    }
    std::wstring path = pathOf(m_tree.GetSelectedItem());
    if (!path.empty())
    {
        ShowObjectProperties(m_hWnd, path, L"Directory");
    }
}

// ---- quick find ---------------------------------------------------------
void CMainFrame::ShowFindBar(bool show)
{
    m_findVisible = show;
    m_find.ShowWindow(show ? SW_SHOW : SW_HIDE);
    OnSize(0, CSize());
    if (show)
    {
        m_find.SetFocus();
    }
    else
    {
        m_list.SetFocus();
    }
}

LRESULT CMainFrame::OnFindChange(WORD, WORD id, HWND, BOOL& handled)
{
    if (id != IDC_FIND_EDIT)
    {
        handled = FALSE;
        return 0;
    }
    wchar_t buf[256] = L"";
    m_find.GetWindowTextW(buf, 256);
    std::wstring needle = buf;
    if (needle.empty())
    {
        return 0;
    }
    std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
    for (size_t i = 0; i < m_listEntries.size(); ++i)
    {
        std::wstring n = m_listEntries[i].name;
        std::transform(n.begin(), n.end(), n.begin(), towlower);
        if (n.find(needle) != std::wstring::npos)
        {
            m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            m_list.SetItemState(static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            m_list.EnsureVisible(static_cast<int>(i), FALSE);
            break;
        }
    }
    return 0;
}

// ---- clipboard ----------------------------------------------------------
void CMainFrame::CopyText(const std::wstring& text)
{
    if (!::OpenClipboard(m_hWnd))
    {
        return;
    }
    ::EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
    {
        memcpy(::GlobalLock(h), text.c_str(), bytes);
        ::GlobalUnlock(h);
        ::SetClipboardData(CF_UNICODETEXT, h);
    }
    ::CloseClipboard();
}

// ---- commands -----------------------------------------------------------
LRESULT CMainFrame::OnRefresh(WORD, WORD, HWND, BOOL&)
{
    std::wstring keep = m_curPath;
    BuildTree();
    return 0;
}

LRESULT CMainFrame::OnProperties(WORD, WORD, HWND, BOOL&)
{
    ShowProps();
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

LRESULT CMainFrame::OnCopyName(WORD, WORD, HWND, BOOL&)
{
    if (::GetFocus() == m_tree.m_hWnd)
    {
        wchar_t buf[512] = L"";
        if (HTREEITEM h = m_tree.GetSelectedItem())
        {
            m_tree.GetItemText(h, buf, 512);
        }
        CopyText(buf);
    }
    else if (const nt::ObjEntry* e = selectedEntry())
    {
        CopyText(e->name);
    }
    return 0;
}

LRESULT CMainFrame::OnCopyFull(WORD, WORD, HWND, BOOL&)
{
    if (::GetFocus() == m_tree.m_hWnd)
    {
        CopyText(pathOf(m_tree.GetSelectedItem()));
    }
    else if (const nt::ObjEntry* e = selectedEntry())
    {
        CopyText(fullPathOf(*e));
    }
    return 0;
}

LRESULT CMainFrame::OnCopySymlink(WORD, WORD, HWND, BOOL&)
{
    if (const nt::ObjEntry* e = selectedEntry())
    {
        CopyText(e->symlink);
    }
    return 0;
}

LRESULT CMainFrame::OnFind(WORD, WORD, HWND, BOOL&)
{
    ShowFindBar(!m_findVisible || ::GetFocus() != m_find.m_hWnd ? true : false);
    return 0;
}

LRESULT CMainFrame::OnExpandAll(WORD, WORD, HWND, BOOL&)
{
    if (HTREEITEM h = m_tree.GetSelectedItem())
    {
        ExpandRecursive(h, TVE_EXPAND);
    }
    return 0;
}

LRESULT CMainFrame::OnCollapseAll(WORD, WORD, HWND, BOOL&)
{
    if (HTREEITEM h = m_tree.GetSelectedItem())
    {
        ExpandRecursive(h, TVE_COLLAPSE);
    }
    return 0;
}

void CMainFrame::ExpandRecursive(HTREEITEM h, UINT action)
{
    for (HTREEITEM c = m_tree.GetChildItem(h); c; c = m_tree.GetNextSiblingItem(c))
    {
        ExpandRecursive(c, action);
    }
    m_tree.Expand(h, action);
}

void CMainFrame::SetListView(DWORD style)
{
    DWORD cur = m_list.GetWindowLong(GWL_STYLE);
    cur = (cur & ~LVS_TYPEMASK) | style;
    m_list.SetWindowLong(GWL_STYLE, cur);
}

LRESULT CMainFrame::OnViewDetails(WORD, WORD, HWND, BOOL&)
{
    SetListView(LVS_REPORT);
    return 0;
}

LRESULT CMainFrame::OnViewLarge(WORD, WORD, HWND, BOOL&)
{
    SetListView(LVS_ICON);
    return 0;
}

LRESULT CMainFrame::OnViewSmall(WORD, WORD, HWND, BOOL&)
{
    SetListView(LVS_SMALLICON);
    return 0;
}

LRESULT CMainFrame::OnViewList(WORD, WORD, HWND, BOOL&)
{
    SetListView(LVS_LIST);
    return 0;
}

LRESULT CMainFrame::OnToggleToolbar(WORD, WORD, HWND, BOOL&)
{
    ::ShowWindow(m_hWndToolBar, ::IsWindowVisible(m_hWndToolBar) ? SW_HIDE : SW_SHOW);
    OnSize(0, CSize());
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnToggleStatusbar(WORD, WORD, HWND, BOOL&)
{
    ::ShowWindow(m_hWndStatusBar, ::IsWindowVisible(m_hWndStatusBar) ? SW_HIDE : SW_SHOW);
    OnSize(0, CSize());
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnPause(WORD, WORD, HWND, BOOL&)
{
    m_paused = !m_paused;
    if (m_paused)
    {
        KillTimer(kRefreshTimer);
    }
    else
    {
        SetTimer(kRefreshTimer, m_interval * 1000);
    }
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnInterval(WORD, WORD id, HWND, BOOL&)
{
    m_interval = (id == ID_INT_1) ? 1 : (id == ID_INT_2) ? 2 : (id == ID_INT_5) ? 5 : 10;
    if (!m_paused)
    {
        SetTimer(kRefreshTimer, m_interval * 1000);
    }
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnAlwaysOnTop(WORD, WORD, HWND, BOOL&)
{
    m_alwaysOnTop = !m_alwaysOnTop;
    SetWindowPos(m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnChooseFont(WORD, WORD, HWND, BOOL&)
{
    LOGFONTW lf = m_logFont;
    if (!m_haveFont)
    {
        ::GetObjectW(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)), sizeof(lf), &lf);
    }
    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = m_hWnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
    if (::ChooseFontW(&cf))
    {
        m_logFont = lf;
        m_haveFont = true;
        ApplyFont();
    }
    return 0;
}

void CMainFrame::ApplyFont()
{
    m_font = ::CreateFontIndirectW(&m_logFont);
    m_tree.SetFont(m_font);
    m_list.SetFont(m_font);
}

void CMainFrame::SyncMenu()
{
    CMenuHandle menu = GetMenu();
    if (menu.IsNull())
    {
        return;
    }
    auto chk = [&](UINT id, bool on)
    {
        menu.CheckMenuItem(id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };
    chk(ID_VIEW_TOOLBAR, ::IsWindowVisible(m_hWndToolBar) != FALSE);
    chk(ID_VIEW_STATUSBAR, ::IsWindowVisible(m_hWndStatusBar) != FALSE);
    chk(ID_VIEW_PAUSE, m_paused);
    chk(ID_ALWAYSONTOP, m_alwaysOnTop);
    menu.CheckMenuRadioItem(ID_INT_1, ID_INT_10,
                            m_interval == 1   ? ID_INT_1
                            : m_interval == 2 ? ID_INT_2
                            : m_interval == 5 ? ID_INT_5
                                              : ID_INT_10,
                            MF_BYCOMMAND);
}

// ---- config -------------------------------------------------------------
std::wstring CMainFrame::IniPath()
{
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p = exe;
    size_t slash = p.rfind(L'\\');
    if (slash != std::wstring::npos)
    {
        p.resize(slash + 1);
    }
    return p + L"WinObj.ini";
}

void CMainFrame::LoadConfig()
{
    std::wstring ini = IniPath();
    const wchar_t* f = ini.c_str();
    m_treeW = static_cast<int>(::GetPrivateProfileIntW(L"Settings", L"TreeWidth", 240, f));
    m_alwaysOnTop = ::GetPrivateProfileIntW(L"Settings", L"AlwaysOnTop", 0, f) != 0;
    m_interval = static_cast<UINT>(::GetPrivateProfileIntW(L"Settings", L"Interval", 5, f));
    WINDOWPLACEMENT wp{};
    if (::GetPrivateProfileStructW(L"Settings", L"WinPos", &wp, sizeof(wp), f) && wp.length == sizeof(wp))
    {
        m_placement = wp;
        m_havePlacement = true;
    }
}

void CMainFrame::SaveConfig()
{
    std::wstring ini = IniPath();
    const wchar_t* f = ini.c_str();
    auto si = [&](const wchar_t* n, int v)
    {
        wchar_t b[16];
        swprintf(b, 16, L"%d", v);
        ::WritePrivateProfileStringW(L"Settings", n, b, f);
    };
    si(L"TreeWidth", m_treeW);
    si(L"AlwaysOnTop", m_alwaysOnTop);
    si(L"Interval", static_cast<int>(m_interval));
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
