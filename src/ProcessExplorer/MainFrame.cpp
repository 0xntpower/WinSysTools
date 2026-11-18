// MainFrame.cpp -- the Process Explorer main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the process-tree refresh and flatten, the new and dead
// flash bookkeeping, the custom-draw row colouring, the DLL and handle lower
// pane, the draggable splitter, the save and command handlers, the registry
// config load and save, and the toolbar-light fix (PinToolbarLight after the
// toolbar is made, OnEraseBkgnd filling the frame with button-face).
#include "MainFrame.h"

#include "ProcessData.h"
#include "NtApi.h"
#include "Modules.h"
#include "Handles.h"
#include "Dialogs.h"
#include "../common/UiUtil.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// ---- message filter -----------------------------------------------------
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

// ---- creation -----------------------------------------------------------
int CMainFrame::OnCreate(LPCREATESTRUCT)
{
    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_MAINFRAME));
    LoadConfig();

    CreateToolbar();
    CreateSimpleStatusBar();

    m_tree.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, IDC_PROCTREE);
    m_tree.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);
    BuildTreeColumns();

    m_icons.Create(16, 16, ILC_COLOR32 | ILC_MASK, 8, 16);
    AddGenericIcon();
    m_tree.SetImageList(m_icons, LVSIL_SMALL);

    m_lower.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS, 0,
                   IDC_LOWERPANE);
    m_lower.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);
    BuildLowerColumns();

    if (HICON hi = static_cast<HICON>(::LoadImageW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APPICON),
                                                   IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    SetWindowTextW(BuildTitle().c_str());

    if (m_haveFont)
    {
        ApplyFont();
    }

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    Refresh();
    SyncMenu();

    if (!m_paused)
    {
        SetTimer(kRefreshTimer, m_intervalMs);
    }
    if (m_havePlacement)
    {
        SetWindowPlacement(&m_placement);
    }
    return 0;
}

// The original appends the current user context as [COMPUTER\user].
std::wstring CMainFrame::BuildTitle()
{
    std::wstring title = L"Process Explorer - Sysinternals: www.sysinternals.com";
    wchar_t comp[256] = {};
    DWORD ccomp = 256;
    wchar_t user[256] = {};
    DWORD cuser = 256;
    if (::GetComputerNameW(comp, &ccomp) && ::GetUserNameW(user, &cuser) && user[0])
    {
        title += L" [";
        title += comp;
        title += L'\\';
        title += user;
        title += L']';
    }
    return title;
}

void CMainFrame::CreateToolbar()
{
    CToolBarCtrl tb;
    tb.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER, 0,
              IDC_TOOLBAR);
    tb.SetButtonStructSize(sizeof(TBBUTTON));
    PinToolbarLight(tb);

    CImageList il;
    il.Create(16, 16, ILC_COLOR32 | ILC_MASK, 6, 2);
    const SHSTOCKICONID stock[] = {SIID_DOCASSOC, SIID_APPLICATION, SIID_DOCASSOC, SIID_DELETE, SIID_FIND};
    for (SHSTOCKICONID id : stock)
    {
        SHSTOCKICONINFO sii{};
        sii.cbSize = sizeof(sii);
        if (SUCCEEDED(::SHGetStockIconInfo(id, SHGSI_ICON | SHGSI_SMALLICON, &sii)) && sii.hIcon)
        {
            il.AddIcon(sii.hIcon);
            ::DestroyIcon(sii.hIcon);
        }
    }
    tb.SetImageList(il.Detach());

    struct
    {
        int bmp, cmd;
    } btns[] = {
        {0, ID_FILE_SAVE}, {-1, 0}, {1, ID_VIEW_REFRESH},   {-1, 0}, {2, ID_PROC_PROPERTIES},
        {3, ID_PROC_KILL}, {-1, 0}, {4, ID_FIND_HANDLEDLL},
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

void CMainFrame::BuildTreeColumns()
{
    struct
    {
        const wchar_t* t;
        int w;
        int fmt;
    } cols[kColCount] = {
        {L"Process", 220, LVCFMT_LEFT},       {L"PID", 55, LVCFMT_RIGHT},         {L"CPU", 55, LVCFMT_RIGHT},
        {L"Private Bytes", 85, LVCFMT_RIGHT}, {L"Working Set", 85, LVCFMT_RIGHT}, {L"Description", 200, LVCFMT_LEFT},
        {L"Company Name", 180, LVCFMT_LEFT},
    };
    for (int i = 0; i < kColCount; ++i)
    {
        m_tree.InsertColumn(i, cols[i].t, cols[i].fmt, cols[i].w);
    }
}

void CMainFrame::BuildLowerColumns()
{
    while (m_lower.DeleteColumn(0))
    {
    }
    if (m_lowerMode == pe::LowerMode::Dlls)
    {
        m_lower.InsertColumn(0, L"Name", LVCFMT_LEFT, 180);
        m_lower.InsertColumn(1, L"Description", LVCFMT_LEFT, 200);
        m_lower.InsertColumn(2, L"Company Name", LVCFMT_LEFT, 170);
        m_lower.InsertColumn(3, L"Path", LVCFMT_LEFT, 320);
    }
    else
    {
        m_lower.InsertColumn(0, L"Type", LVCFMT_LEFT, 110);
        m_lower.InsertColumn(1, L"Name", LVCFMT_LEFT, 460);
        m_lower.InsertColumn(2, L"Handle", LVCFMT_RIGHT, 80);
    }
}

void CMainFrame::AddGenericIcon()
{
    SHSTOCKICONINFO sii{};
    sii.cbSize = sizeof(sii);
    if (SUCCEEDED(::SHGetStockIconInfo(SIID_APPLICATION, SHGSI_ICON | SHGSI_SMALLICON, &sii)) && sii.hIcon)
    {
        m_icons.AddIcon(sii.hIcon);
        ::DestroyIcon(sii.hIcon);
    }
}

// Icon index for a process image, cached by path.
int CMainFrame::IconFor(const std::wstring& path)
{
    if (path.empty())
    {
        return 0;
    }
    auto it = m_iconMap.find(path);
    if (it != m_iconMap.end())
    {
        return it->second;
    }
    int index = 0;
    SHFILEINFOW sfi{};
    if (::SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon)
    {
        index = m_icons.AddIcon(sfi.hIcon);
        ::DestroyIcon(sfi.hIcon);
        if (index < 0)
        {
            index = 0;
        }
    }
    m_iconMap[path] = index;
    return index;
}

// ---- refresh ------------------------------------------------------------
void CMainFrame::OnTimer(UINT_PTR id)
{
    if (id == kRefreshTimer)
    {
        Refresh();
    }
}

void CMainFrame::Refresh()
{
    std::vector<pe::ProcRow> live = m_engine.Refresh();
    if (live.empty())
    {
        return;
    }
    const uint64_t now = pe::NowFileTime();
    const uint64_t flashTicks = m_highlightMs * 10000ULL;

    std::unordered_set<DWORD> livePids;
    for (const pe::ProcRow& r : live)
    {
        livePids.insert(r.pid);
    }

    // New-process flash and exited-process retention.
    for (const pe::ProcRow& r : live)
    {
        if (!m_prevPids.empty() && m_prevPids.count(r.pid) == 0)
        {
            m_newFlash[r.pid] = now + flashTicks;
        }
    }
    for (DWORD pid : m_prevPids)
    {
        if (livePids.count(pid) == 0)
        {
            auto it = m_prevByPid.find(pid);
            if (it != m_prevByPid.end())
            {
                pe::ProcRow dead = it->second;
                dead.isDead = true;
                dead.flashUntil = now + flashTicks;
                m_deadRows.push_back(std::move(dead));
            }
        }
    }
    m_deadRows.erase(std::remove_if(m_deadRows.begin(), m_deadRows.end(),
                                    [&](const pe::ProcRow& d)
                                    {
                                        return now >= d.flashUntil || livePids.count(d.pid) != 0;
                                    }),
                     m_deadRows.end());
    for (auto it = m_newFlash.begin(); it != m_newFlash.end();)
    {
        it = (now >= it->second || livePids.count(it->first) == 0) ? m_newFlash.erase(it) : std::next(it);
    }

    // Mark new flags on live rows.
    for (pe::ProcRow& r : live)
    {
        r.isNew = m_newFlash.count(r.pid) != 0;
    }

    FlattenTree(live);

    // Remember selection and scroll by pid.
    const DWORD selPid = SelectedPid();
    const int topIndex = m_tree.GetTopIndex();
    const DWORD topPid = (topIndex >= 0 && topIndex < static_cast<int>(m_rows.size())) ? m_rows[topIndex].pid : 0;

    m_tree.SetRedraw(FALSE);
    m_tree.DeleteAllItems();
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
    {
        InsertTreeRow(i);
    }
    RestoreSelection(selPid, topPid);
    m_tree.SetRedraw(TRUE);

    // Keep prev state.
    m_prevPids = std::move(livePids);
    m_prevByPid.clear();
    for (const pe::ProcRow& r : live)
    {
        m_prevByPid[r.pid] = r;
    }

    UpdateStatus(static_cast<int>(live.size()));

    if (m_showLower && (m_lowerMode == pe::LowerMode::Dlls))
    {
        UpdateLowerPane();
    }
}

// Build the parent-child hierarchy and flatten it depth first into m_rows.
void CMainFrame::FlattenTree(std::vector<pe::ProcRow>& live)
{
    m_rows.clear();
    m_depth.clear();

    std::unordered_map<DWORD, uint64_t> createByPid;
    for (const pe::ProcRow& r : live)
    {
        createByPid[r.pid] = r.createTime;
    }
    std::unordered_map<DWORD, std::vector<int>> children;
    std::vector<int> roots;
    for (int i = 0; i < static_cast<int>(live.size()); ++i)
    {
        const pe::ProcRow& r = live[i];
        auto pit = createByPid.find(r.parentPid);
        const bool validParent =
            r.parentPid != 0 && r.parentPid != r.pid && pit != createByPid.end() && pit->second <= r.createTime;
        if (validParent)
        {
            children[r.parentPid].push_back(i);
        }
        else
        {
            roots.push_back(i);
        }
    }
    auto byPid = [&](int a, int b)
    {
        return live[a].pid < live[b].pid;
    };
    std::sort(roots.begin(), roots.end(), byPid);
    for (auto& kv : children)
    {
        std::sort(kv.second.begin(), kv.second.end(), byPid);
    }

    std::vector<std::pair<int, int>> stack; // (live index, depth), reverse order
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
    {
        stack.emplace_back(*it, 0);
    }
    while (!stack.empty())
    {
        auto [idx, depth] = stack.back();
        stack.pop_back();
        m_rows.push_back(live[idx]);
        m_depth.push_back(depth);
        auto cit = children.find(live[idx].pid);
        if (cit != children.end())
        {
            for (auto it = cit->second.rbegin(); it != cit->second.rend(); ++it)
            {
                stack.emplace_back(*it, depth + 1);
            }
        }
    }
    for (const pe::ProcRow& d : m_deadRows)
    {
        m_rows.push_back(d);
        m_depth.push_back(0);
    }
}

void CMainFrame::InsertTreeRow(int i)
{
    const pe::ProcRow& r = m_rows[i];
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_INDENT | LVIF_PARAM;
    item.iItem = i;
    item.iIndent = m_depth[i];
    item.iImage = IconFor(r.imagePath);
    item.lParam = static_cast<LPARAM>(r.pid);
    item.pszText = const_cast<LPWSTR>(r.name.c_str());
    m_tree.InsertItem(&item);

    wchar_t buf[32] = {};
    swprintf(buf, 32, L"%u", r.pid);
    m_tree.SetItemText(i, 1, buf);
    if (r.cpu >= 0.005)
    {
        swprintf(buf, 32, L"%.2f", r.cpu);
        m_tree.SetItemText(i, 2, buf);
    }
    m_tree.SetItemText(i, 3, FormatKb(r.privateBytes).c_str());
    m_tree.SetItemText(i, 4, FormatKb(r.workingSet).c_str());
    m_tree.SetItemText(i, 5, r.description.c_str());
    m_tree.SetItemText(i, 6, r.company.c_str());
}

std::wstring CMainFrame::FormatKb(uint64_t bytes)
{
    if (bytes == 0)
    {
        return std::wstring();
    }
    return pe::GroupNumber((bytes + 1023) / 1024) + L" K";
}

DWORD CMainFrame::SelectedPid()
{
    int sel = m_tree.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(m_rows.size()))
    {
        return 0;
    }
    return m_rows[sel].pid;
}

void CMainFrame::RestoreSelection(DWORD selPid, DWORD topPid)
{
    if (topPid)
    {
        for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        {
            if (m_rows[i].pid == topPid)
            {
                m_tree.EnsureVisible(static_cast<int>(m_rows.size()) - 1, FALSE);
                m_tree.EnsureVisible(i, FALSE);
                break;
            }
        }
    }
    if (selPid)
    {
        for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        {
            if (m_rows[i].pid == selPid)
            {
                m_tree.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                break;
            }
        }
    }
}

// ---- colouring ----------------------------------------------------------
LRESULT CMainFrame::OnCustomDraw(int, LPNMHDR p, BOOL& handled)
{
    if (p->hwndFrom != m_tree.m_hWnd)
    {
        handled = FALSE;
        return 0;
    }
    auto* cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(p);
    switch (cd->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
    {
        const int i = static_cast<int>(cd->nmcd.dwItemSpec);
        if (i >= 0 && i < static_cast<int>(m_rows.size()))
        {
            cd->clrTextBk = RowColor(m_rows[i]);
        }
        return CDRF_DODEFAULT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

COLORREF CMainFrame::RowColor(const pe::ProcRow& r)
{
    if (r.isDead)
    {
        return pe::kColorDead;
    }
    if (r.isNew)
    {
        return pe::kColorNew;
    }
    if (r.isSuspended)
    {
        return pe::kColorSuspended;
    }
    if (r.isService)
    {
        return pe::kColorServices;
    }
    if (r.isOwn)
    {
        return pe::kColorOwn;
    }
    return RGB(255, 255, 255);
}

// ---- lower pane ---------------------------------------------------------
LRESULT CMainFrame::OnTreeItemChanged(int, LPNMHDR p, BOOL& handled)
{
    auto* nm = reinterpret_cast<LPNMLISTVIEW>(p);
    if (p->hwndFrom == m_tree.m_hWnd && (nm->uNewState & LVIS_SELECTED))
    {
        if (m_showLower)
        {
            UpdateLowerPane();
        }
    }
    handled = FALSE;
    return 0;
}

void CMainFrame::UpdateLowerPane()
{
    const DWORD pid = SelectedPid();
    m_lower.SetRedraw(FALSE);
    m_lower.DeleteAllItems();
    if (pid != 0 && pid != 4)
    {
        if (m_lowerMode == pe::LowerMode::Dlls)
        {
            int row = 0;
            for (const pe::ModuleRow& m : pe::EnumModules(pid))
            {
                m_lower.InsertItem(row, m.name.c_str());
                m_lower.SetItemText(row, 1, m.description.c_str());
                m_lower.SetItemText(row, 2, m.company.c_str());
                m_lower.SetItemText(row, 3, m.path.c_str());
                ++row;
            }
        }
        else
        {
            int row = 0;
            for (const pe::HandleRow& h : pe::EnumHandles(pid))
            {
                m_lower.InsertItem(row, h.type.c_str());
                m_lower.SetItemText(row, 1, h.name.c_str());
                wchar_t b[32] = {};
                swprintf(b, 32, L"0x%llX", static_cast<unsigned long long>(h.handle));
                m_lower.SetItemText(row, 2, b);
                ++row;
            }
        }
    }
    m_lower.SetRedraw(TRUE);
}

// ---- layout -------------------------------------------------------------
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
    const int top = BandHeight(m_hWndToolBar);
    const int bottom = rc.bottom - BandHeight(m_hWndStatusBar);
    int usable = bottom - top;
    if (usable < 60)
    {
        return;
    }
    if (!m_showLower)
    {
        m_tree.SetWindowPos(nullptr, 0, top, rc.right, usable, SWP_NOZORDER);
        m_lower.ShowWindow(SW_HIDE);
        return;
    }
    m_lower.ShowWindow(SW_SHOW);
    if (m_splitY < 60 || m_splitY > usable - 60)
    {
        m_splitY = usable * 3 / 5;
    }
    m_tree.SetWindowPos(nullptr, 0, top, rc.right, m_splitY, SWP_NOZORDER);
    const int lowTop = top + m_splitY + kSplitBar;
    m_lower.SetWindowPos(nullptr, 0, lowTop, rc.right, bottom - lowTop, SWP_NOZORDER);
}

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

CRect CMainFrame::SplitterRect()
{
    CRect rc;
    GetClientRect(&rc);
    const int top = BandHeight(m_hWndToolBar);
    return CRect(0, top + m_splitY, rc.right, top + m_splitY + kSplitBar);
}

BOOL CMainFrame::OnSetCursor(CWindow wnd, UINT hit, UINT)
{
    if (m_showLower && wnd.m_hWnd == m_hWnd && hit == HTCLIENT)
    {
        POINT pt{};
        ::GetCursorPos(&pt);
        ScreenToClient(&pt);
        if (SplitterRect().PtInRect(pt))
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_SIZENS));
            return TRUE;
        }
    }
    SetMsgHandled(FALSE);
    return FALSE;
}

void CMainFrame::OnLButtonDown(UINT, CPoint pt)
{
    if (m_showLower && SplitterRect().PtInRect(pt))
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
    m_splitY = pt.y - BandHeight(m_hWndToolBar);
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

void CMainFrame::UpdateStatus(int count)
{
    if (!m_hWndStatusBar)
    {
        return;
    }
    wchar_t buf[128] = {};
    swprintf(buf, 128, L"Processes: %d   CPUs: %u", count, m_engine.CpuCount());
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(buf));
}

// ---- commands -----------------------------------------------------------
LRESULT CMainFrame::OnRefresh(WORD, WORD, HWND, BOOL&)
{
    Refresh();
    if (m_showLower)
    {
        UpdateLowerPane();
    }
    return 0;
}

LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&)
{
    PostMessage(WM_CLOSE);
    return 0;
}
LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&)
{
    CAboutDlg().DoModal(m_hWnd);
    return 0;
}

const pe::ProcRow* CMainFrame::SelectedRow()
{
    int sel = m_tree.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0 || sel >= static_cast<int>(m_rows.size()))
    {
        return nullptr;
    }
    return &m_rows[sel];
}

LRESULT CMainFrame::OnProperties(WORD, WORD, HWND, BOOL&)
{
    const pe::ProcRow* r = SelectedRow();
    if (!r)
    {
        return 0;
    }
    std::wstring parentName;
    auto it = m_prevByPid.find(r->parentPid);
    if (it != m_prevByPid.end())
    {
        parentName = it->second.name;
    }
    CProcPropSheet sheet(*r, parentName);
    sheet.DoModal(m_hWnd);
    return 0;
}

// Kill the selected process. Confirmed, and only ever to be exercised on a
// throwaway process. TerminateProcess is the standard kill path.
LRESULT CMainFrame::OnKill(WORD, WORD, HWND, BOOL&)
{
    const pe::ProcRow* r = SelectedRow();
    if (!r || r->pid == 0 || r->pid == 4)
    {
        return 0;
    }
    wchar_t msg[256] = {};
    swprintf(msg, 256, L"Are you sure you want to kill %s (PID %u)?", r->name.c_str(), r->pid);
    if (::MessageBoxW(m_hWnd, msg, L"Process Explorer", MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return 0;
    }
    if (HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE, r->pid))
    {
        ::TerminateProcess(h, 1);
        ::CloseHandle(h);
        Refresh();
    }
    return 0;
}

// Suspend or resume the selected process via the native entry points.
LRESULT CMainFrame::OnSuspend(WORD, WORD, HWND, BOOL&)
{
    const pe::ProcRow* r = SelectedRow();
    if (!r || r->pid == 0 || r->pid == 4)
    {
        return 0;
    }
    using ProcFn = LONG(NTAPI*)(HANDLE);
    static ProcFn suspend = reinterpret_cast<ProcFn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess")));
    static ProcFn resume = reinterpret_cast<ProcFn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess")));
    ProcFn fn = r->isSuspended ? resume : suspend;
    if (fn)
    {
        if (HANDLE h = ::OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, r->pid))
        {
            fn(h);
            ::CloseHandle(h);
            Refresh();
        }
    }
    return 0;
}

LRESULT CMainFrame::OnSetPriority(WORD, WORD id, HWND, BOOL&)
{
    const pe::ProcRow* r = SelectedRow();
    if (!r || r->pid == 0 || r->pid == 4)
    {
        return 0;
    }
    DWORD cls = NORMAL_PRIORITY_CLASS;
    switch (id)
    {
    case ID_PRI_REALTIME:
        cls = REALTIME_PRIORITY_CLASS;
        break;
    case ID_PRI_HIGH:
        cls = HIGH_PRIORITY_CLASS;
        break;
    case ID_PRI_NORMAL:
        cls = NORMAL_PRIORITY_CLASS;
        break;
    case ID_PRI_IDLE:
        cls = IDLE_PRIORITY_CLASS;
        break;
    }
    if (HANDLE h = ::OpenProcess(PROCESS_SET_INFORMATION, FALSE, r->pid))
    {
        ::SetPriorityClass(h, cls);
        ::CloseHandle(h);
    }
    return 0;
}

LRESULT CMainFrame::OnSearchOnline(WORD, WORD, HWND, BOOL&)
{
    const pe::ProcRow* r = SelectedRow();
    if (r && !r->name.empty())
    {
        std::wstring url = L"https://www.bing.com/search?q=" + r->name;
        ::ShellExecuteW(m_hWnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return 0;
}

LRESULT CMainFrame::OnDblClk(int, LPNMHDR p, BOOL&)
{
    if (p->hwndFrom == m_tree.m_hWnd)
    {
        BOOL dummy = FALSE;
        OnProperties(0, 0, nullptr, dummy);
    }
    return 0;
}

LRESULT CMainFrame::OnLowerDlls(WORD, WORD, HWND, BOOL&)
{
    m_lowerMode = pe::LowerMode::Dlls;
    BuildLowerColumns();
    UpdateLowerPane();
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnLowerHandles(WORD, WORD, HWND, BOOL&)
{
    m_lowerMode = pe::LowerMode::Handles;
    BuildLowerColumns();
    UpdateLowerPane();
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnToggleLower(WORD, WORD, HWND, BOOL&)
{
    m_showLower = !m_showLower;
    OnSize(0, CSize());
    if (m_showLower)
    {
        UpdateLowerPane();
    }
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnSpeed(WORD, WORD id, HWND, BOOL&)
{
    switch (id)
    {
    case ID_SPEED_HALF:
        m_intervalMs = 500;
        break;
    case ID_SPEED_1:
        m_intervalMs = 1000;
        break;
    case ID_SPEED_2:
        m_intervalMs = 2000;
        break;
    case ID_SPEED_5:
        m_intervalMs = 5000;
        break;
    case ID_SPEED_10:
        m_intervalMs = 10000;
        break;
    }
    m_paused = false;
    KillTimer(kRefreshTimer);
    SetTimer(kRefreshTimer, m_intervalMs);
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnPaused(WORD, WORD, HWND, BOOL&)
{
    m_paused = !m_paused;
    if (m_paused)
    {
        KillTimer(kRefreshTimer);
    }
    else
    {
        SetTimer(kRefreshTimer, m_intervalMs);
    }
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnSave(WORD, WORD, HWND, BOOL&)
{
    wchar_t path[MAX_PATH] = L"procexp.txt";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFilter = L"Text files\0*.txt\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (::GetSaveFileNameW(&ofn))
    {
        SaveTreeToFile(path);
    }
    return 0;
}

void CMainFrame::SaveTreeToFile(const wchar_t* path)
{
    HANDLE f = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const unsigned char bom[2] = {0xFF, 0xFE};
    DWORD written = 0;
    ::WriteFile(f, bom, 2, &written, nullptr);
    auto emit = [&](const std::wstring& s)
    {
        ::WriteFile(f, s.data(), static_cast<DWORD>(s.size() * sizeof(wchar_t)), &written, nullptr);
    };
    emit(L"Process\tPID\tCPU\tPrivate Bytes\tWorking Set\tDescription\tCompany Name\r\n");
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
    {
        const pe::ProcRow& r = m_rows[i];
        std::wstring line(static_cast<size_t>(m_depth[i]) * 2, L' ');
        wchar_t cpu[16] = {};
        if (r.cpu >= 0.005)
        {
            swprintf(cpu, 16, L"%.2f", r.cpu);
        }
        wchar_t pid[16] = {};
        swprintf(pid, 16, L"%u", r.pid);
        line += r.name + L"\t" + pid + L"\t" + cpu + L"\t" + FormatKb(r.privateBytes) + L"\t" + FormatKb(r.workingSet) +
                L"\t" + r.description + L"\t" + r.company + L"\r\n";
        emit(line);
    }
    ::CloseHandle(f);
}

LRESULT CMainFrame::OnFindHandle(WORD, WORD, HWND, BOOL&)
{
    CFindHandleDlg dlg(m_rows);
    dlg.DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnFont(WORD, WORD, HWND, BOOL&)
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
    m_lower.SetFont(m_font);
}

LRESULT CMainFrame::OnHelp(WORD, WORD, HWND, BOOL&)
{
    ::ShellExecuteW(m_hWnd, L"open", L"https://learn.microsoft.com/sysinternals/downloads/process-explorer", nullptr,
                    nullptr, SW_SHOWNORMAL);
    return 0;
}

void CMainFrame::SyncMenu()
{
    CMenuHandle menu = GetMenu();
    if (menu.IsNull())
    {
        return;
    }
    menu.CheckMenuItem(ID_VIEW_LOWERPANE, MF_BYCOMMAND | (m_showLower ? MF_CHECKED : MF_UNCHECKED));
    menu.CheckMenuRadioItem(ID_VIEW_DLLS, ID_VIEW_THREADS,
                            m_lowerMode == pe::LowerMode::Dlls ? ID_VIEW_DLLS : ID_VIEW_HANDLES, MF_BYCOMMAND);
    menu.CheckMenuItem(ID_SPEED_PAUSED, MF_BYCOMMAND | (m_paused ? MF_CHECKED : MF_UNCHECKED));
    UINT speedId = ID_SPEED_1;
    switch (m_intervalMs)
    {
    case 500:
        speedId = ID_SPEED_HALF;
        break;
    case 1000:
        speedId = ID_SPEED_1;
        break;
    case 2000:
        speedId = ID_SPEED_2;
        break;
    case 5000:
        speedId = ID_SPEED_5;
        break;
    case 10000:
        speedId = ID_SPEED_10;
        break;
    }
    menu.CheckMenuRadioItem(ID_SPEED_HALF, ID_SPEED_10, speedId, MF_BYCOMMAND);
}

// ---- config (registry) --------------------------------------------------
void CMainFrame::LoadConfig()
{
    HKEY k = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
    {
        return;
    }
    auto dword = [&](const wchar_t* name, DWORD& out)
    {
        DWORD v = 0, cb = sizeof(v), type = 0;
        if (::RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<LPBYTE>(&v), &cb) == ERROR_SUCCESS)
        {
            out = v;
        }
    };
    DWORD v = 0;
    v = m_intervalMs;
    dword(L"RefreshRate", v);
    m_intervalMs = v ? v : 1000;
    v = m_showLower;
    dword(L"ShowLowerPane", v);
    m_showLower = v != 0;
    v = static_cast<DWORD>(m_lowerMode);
    dword(L"LowerPaneView", v);
    m_lowerMode = v ? pe::LowerMode::Handles : pe::LowerMode::Dlls;
    v = static_cast<DWORD>(m_highlightMs);
    dword(L"HighlightDuration", v);
    m_highlightMs = v ? v : 1000;

    WINDOWPLACEMENT wp{};
    DWORD cb = sizeof(wp), type = 0;
    if (::RegQueryValueExW(k, L"WindowPlacement", nullptr, &type, reinterpret_cast<LPBYTE>(&wp), &cb) ==
            ERROR_SUCCESS &&
        cb == sizeof(wp) && wp.length == sizeof(wp))
    {
        m_placement = wp;
        m_havePlacement = true;
    }
    cb = sizeof(m_logFont);
    if (::RegQueryValueExW(k, L"Font", nullptr, &type, reinterpret_cast<LPBYTE>(&m_logFont), &cb) == ERROR_SUCCESS &&
        cb == sizeof(m_logFont) && m_logFont.lfFaceName[0])
    {
        m_haveFont = true;
    }
    ::RegCloseKey(k);
}

void CMainFrame::SaveConfig()
{
    HKEY k = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
    {
        return;
    }
    auto setDw = [&](const wchar_t* name, DWORD v)
    {
        ::RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    };
    setDw(L"RefreshRate", m_intervalMs);
    setDw(L"ShowLowerPane", m_showLower ? 1 : 0);
    setDw(L"LowerPaneView", m_lowerMode == pe::LowerMode::Handles ? 1 : 0);
    setDw(L"HighlightDuration", static_cast<DWORD>(m_highlightMs));
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(&wp))
    {
        ::RegSetValueExW(k, L"WindowPlacement", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp), sizeof(wp));
    }
    if (m_haveFont)
    {
        ::RegSetValueExW(k, L"Font", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&m_logFont), sizeof(m_logFont));
    }
    ::RegCloseKey(k);
}

void CMainFrame::OnDestroy()
{
    KillTimer(kRefreshTimer);
    SaveConfig();
    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->RemoveMessageFilter(this);
    }
    PostQuitMessage(0);
}
