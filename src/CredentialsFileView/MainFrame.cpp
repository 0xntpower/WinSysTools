// MainFrame.cpp -- the CredentialsFileView main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the DPAPI decode feeding the owner-data list, the
// hand-laid horizontal splitter, the hex-dump and strings lower pane, the
// clipboard and export commands, the config load and save, and the
// toolbar-light fix (PinToolbarLight after the toolbar is made, OnEraseBkgnd
// filling the frame with button-face).
#include "MainFrame.h"

#include "CredRecord.h"
#include "CredFile.h"
#include "Columns.h"
#include "Export.h"
#include "Config.h"
#include "Dialogs.h"
#include "../common/UiUtil.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- message filter -----------------------------------------------------
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
    m_splitPos = (m_cfg.verSplitLoc > 0 && m_cfg.verSplitLoc < 10000) ? m_cfg.verSplitLoc : 0;
    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

    CreateToolbar();
    m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);
    int parts[] = {220, -1};
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, (LPARAM)parts);

    // The frame hosts the two panes directly, laid out by hand in OnSize, so
    // nothing is designated as m_hWndClient.
    m_list.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                  WS_EX_CLIENTEDGE, IDC_LISTVIEW);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

    m_lower.Create(m_hWnd, rcDefault, nullptr,
                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                       ES_AUTOHSCROLL,
                   WS_EX_CLIENTEDGE, IDC_LOWERPANE);
    LOGFONTW lf{};
    lf.lfHeight = -12;
    wcscpy_s(lf.lfFaceName, L"Lucida Console");
    m_lowerFont = ::CreateFontIndirectW(&lf);
    m_lower.SetFont(m_lowerFont);

    SetWindowTextW(L"CredentialsFileView");
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    RebuildColumns();
    ApplyGridLines();
    SyncMenuChecks();

    if (m_cfg.havePlacement)
    {
        SetWindowPlacement(&m_cfg.placement);
    }

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    SetMsgHandled(FALSE);
    return 0;
}

// Show the Decryption Options dialog once the window is up, unless a headless
// path already filled the list.
void CMainFrame::PromptAndDecrypt()
{
    CDecryptOptionsDlg dlg(m_cfg);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        Refresh();
    }
}

// ---- two-pane layout (manual splitter) ------------------------------
int CMainFrame::clientTopOffset() const
{
    if (!m_hWndToolBar)
    {
        return 0;
    }
    RECT t;
    ::GetWindowRect(m_hWndToolBar, &t);
    return t.bottom - t.top;
}
int CMainFrame::clientBottomOffset() const
{
    RECT rc;
    ::GetClientRect(m_hWnd, &rc);
    int b = rc.bottom;
    if (m_hWndStatusBar)
    {
        RECT s;
        ::GetWindowRect(m_hWndStatusBar, &s);
        b -= (s.bottom - s.top);
    }
    return b;
}

void CMainFrame::PositionPanes()
{
    if (!m_list.IsWindow() || !m_lower.IsWindow())
    {
        return;
    }
    RECT rc;
    GetClientRect(&rc);
    int top = clientTopOffset();
    int bottom = clientBottomOffset();
    int width = rc.right;
    int avail = bottom - top;
    if (avail < 2 * kBar + 40)
    {
        m_list.MoveWindow(0, top, width, avail > 0 ? avail : 0, TRUE);
        m_lower.MoveWindow(0, bottom, width, 0, TRUE);
        return;
    }
    if (m_splitPos < 10)
    {
        m_splitPos = avail * 65 / 100;
    }
    if (m_splitPos > avail - kBar - 20)
    {
        m_splitPos = avail - kBar - 20;
    }
    if (m_splitPos < 20)
    {
        m_splitPos = 20;
    }
    m_list.MoveWindow(0, top, width, m_splitPos, TRUE);
    m_lower.MoveWindow(0, top + m_splitPos + kBar, width, bottom - (top + m_splitPos + kBar), TRUE);
}

void CMainFrame::OnSize(UINT, CSize)
{
    SetMsgHandled(FALSE);
    PositionPanes();
}

bool CMainFrame::overBar(int y) const
{
    int barTop = clientTopOffset() + m_splitPos;
    return y >= barTop && y < barTop + kBar;
}

void CMainFrame::OnLButtonDown(UINT, CPoint pt)
{
    if (overBar(pt.y))
    {
        m_dragging = true;
        SetCapture();
    }
    else
    {
        SetMsgHandled(FALSE);
    }
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
void CMainFrame::OnMouseMove(UINT, CPoint pt)
{
    if (!m_dragging)
    {
        SetMsgHandled(FALSE);
        return;
    }
    int top = clientTopOffset(), bottom = clientBottomOffset();
    m_splitPos = pt.y - top;
    PositionPanes();
}
BOOL CMainFrame::OnSetCursor(CWindow wnd, UINT hitTest, UINT)
{
    if (wnd.m_hWnd == m_hWnd && hitTest == HTCLIENT)
    {
        POINT pt;
        ::GetCursorPos(&pt);
        ::ScreenToClient(m_hWnd, &pt);
        if (overBar(pt.y))
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_SIZENS));
            return TRUE;
        }
    }
    SetMsgHandled(FALSE);
    return FALSE;
}

void CMainFrame::CreateToolbar()
{
    CToolBarCtrl tb;
    tb.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER, 0,
              IDC_TOOLBAR);
    tb.SetButtonStructSize(sizeof(TBBUTTON));
    PinToolbarLight(tb);
    tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));
    struct
    {
        int bmp, cmd;
    } btns[] = {
        {-1, 0},          {6, ID_CFV_DECRYPT_OPTIONS}, {1, ID_CFV_SAVE}, {5, ID_CFV_REFRESH},
        {2, ID_CFV_COPY}, {3, ID_CFV_PROPERTIES},      {0, ID_CFV_FIND}, {4, ID_CFV_EXIT},
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
    tb.AddButtons((int)b.size(), b.data());
    tb.AutoSize();
    m_hWndToolBar = tb;
}

// ---- columns --------------------------------------------------------
void CMainFrame::RebuildColumns()
{
    while (m_list.DeleteColumn(0))
    {
    }
    int i = 0;
    for (size_t k = 0; k < m_cfg.visibleColumns.size(); ++k)
    {
        int c = m_cfg.visibleColumns[k];
        int w = k < m_cfg.columnWidths.size() ? m_cfg.columnWidths[k] : kColumns[c].width;
        int fmt = (kColumns[c].kind == ColKind::CK_NUMERIC) ? LVCFMT_RIGHT : LVCFMT_LEFT;
        m_list.InsertColumn(i++, kColumns[c].title, fmt, w);
    }
}

void CMainFrame::ApplyGridLines()
{
    DWORD ex = m_list.GetExtendedListViewStyle();
    ex = m_cfg.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
    ex = m_cfg.showInfoTip ? (ex | LVS_EX_INFOTIP) : (ex & ~LVS_EX_INFOTIP);
    m_list.SetExtendedListViewStyle(ex);
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
    chk(ID_CFV_GRIDLINES, m_cfg.showGridLines);
    chk(ID_CFV_TOOLTIPS, m_cfg.showInfoTip);
    chk(ID_CFV_MARKODDEVEN, m_cfg.markOddEvenRows);
    chk(ID_CFV_SHOWTIMEGMT, m_cfg.showTimeInGmt);
    menu.CheckMenuRadioItem(ID_CFV_LOWERPANE_HEX, ID_CFV_LOWERPANE_STR,
                            m_cfg.lowerPaneMode == 2 ? ID_CFV_LOWERPANE_STR : ID_CFV_LOWERPANE_HEX, MF_BYCOMMAND);
    menu.CheckMenuRadioItem(ID_CFV_ENC_DEFAULT, ID_CFV_ENC_UTF8, ID_CFV_ENC_DEFAULT + m_cfg.saveFileEncoding,
                            MF_BYCOMMAND);
}

DecodeRequest CMainFrame::request() const
{
    DecodeRequest r;
    r.source = m_cfg.dataSource;
    r.password = m_cfg.password_;
    r.credFolders = m_cfg.folders();
    r.display.showTimeInGmt = m_cfg.showTimeInGmt;
    return r;
}

// ---- data -----------------------------------------------------------
void CMainFrame::Refresh()
{
    HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
    m_all.clear();
    DecodeFolders(request(), m_all);
    ApplyFilterAndSort();
    ::SetCursor(prev);
}

void CMainFrame::ApplyFilterAndSort()
{
    m_items = m_all;
    SortItems();
    m_list.SetItemCountEx((int)m_items.size(), LVSICF_NOSCROLL);
    m_list.Invalidate();
    UpdateStatus();
    UpdateLowerPane();
}

void CMainFrame::SortItems()
{
    const int col = m_cfg.sortColumn;
    const bool asc = m_cfg.sortAscending;
    std::stable_sort(m_items.begin(), m_items.end(),
                     [&](const CredRecord& a, const CredRecord& b)
                     {
                         bool na = false, nb = false;
                         long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
                         int c;
                         if (na && nb)
                         {
                             c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
                         }
                         else
                         {
                             c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
                         }
                         return asc ? (c < 0) : (c > 0);
                     });
}

LRESULT CMainFrame::OnColumnClick(int, LPNMHDR p, BOOL&)
{
    int disp = reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem;
    if (disp < 0 || disp >= (int)m_cfg.visibleColumns.size())
    {
        return 0;
    }
    int clicked = m_cfg.visibleColumns[disp];
    if (clicked == m_cfg.sortColumn)
    {
        m_cfg.sortAscending = !m_cfg.sortAscending;
    }
    else
    {
        m_cfg.sortColumn = clicked;
        m_cfg.sortAscending = true;
    }
    SortItems();
    m_list.Invalidate();
    return 0;
}

// ---- owner-data feed ------------------------------------------------
LRESULT CMainFrame::OnGetDispInfo(int, LPNMHDR p, BOOL&)
{
    auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
    int item = di->item.iItem;
    if (item < 0 || item >= (int)m_items.size())
    {
        return 0;
    }
    if (di->item.mask & LVIF_TEXT)
    {
        int col = m_cfg.visibleColumns[di->item.iSubItem];
        static std::wstring text;
        text = m_items[item].column(col);
        di->item.pszText = const_cast<LPWSTR>(text.c_str());
    }
    return 0;
}

LRESULT CMainFrame::OnItemChanged(int, LPNMHDR, BOOL&)
{
    UpdateStatus();
    UpdateLowerPane();
    return 0;
}
LRESULT CMainFrame::OnStateChanged(int, LPNMHDR, BOOL&)
{
    UpdateStatus();
    return 0;
}

void CMainFrame::UpdateStatus()
{
    wchar_t buf[128];
    int sel = m_list.GetSelectedCount();
    if (sel > 0)
    {
        wsprintfW(buf, L"%d item(s), %d Selected", (int)m_items.size(), sel);
    }
    else
    {
        wsprintfW(buf, L"%d item(s)", (int)m_items.size());
    }
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
}

// ---- lower pane -----------------------------------------------------
void CMainFrame::UpdateLowerPane()
{
    int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
    if (i < 0 || i >= (int)m_items.size())
    {
        m_lower.SetWindowTextW(L"");
        return;
    }
    const std::vector<BYTE>& data = m_items[i].plain;
    std::wstring text = (m_cfg.lowerPaneMode == 2) ? BuildStrings(data) : BuildHexDump(data);
    m_lower.SetWindowTextW(text.c_str());
}

std::wstring CMainFrame::BuildHexDump(const std::vector<BYTE>& d)
{
    std::wstring out;
    wchar_t line[8];
    for (size_t off = 0; off < d.size(); off += 16)
    {
        swprintf_s(line, L"%4.4X   ", (unsigned)off);
        out += line;
        std::wstring ascii;
        for (size_t j = 0; j < 16; ++j)
        {
            if (off + j < d.size())
            {
                wchar_t hx[8];
                swprintf_s(hx, L"%2.2X ", d[off + j]);
                out += hx;
                wchar_t c = d[off + j];
                ascii += (c >= 32 && c < 127) ? c : L'.';
            }
            else
            {
                out += L"   ";
            }
        }
        out += L"  " + ascii + L"\r\n";
    }
    return out;
}

std::wstring CMainFrame::BuildStrings(const std::vector<BYTE>& d)
{
    std::wstring out;
    int idx = 0;
    auto emit = [&](const std::wstring& s)
    {
        if (s.size() >= 2)
        {
            wchar_t pre[24];
            swprintf_s(pre, L"[%d]  ", idx++);
            out += pre;
            out += s;
            out += L"\r\n";
        }
    };
    // ASCII runs
    std::wstring cur;
    for (size_t i = 0; i < d.size(); ++i)
    {
        if (d[i] >= 32 && d[i] < 127)
        {
            cur += (wchar_t)d[i];
        }
        else
        {
            emit(cur);
            cur.clear();
        }
    }
    emit(cur);
    cur.clear();
    // UTF-16 runs
    for (size_t i = 0; i + 1 < d.size(); i += 2)
    {
        wchar_t c = d[i] | (d[i + 1] << 8);
        if (c >= 32 && c < 0xFFFF && c != 0x7F)
        {
            cur += c;
        }
        else
        {
            emit(cur);
            cur.clear();
        }
    }
    emit(cur);
    return out;
}

// ---- selection helpers ---------------------------------------------
std::vector<CredRecord> CMainFrame::SelectedItems()
{
    std::vector<CredRecord> v;
    for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
    {
        v.push_back(m_items[i]);
    }
    return v;
}

ExportOptions CMainFrame::expOpt()
{
    ExportOptions o;
    o.encoding = m_cfg.saveFileEncoding;
    return o;
}

// ---- commands -------------------------------------------------------
LRESULT CMainFrame::OnRefresh(WORD, WORD, HWND, BOOL&)
{
    Refresh();
    return 0;
}
LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&)
{
    PostMessage(WM_CLOSE);
    return 0;
}
LRESULT CMainFrame::OnDecryptOptions(WORD, WORD, HWND, BOOL&)
{
    PromptAndDecrypt();
    return 0;
}

LRESULT CMainFrame::OnSelectAll(WORD, WORD, HWND, BOOL&)
{
    m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
    UpdateStatus();
    return 0;
}
LRESULT CMainFrame::OnDeselectAll(WORD, WORD, HWND, BOOL&)
{
    m_list.SetItemState(-1, 0, LVIS_SELECTED);
    UpdateStatus();
    return 0;
}

LRESULT CMainFrame::OnGridLines(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showGridLines = !m_cfg.showGridLines;
    ApplyGridLines();
    SyncMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnTooltips(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showInfoTip = !m_cfg.showInfoTip;
    ApplyGridLines();
    SyncMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnMarkOddEven(WORD, WORD, HWND, BOOL&)
{
    m_cfg.markOddEvenRows = !m_cfg.markOddEvenRows;
    m_list.Invalidate();
    SyncMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnShowTimeGmt(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showTimeInGmt = !m_cfg.showTimeInGmt;
    SyncMenuChecks();
    Refresh();
    return 0;
}
LRESULT CMainFrame::OnEncoding(WORD, WORD id, HWND, BOOL&)
{
    m_cfg.saveFileEncoding = id - ID_CFV_ENC_DEFAULT;
    SyncMenuChecks();
    return 0;
}

LRESULT CMainFrame::OnLowerPaneMode(WORD, WORD id, HWND, BOOL&)
{
    m_cfg.lowerPaneMode = (id == ID_CFV_LOWERPANE_STR) ? 2 : 1;
    SyncMenuChecks();
    UpdateLowerPane();
    return 0;
}

LRESULT CMainFrame::OnAutoSize(WORD, WORD, HWND, BOOL&)
{
    for (int i = 0; i < (int)m_cfg.visibleColumns.size(); ++i)
    {
        m_list.SetColumnWidth(i, LVSCW_AUTOSIZE);
    }
    SyncWidths();
    return 0;
}
LRESULT CMainFrame::OnAutoSizeHdr(WORD, WORD, HWND, BOOL&)
{
    for (int i = 0; i < (int)m_cfg.visibleColumns.size(); ++i)
    {
        m_list.SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER);
    }
    SyncWidths();
    return 0;
}
void CMainFrame::SyncWidths()
{
    for (size_t i = 0; i < m_cfg.visibleColumns.size(); ++i)
    {
        if (i < m_cfg.columnWidths.size())
        {
            m_cfg.columnWidths[i] = m_list.GetColumnWidth((int)i);
        }
    }
}

LRESULT CMainFrame::OnCopy(WORD, WORD, HWND, BOOL&)
{
    auto sel = SelectedItems();
    if (sel.empty())
    {
        return 0;
    }
    CopyToClipboard(BuildReport(sel, m_cfg.visibleColumns, ExportFormat::Tab, expOpt()));
    return 0;
}

LRESULT CMainFrame::OnSaveSelected(WORD, WORD, HWND, BOOL&)
{
    auto items = SelectedItems();
    if (items.empty())
    {
        items = m_items;
    }
    CFileDialog dlg(FALSE, L"txt", L"CredentialsFileView.txt",
                    OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
                    L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
                    L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.html)\0*.html\0"
                    L"HTML File - Vertical (*.html)\0*.html\0XML File (*.xml)\0*.xml\0"
                    L"Comma Delimited Text File (*.csv)\0*.csv\0",
                    m_hWnd);
    dlg.m_ofn.lpstrTitle = L"Select a filename to save";
    if (dlg.m_ofn.nFilterIndex == 0)
    {
        dlg.m_ofn.nFilterIndex = m_cfg.saveFilterIndex + 1;
    }
    if (dlg.DoModal() != IDOK)
    {
        return 0;
    }
    static const ExportFormat byIdx[] = {
        ExportFormat::Text,         ExportFormat::Tab, ExportFormat::Tabular, ExportFormat::HtmlHorizontal,
        ExportFormat::HtmlVertical, ExportFormat::Xml, ExportFormat::Csv};
    DWORD fi = dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex - 1 : 0;
    m_cfg.saveFilterIndex = (int)fi;
    WriteReport(dlg.m_szFileName, items, m_cfg.visibleColumns, byIdx[std::min<DWORD>(fi, 6)], expOpt());
    return 0;
}

LRESULT CMainFrame::OnHtmlAll(WORD, WORD, HWND, BOOL&)
{
    ShowHtml(m_items);
    return 0;
}
LRESULT CMainFrame::OnHtmlSel(WORD, WORD, HWND, BOOL&)
{
    auto s = SelectedItems();
    ShowHtml(s.empty() ? m_items : s);
    return 0;
}

void CMainFrame::ShowHtml(const std::vector<CredRecord>& items)
{
    wchar_t dir[MAX_PATH], tmp[MAX_PATH];
    ::GetTempPathW(MAX_PATH, dir);
    ::GetTempFileNameW(dir, L"cfv", 0, tmp);
    std::wstring html = tmp;
    html += L".html";
    ExportOptions o = expOpt();
    o.encoding = 0;
    WriteReport(html, items, m_cfg.visibleColumns, ExportFormat::HtmlHorizontal, o);
    ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CMainFrame::OnChooseColumns(WORD, WORD, HWND, BOOL&)
{
    CColumnsDlg dlg(m_cfg.visibleColumns, m_cfg.columnWidths);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        m_cfg.visibleColumns = dlg.m_visible;
        m_cfg.columnWidths = dlg.m_widths;
        if (std::find(m_cfg.visibleColumns.begin(), m_cfg.visibleColumns.end(), m_cfg.sortColumn) ==
                m_cfg.visibleColumns.end() &&
            !m_cfg.visibleColumns.empty())
        {
            m_cfg.sortColumn = m_cfg.visibleColumns.front();
        }
        RebuildColumns();
        m_list.Invalidate();
    }
    return 0;
}

LRESULT CMainFrame::OnProperties(WORD, WORD, HWND, BOOL&)
{
    int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
    if (i < 0 || i >= (int)m_items.size())
    {
        return 0;
    }
    CPropertiesDlg dlg(m_items[i], m_cfg.visibleColumns);
    dlg.DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&)
{
    CAboutDlg().DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnRunAsAdmin(WORD, WORD, HWND, BOOL&)
{
    wchar_t path[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if ((INT_PTR)::ShellExecuteW(m_hWnd, L"runas", path, nullptr, nullptr, SW_SHOWNORMAL) > 32)
    {
        PostMessage(WM_CLOSE);
    }
    return 0;
}

// ---- find -----------------------------------------------------------
LRESULT CMainFrame::OnFind(WORD, WORD, HWND, BOOL&)
{
    if (m_pFind)
    {
        m_pFind->SetFocus();
        return 0;
    }
    m_pFind = new CFindReplaceDialog();
    m_pFind->Create(TRUE, L"", nullptr, FR_DOWN, m_hWnd);
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
        FindText(fr->lpstrFindWhat, (fr->Flags & FR_DOWN) != 0);
    }
}
void CMainFrame::FindText(const std::wstring& what, bool down)
{
    if (what.empty() || m_items.empty())
    {
        return;
    }
    std::wstring needle = what;
    std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
    int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
    int n = (int)m_items.size();
    for (int step = 1; step <= n; ++step)
    {
        int i = down ? (start + step) % n : ((start - step) % n + n) % n;
        for (int c : m_cfg.visibleColumns)
        {
            std::wstring v = m_items[i].column(c);
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
}

// ---- notifications --------------------------------------------------
LRESULT CMainFrame::OnDblClk(int, LPNMHDR, BOOL&)
{
    BOOL b;
    OnProperties(0, 0, nullptr, b);
    return 0;
}

LRESULT CMainFrame::OnRClick(int, LPNMHDR p, BOOL&)
{
    auto* ia = reinterpret_cast<LPNMITEMACTIVATE>(p);
    m_rclickRow = ia->iItem;
    m_rclickCol = ia->iSubItem;
    HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CONTEXT_MENU));
    if (!menu)
    {
        return 0;
    }
    HMENU sub = ::GetSubMenu(menu, 0);
    POINT pt;
    ::GetCursorPos(&pt);
    ::TrackPopupMenu(sub, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
    ::DestroyMenu(menu);
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

void CMainFrame::OnDestroy()
{
    SyncWidths();
    if (m_splitPos > 0)
    {
        m_cfg.verSplitLoc = m_splitPos;
    }
    m_cfg.placement.length = sizeof(m_cfg.placement);
    if (GetWindowPlacement(&m_cfg.placement))
    {
        m_cfg.havePlacement = true;
    }
    m_cfg.save();
    if (m_lowerFont)
    {
        ::DeleteObject(m_lowerFont);
    }
    PostQuitMessage(0);
}
