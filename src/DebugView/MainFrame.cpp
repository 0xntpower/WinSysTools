// MainFrame.cpp -- the DebugView main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the Win32 DBWIN capture feed, the owner-data report
// list with its sequence, time, pid and message columns, the filter and
// highlight view, find, save and clipboard, the config load and save, and the
// toolbar-light fix (PinToolbarLight after the toolbar is made, OnEraseBkgnd
// filling the frame with button-face).
#include "MainFrame.h"

#include "Capture.h"
#include "Columns.h"
#include "Config.h"
#include "DbgRow.h"
#include "Filter.h"
#include "Export.h"
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
    if (m_pFind && ::IsDialogMessageW(m_pFind->m_hWnd, pMsg))
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
    m_cfg.load();
    ::QueryPerformanceFrequency(&m_freq);
    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

    CreateToolbar();
    m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);
    int parts[] = {220, -1};
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));

    m_hWndClient =
        m_list.Create(m_hWnd, rcDefault, nullptr,
                      WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA | LVS_NOSORTHEADER,
                      WS_EX_CLIENTEDGE, IDC_LISTVIEW);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);

    m_font.CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
    m_list.SetFont(m_font);

    SetWindowTextW(L"DebugView");
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), L"APPICON"))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    RebuildColumns();
    SyncMenuChecks();
    SyncToolbar();
    DisableUnsupported();
    ApplyAlwaysOnTop();
    ApplyToolbarVisibility();

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    ApplyCapture();
    UpdateStatus();

    if (m_cfg.havePlacement)
    {
        SetWindowPlacement(&m_cfg.placement);
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

    // bmp index, command, is-a-toggle. Order and indices recovered from the
    // original TBBUTTON array at 0x140092300.
    struct Btn
    {
        int bmp;
        int cmd;
        bool check;
    };
    const Btn btns[] = {
        {-1, 0, false},
        {22, ID_FILE_OPEN, false},
        {0, ID_FILE_SAVE, false},
        {19, ID_FILE_LOGTOFILE, false},
        {-1, 0, false},
        {2, ID_CAPTURE_EVENTS, true},
        {-1, 0, false},
        {17, ID_CAPTURE_KERNEL, true},
        {9, ID_CAPTURE_PASSTHRU, false},
        {-1, 0, false},
        {12, ID_CAPTURE_WIN32, true},
        {-1, 0, false},
        {6, ID_EDIT_CLEAR, false},
        {4, ID_OPT_AUTOSCROLL, true},
        {14, ID_OPT_CLOCKTIME, true},
        {-1, 0, false},
        {16, ID_EDIT_FILTER, false},
        {11, ID_OPT_HISTORY, false},
        {-1, 0, false},
        {7, ID_EDIT_FIND, false},
    };
    std::vector<TBBUTTON> b;
    for (const Btn& x : btns)
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
            t.fsStyle = x.check ? TBSTYLE_CHECK : TBSTYLE_BUTTON;
        }
        b.push_back(t);
    }
    tb.AddButtons(static_cast<int>(b.size()), b.data());
    m_hWndToolBar = tb;
}

// ---- columns ------------------------------------------------------------
void CMainFrame::RebuildColumns()
{
    while (m_list.DeleteColumn(0))
    {
    }
    m_visCols.clear();
    m_visCols.push_back(COL_SEQ);
    m_visCols.push_back(COL_TIME);
    if (m_cfg.win32Pids)
    {
        m_visCols.push_back(COL_PID);
    }
    m_visCols.push_back(COL_MESSAGE);

    int i = 0;
    for (int c : m_visCols)
    {
        m_list.InsertColumn(i++, kColumns[c].title, kColumns[c].format, m_cfg.colWidth[c]);
    }
}

void CMainFrame::SyncWidths()
{
    for (int i = 0; i < static_cast<int>(m_visCols.size()); ++i)
    {
        m_cfg.colWidth[m_visCols[i]] = m_list.GetColumnWidth(i);
    }
}

void CMainFrame::SyncMenuChecks()
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
    chk(ID_CAPTURE_EVENTS, m_cfg.captureEvents);
    chk(ID_CAPTURE_WIN32, m_cfg.captureWin32);
    chk(ID_CAPTURE_KERNEL, m_cfg.captureKernel);
    chk(ID_OPT_AUTOSCROLL, m_cfg.autoScroll);
    chk(ID_OPT_CLOCKTIME, m_cfg.clockTime);
    chk(ID_OPT_MILLISECONDS, m_cfg.showMs);
    chk(ID_OPT_WIN32PIDS, m_cfg.win32Pids);
    chk(ID_OPT_HIDETOOLBAR, m_cfg.hideToolbar);
    chk(ID_OPT_ALWAYSONTOP, m_cfg.alwaysOnTop);
}

void CMainFrame::SyncToolbar()
{
    CToolBarCtrl tb(m_hWndToolBar);
    if (!tb.IsWindow())
    {
        return;
    }
    tb.CheckButton(ID_CAPTURE_EVENTS, m_cfg.captureEvents);
    tb.CheckButton(ID_CAPTURE_WIN32, m_cfg.captureWin32);
    tb.CheckButton(ID_CAPTURE_KERNEL, m_cfg.captureKernel);
    tb.CheckButton(ID_OPT_AUTOSCROLL, m_cfg.autoScroll);
    tb.CheckButton(ID_OPT_CLOCKTIME, m_cfg.clockTime);
}

// The original menu carries many features outside this recreation's scope
// (remote connect, printing, logging, crash dump). Grey them so their state
// is honest rather than silently doing nothing.
void CMainFrame::DisableUnsupported()
{
    CMenuHandle menu = GetMenu();
    if (menu.IsNull())
    {
        return;
    }
    const UINT ids[] = {
        ID_FILE_NEWWINDOW,  ID_FILE_OPEN,      ID_FILE_LOGTOFILE, ID_FILE_LOGTOFILEAS, ID_FILE_PRINT,
        ID_FILE_PRINTRANGE, ID_FILE_CRASHDUMP, ID_EDIT_APPENDCMT, ID_CAPTURE_PASSTHRU, ID_OPT_FORCECR,
        ID_OPT_HIDEMIN,     ID_COMP_CONNECT,   ID_COMP_LOCAL,     ID_COMP_DISCONNECT,  ID_HELP_HELP,
    };
    for (UINT id : ids)
    {
        menu.EnableMenuItem(id, MF_BYCOMMAND | MF_GRAYED);
    }
    CToolBarCtrl tb(m_hWndToolBar);
    if (tb.IsWindow())
    {
        tb.EnableButton(ID_FILE_OPEN, FALSE);
        tb.EnableButton(ID_FILE_LOGTOFILE, FALSE);
        tb.EnableButton(ID_CAPTURE_PASSTHRU, FALSE);
    }
}

// ---- capture ------------------------------------------------------------
void CMainFrame::ApplyCapture()
{
    bool want = m_cfg.captureEvents && m_cfg.captureWin32;
    if (want && !m_capture.running())
    {
        if (!m_capture.start(m_hWnd, WM_DBG_LINE))
        {
            MessageBoxW(L"Could not start Win32 debug capture. Another debug monitor may be running.", L"DebugView",
                        MB_OK | MB_ICONWARNING);
        }
    }
    else if (!want && m_capture.running())
    {
        m_capture.stop();
    }
}

LRESULT CMainFrame::OnDbgLine(UINT, WPARAM, LPARAM lParam, BOOL&)
{
    DbgRow* row = reinterpret_cast<DbgRow*>(lParam);
    if (!row)
    {
        return 0;
    }
    if (!m_haveBase)
    {
        m_baseQpc = row->qpc;
        m_haveBase = true;
    }
    row->seq = m_nextSeq++;
    m_rows.push_back(std::move(*row));
    delete row;

    TrimHistory();

    int idx = static_cast<int>(m_rows.size()) - 1;
    if (m_cfg.filter.shows(m_rows[idx].message))
    {
        m_view.push_back(idx);
        m_list.SetItemCountEx(static_cast<int>(m_view.size()), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
        if (m_cfg.autoScroll && !m_view.empty())
        {
            m_list.EnsureVisible(static_cast<int>(m_view.size()) - 1, FALSE);
        }
    }
    UpdateStatus();
    return 0;
}

// ponytail: history trim rebuilds the view on overflow, O(n) per trimmed
// line. Only runs when a non-zero depth is set, the default keeps everything.
void CMainFrame::TrimHistory()
{
    if (m_cfg.historyDepth <= 0 || static_cast<int>(m_rows.size()) <= m_cfg.historyDepth)
    {
        return;
    }
    int drop = static_cast<int>(m_rows.size()) - m_cfg.historyDepth;
    m_rows.erase(m_rows.begin(), m_rows.begin() + drop);
    RebuildView();
}

void CMainFrame::RebuildView()
{
    m_view.clear();
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
    {
        if (m_cfg.filter.shows(m_rows[i].message))
        {
            m_view.push_back(i);
        }
    }
    m_list.SetItemCountEx(static_cast<int>(m_view.size()), LVSICF_NOINVALIDATEALL);
    m_list.Invalidate();
}

// ---- cell formatting ----------------------------------------------------
std::wstring CMainFrame::cell(const DbgRow& row, int col)
{
    switch (col)
    {
    case COL_SEQ:
        return std::to_wstring(row.seq);
    case COL_PID:
        return std::to_wstring(row.pid);
    case COL_MESSAGE:
        return row.message;
    case COL_TIME:
        return formatTime(row);
    default:
        return std::wstring();
    }
}

std::wstring CMainFrame::formatTime(const DbgRow& row)
{
    if (m_cfg.clockTime)
    {
        FILETIME local{};
        SYSTEMTIME st{};
        ::FileTimeToLocalFileTime(&row.ft, &local);
        ::FileTimeToSystemTime(&local, &st);
        wchar_t buf[32];
        if (m_cfg.showMs)
        {
            swprintf(buf, 32, L"%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        }
        else
        {
            swprintf(buf, 32, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        }
        return buf;
    }
    double secs =
        m_freq.QuadPart ? static_cast<double>(row.qpc - m_baseQpc) / static_cast<double>(m_freq.QuadPart) : 0.0;
    wchar_t buf[32];
    swprintf(buf, 32, L"%.8f", secs);
    return buf;
}

LRESULT CMainFrame::OnGetDispInfo(int, LPNMHDR p, BOOL&)
{
    auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
    int item = di->item.iItem;
    if (item < 0 || item >= static_cast<int>(m_view.size()))
    {
        return 0;
    }
    if (di->item.mask & LVIF_TEXT)
    {
        int col = m_visCols[di->item.iSubItem];
        static std::wstring text;
        text = cell(m_rows[m_view[item]], col);
        di->item.pszText = const_cast<LPWSTR>(text.c_str());
    }
    return 0;
}

LRESULT CMainFrame::OnItemChanged(int, LPNMHDR, BOOL&)
{
    UpdateStatus();
    return 0;
}

LRESULT CMainFrame::OnCustomDraw(int, LPNMHDR p, BOOL&)
{
    auto* cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(p);
    switch (cd->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
    {
        int item = static_cast<int>(cd->nmcd.dwItemSpec);
        if (item >= 0 && item < static_cast<int>(m_view.size()))
        {
            if (m_cfg.filter.highlights(m_rows[m_view[item]].message))
            {
                cd->clrTextBk = RGB(255, 255, 0);
            }
        }
        return CDRF_DODEFAULT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

LRESULT CMainFrame::OnToolTip(int, LPNMHDR p, BOOL&)
{
    auto* ti = reinterpret_cast<LPNMTTDISPINFOW>(p);
    static std::wstring text;
    switch (static_cast<int>(ti->hdr.idFrom))
    {
    case ID_FILE_OPEN:
        text = L"Open (Ctrl+O)";
        break;
    case ID_FILE_SAVE:
        text = L"Save (Ctrl+S)";
        break;
    case ID_FILE_LOGTOFILE:
        text = L"Log to File (Ctrl+G)";
        break;
    case ID_CAPTURE_EVENTS:
        text = L"Capture (Ctrl+E)";
        break;
    case ID_CAPTURE_KERNEL:
        text = L"Capture Kernel (Ctrl+K)";
        break;
    case ID_CAPTURE_PASSTHRU:
        text = L"Passthrough Kernel";
        break;
    case ID_CAPTURE_WIN32:
        text = L"Capture Win32 (Ctrl+W)";
        break;
    case ID_EDIT_CLEAR:
        text = L"Clear (Ctrl+X)";
        break;
    case ID_OPT_AUTOSCROLL:
        text = L"Autoscroll (Ctrl+A)";
        break;
    case ID_OPT_CLOCKTIME:
        text = L"Time Format (Ctrl+T)";
        break;
    case ID_EDIT_FILTER:
        text = L"Filter/Highlight (Ctrl+L)";
        break;
    case ID_OPT_HISTORY:
        text = L"History Depth (Ctrl+H)";
        break;
    case ID_EDIT_FIND:
        text = L"Find (Ctrl+F)";
        break;
    default:
        return 0;
    }
    ti->lpszText = const_cast<LPWSTR>(text.c_str());
    return 0;
}

void CMainFrame::UpdateStatus()
{
    wchar_t buf[128];
    int sel = m_list.GetSelectedCount();
    if (sel > 0)
    {
        wsprintfW(buf, L"%d line(s), %d selected", static_cast<int>(m_view.size()), sel);
    }
    else
    {
        wsprintfW(buf, L"%d line(s)", static_cast<int>(m_view.size()));
    }
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(buf));
    const wchar_t* state = m_capture.running() ? L"Capture On" : L"Capture Off";
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(state));
}

// ---- capture commands ---------------------------------------------------
LRESULT CMainFrame::OnCaptureEvents(WORD, WORD, HWND, BOOL&)
{
    m_cfg.captureEvents = !m_cfg.captureEvents;
    SyncMenuChecks();
    SyncToolbar();
    ApplyCapture();
    UpdateStatus();
    return 0;
}
LRESULT CMainFrame::OnCaptureWin32(WORD, WORD, HWND, BOOL&)
{
    m_cfg.captureWin32 = !m_cfg.captureWin32;
    SyncMenuChecks();
    SyncToolbar();
    ApplyCapture();
    UpdateStatus();
    return 0;
}
LRESULT CMainFrame::OnCaptureKernel(WORD, WORD, HWND, BOOL&)
{
    if (!m_kernelWarned)
    {
        MessageBoxW(L"Kernel capture needs the DbgView helper driver (Dbgv.sys), which is "
                    L"out of scope in this recreation. Only the Win32 user-mode capture path "
                    L"is implemented.",
                    L"DebugView", MB_OK | MB_ICONINFORMATION);
        m_kernelWarned = true;
    }
    m_cfg.captureKernel = false;
    SyncMenuChecks();
    SyncToolbar();
    return 0;
}

LRESULT CMainFrame::OnClear(WORD, WORD, HWND, BOOL&)
{
    m_rows.clear();
    m_view.clear();
    m_nextSeq = 0;
    m_haveBase = false;
    m_list.SetItemCount(0);
    m_list.Invalidate();
    UpdateStatus();
    return 0;
}

// ---- option toggles -----------------------------------------------------
LRESULT CMainFrame::OnAutoScroll(WORD, WORD, HWND, BOOL&)
{
    m_cfg.autoScroll = !m_cfg.autoScroll;
    SyncMenuChecks();
    SyncToolbar();
    if (m_cfg.autoScroll && !m_view.empty())
    {
        m_list.EnsureVisible(static_cast<int>(m_view.size()) - 1, FALSE);
    }
    return 0;
}
LRESULT CMainFrame::OnClockTime(WORD, WORD, HWND, BOOL&)
{
    m_cfg.clockTime = !m_cfg.clockTime;
    SyncMenuChecks();
    SyncToolbar();
    m_list.Invalidate();
    return 0;
}
LRESULT CMainFrame::OnMilliseconds(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showMs = !m_cfg.showMs;
    SyncMenuChecks();
    m_list.Invalidate();
    return 0;
}
LRESULT CMainFrame::OnWin32Pids(WORD, WORD, HWND, BOOL&)
{
    SyncWidths();
    m_cfg.win32Pids = !m_cfg.win32Pids;
    RebuildColumns();
    SyncMenuChecks();
    m_list.Invalidate();
    return 0;
}
LRESULT CMainFrame::OnHideToolbar(WORD, WORD, HWND, BOOL&)
{
    m_cfg.hideToolbar = !m_cfg.hideToolbar;
    SyncMenuChecks();
    ApplyToolbarVisibility();
    return 0;
}
void CMainFrame::ApplyToolbarVisibility()
{
    if (m_hWndToolBar)
    {
        ::ShowWindow(m_hWndToolBar, m_cfg.hideToolbar ? SW_HIDE : SW_SHOW);
    }
    UpdateLayout();
}
LRESULT CMainFrame::OnAlwaysOnTop(WORD, WORD, HWND, BOOL&)
{
    m_cfg.alwaysOnTop = !m_cfg.alwaysOnTop;
    SyncMenuChecks();
    ApplyAlwaysOnTop();
    return 0;
}
void CMainFrame::ApplyAlwaysOnTop()
{
    SetWindowPos(m_cfg.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
LRESULT CMainFrame::OnFont(WORD, WORD, HWND, BOOL&)
{
    LOGFONTW lf{};
    m_font.GetLogFont(lf);
    CFontDialog dlg(&lf, CF_SCREENFONTS | CF_FIXEDPITCHONLY, nullptr, m_hWnd);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        m_font.DeleteObject();
        m_font.CreateFontIndirectW(&lf);
        m_list.SetFont(m_font);
        m_list.Invalidate();
    }
    return 0;
}
LRESULT CMainFrame::OnHistory(WORD, WORD, HWND, BOOL&)
{
    CHistoryDlg dlg(m_cfg.historyDepth);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        m_cfg.historyDepth = dlg.m_depth;
        TrimHistory();
        UpdateStatus();
    }
    return 0;
}
LRESULT CMainFrame::OnFilter(WORD, WORD, HWND, BOOL&)
{
    CFilterDlg dlg(m_cfg.filter);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        m_cfg.filter = dlg.m_filter;
        RebuildView();
    }
    return 0;
}

// ---- selection / clipboard / save ---------------------------------------
Report CMainFrame::buildReport(bool selectedOnly)
{
    Report rep;
    for (int c : m_visCols)
    {
        rep.headers.push_back(kColumns[c].title);
    }
    auto addRow = [&](int viewIdx)
    {
        std::vector<std::wstring> r;
        for (int c : m_visCols)
        {
            r.push_back(cell(m_rows[m_view[viewIdx]], c));
        }
        rep.rows.push_back(std::move(r));
    };
    if (selectedOnly && m_list.GetSelectedCount() > 0)
    {
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
        {
            if (i < static_cast<int>(m_view.size()))
            {
                addRow(i);
            }
        }
    }
    else
    {
        for (int i = 0; i < static_cast<int>(m_view.size()); ++i)
        {
            addRow(i);
        }
    }
    return rep;
}

LRESULT CMainFrame::OnCopy(WORD, WORD, HWND, BOOL&)
{
    if (m_list.GetSelectedCount() == 0)
    {
        return 0;
    }
    std::wstring text = BuildReport(buildReport(true), ExportFormat::Tab, false);
    CopyToClipboard(text);
    return 0;
}

LRESULT CMainFrame::OnSave(WORD, WORD, HWND, BOOL&)
{
    CFileDialog dlg(FALSE, L"log", L"DebugView.log", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
                    L"Log File (*.log)\0*.log\0Text File (*.txt)\0*.txt\0CSV File (*.csv)\0*.csv\0", m_hWnd);
    dlg.m_ofn.lpstrTitle = L"Save debug output";
    if (dlg.DoModal(m_hWnd) != IDOK)
    {
        return 0;
    }
    ExportFormat fmt = (dlg.m_ofn.nFilterIndex == 3) ? ExportFormat::Csv : ExportFormat::Tab;
    WriteReport(dlg.m_szFileName, buildReport(false), fmt, true);
    return 0;
}

void CMainFrame::CopyToClipboard(const std::wstring& text)
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

// ---- find ---------------------------------------------------------------
LRESULT CMainFrame::OnFind(WORD, WORD, HWND, BOOL&)
{
    if (m_pFind)
    {
        m_pFind->SetFocus();
        return 0;
    }
    m_pFind = new CFindReplaceDialog();
    m_pFind->Create(TRUE, m_lastFind.c_str(), nullptr, FR_DOWN, m_hWnd);
    return 0;
}
LRESULT CMainFrame::OnFindNext(WORD, WORD, HWND, BOOL&)
{
    if (!m_lastFind.empty())
    {
        FindText(m_lastFind, true);
    }
    return 0;
}
LRESULT CMainFrame::OnFindPrev(WORD, WORD, HWND, BOOL&)
{
    if (!m_lastFind.empty())
    {
        FindText(m_lastFind, false);
    }
    return 0;
}

void CMainFrame::OnFindReplaceMsg(LPARAM lParam)
{
    auto* fr = reinterpret_cast<LPFINDREPLACEW>(lParam);
    if (fr->Flags & FR_DIALOGTERM)
    {
        m_pFind = nullptr;
        return;
    }
    if (fr->Flags & FR_FINDNEXT)
    {
        m_lastFind = fr->lpstrFindWhat;
        FindText(m_lastFind, (fr->Flags & FR_DOWN) != 0);
    }
}
void CMainFrame::FindText(const std::wstring& what, bool down)
{
    if (what.empty() || m_view.empty())
    {
        return;
    }
    std::wstring needle = what;
    std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
    int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
    int n = static_cast<int>(m_view.size());
    for (int step = 1; step <= n; ++step)
    {
        int i = down ? (start + step) % n : ((start - step) % n + n) % n;
        std::wstring v = m_rows[m_view[i]].message;
        std::transform(v.begin(), v.end(), v.begin(), towlower);
        if (v.find(needle) != std::wstring::npos)
        {
            m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            m_list.EnsureVisible(i, FALSE);
            return;
        }
    }
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

void CMainFrame::OnDestroy()
{
    m_capture.stop();
    SyncWidths();
    m_cfg.placement.length = sizeof(m_cfg.placement);
    if (GetWindowPlacement(&m_cfg.placement))
    {
        m_cfg.havePlacement = true;
    }
    m_cfg.save();
    PostQuitMessage(0);
}
