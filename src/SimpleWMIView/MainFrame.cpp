// MainFrame.cpp -- the SimpleWMIView main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the toolbar and options bar wiring, the owner-data
// ListView feed over the RunWmiQuery() model, the sort and find helpers, the
// export and clipboard commands, the config load and save, and the toolbar-light
// fix (PinToolbarLight after the toolbar is made, OnEraseBkgnd filling the frame
// with button-face).
#include "MainFrame.h"

#include "WmiQuery.h"
#include "WmiResult.h"
#include "Config.h"
#include "Export.h"
#include "Dialogs.h"
#include "OptionsBar.h"
#include "../common/UiUtil.h"

#include <algorithm>
#include <string>
#include <vector>

DisplayOptions CMainFrame::disp() const
{
    DisplayOptions d;
    d.showTimeInGmt = m_cfg.showTimeInGmt;
    d.showMilliseconds = m_cfg.showMilliseconds;
    return d;
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
    if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg))
    {
        return TRUE;
    }
    if (m_bar.IsWindow() && ::IsDialogMessageW(m_bar.m_hWnd, pMsg))
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
    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

    CreateToolbar();
    m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);
    int parts[] = {300, 460, -1};
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 3, (LPARAM)parts);

    m_bar.m_host = this;
    m_bar.Create(m_hWnd);
    m_bar.FromConfig(m_cfg);
    RECT rb{};
    m_bar.GetWindowRect(&rb);
    m_barHeight = rb.bottom - rb.top;
    m_bar.ShowWindow(SW_SHOW);

    m_hWndClient = m_list.Create(m_hWnd, rcDefault, nullptr,
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                                 WS_EX_CLIENTEDGE, IDC_LISTVIEW);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

    SetWindowTextW(L"SimpleWMIView");
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    ApplyGridLines();
    ApplyColors();
    SyncMenuChecks();

    if (m_cfg.havePlacement)
    {
        SetWindowPlacement(&m_cfg.placement);
    }

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    if (m_autoUpdate || m_cfg.autoUpdateOnClassChange)
    {
        PostMessage(WM_COMMAND, MAKEWPARAM(ID_SWV_REFRESH, 0));
    }
    return 0;
}

void CMainFrame::CreateToolbar()
{
    // Standard flat toolbar. PinToLightMode keeps the themed strip light, so
    // the masked imagelist renders on the toolbar without a dark box.
    CToolBarCtrl tb;
    tb.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER, 0,
              IDC_TOOLBAR);
    tb.SetButtonStructSize(sizeof(TBBUTTON));
    PinToolbarLight(tb);
    tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));
    // Button order and bitmap indices recovered from sub_14000FAF4.
    struct
    {
        int bmp, cmd;
    } btns[] = {
        {1, ID_SWV_SAVE}, {5, ID_SWV_REFRESH}, {2, ID_SWV_COPY}, {3, ID_SWV_PROPERTIES},
        {0, ID_SWV_FIND}, {6, ID_SWV_SORTBY},  {4, ID_SWV_EXIT},
    };
    std::vector<TBBUTTON> b;
    for (auto& x : btns)
    {
        TBBUTTON t{};
        t.iBitmap = x.bmp;
        t.idCommand = x.cmd;
        t.fsState = TBSTATE_ENABLED;
        t.fsStyle = TBSTYLE_BUTTON;
        b.push_back(t);
    }
    tb.AddButtons((int)b.size(), b.data());
    m_hWndToolBar = tb;
}

void CMainFrame::OnSize(UINT type, CSize)
{
    if (type == SIZE_MINIMIZED)
    {
        return;
    }
    if (m_hWndToolBar)
    {
        ::SendMessageW(m_hWndToolBar, TB_AUTOSIZE, 0, 0);
    }
    if (m_hWndStatusBar)
    {
        ::SendMessageW(m_hWndStatusBar, WM_SIZE, 0, 0);
    }

    RECT rc;
    GetClientRect(&rc);
    int top = 0, bottom = rc.bottom;
    if (m_hWndToolBar)
    {
        RECT rt{};
        ::GetWindowRect(m_hWndToolBar, &rt);
        top += rt.bottom - rt.top;
    }
    if (m_hWndStatusBar)
    {
        RECT rs{};
        ::GetWindowRect(m_hWndStatusBar, &rs);
        bottom -= rs.bottom - rs.top;
    }
    if (m_bar.IsWindow())
    {
        m_bar.SetWindowPos(nullptr, 0, top, rc.right, m_barHeight, SWP_NOZORDER);
        top += m_barHeight;
    }
    if (m_list.IsWindow())
    {
        m_list.SetWindowPos(nullptr, 0, top, rc.right, bottom - top, SWP_NOZORDER);
    }
}

void CMainFrame::OnSetFocus(CWindow)
{
    if (m_list.IsWindow())
    {
        m_list.SetFocus();
    }
}

// ---- columns ------------------------------------------------------------
void CMainFrame::RebuildColumns()
{
    while (m_list.DeleteColumn(0))
    {
    }
    int i = 0;
    for (int c : m_cols)
    {
        int fmt = (m_cfg.alignNumbersToRight && !m_res.columns[c].isArray && cimIsNumeric(m_res.columns[c].cimType))
                      ? LVCFMT_RIGHT
                      : LVCFMT_LEFT;
        m_list.InsertColumn(i, m_res.columns[c].name.c_str(), fmt, m_res.columns[c].width);
        if (m_cfg.autoColumnsSize)
        {
            m_list.SetColumnWidth(i, LVSCW_AUTOSIZE);
        }
        ++i;
    }
}

void CMainFrame::ApplyGridLines()
{
    DWORD ex = m_list.GetExtendedListViewStyle();
    ex = m_cfg.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
    m_list.SetExtendedListViewStyle(ex);
}

void CMainFrame::ApplyColors()
{
    if (m_cfg.blackBackground)
    {
        m_list.SetBkColor(RGB(0, 0, 0));
        m_list.SetTextBkColor(RGB(0, 0, 0));
        m_list.SetTextColor(RGB(255, 255, 255));
    }
    else
    {
        m_list.SetBkColor(::GetSysColor(COLOR_WINDOW));
        m_list.SetTextBkColor(::GetSysColor(COLOR_WINDOW));
        m_list.SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
    }
    m_list.Invalidate();
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
    chk(ID_SWV_GRIDLINES, m_cfg.showGridLines);
    chk(ID_SWV_MARKODDEVEN, m_cfg.markOddEvenRows);
    chk(ID_SWV_BLACKBG, m_cfg.blackBackground);
    chk(ID_SWV_ALIGNNUMERIC, m_cfg.alignNumbersToRight);
    chk(ID_SWV_SHOWGMT, m_cfg.showTimeInGmt);
    chk(ID_SWV_SHOWMS, m_cfg.showMilliseconds);
    chk(ID_SWV_TOOLTIPS, m_cfg.showInfoTip);
}

// ---- data ---------------------------------------------------------------
void CMainFrame::OnBarUpdate()
{
    DoRefresh();
}

void CMainFrame::DoRefresh()
{
    m_bar.ToConfig(m_cfg);
    m_password = m_bar.getPassword();

    HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));

    WmiQueryParams p;
    p.nameSpace = m_cfg.nameSpace;
    p.classOrQuery = m_cfg.classOrQuery;
    p.connectComputer = m_cfg.connectComputer;
    p.computerName = m_cfg.computerName;
    p.userName = m_cfg.userName;
    p.password = m_password;

    WmiResult fresh = RunWmiQuery(p);

    // Keep the visible-column set if the columns are unchanged, else reset.
    bool sameCols = fresh.columns.size() == m_res.columns.size();
    for (size_t i = 0; sameCols && i < fresh.columns.size(); ++i)
    {
        if (_wcsicmp(fresh.columns[i].name.c_str(), m_res.columns[i].name.c_str()) != 0)
        {
            sameCols = false;
        }
    }

    m_res = std::move(fresh);
    if (!sameCols || m_cols.empty())
    {
        m_cols.clear();
        for (int i = 0; i < (int)m_res.columns.size(); ++i)
        {
            m_cols.push_back(i);
        }
        if (m_cfg.sortColumn >= (int)m_res.columns.size())
        {
            m_cfg.sortColumn = 0;
        }
    }

    RebuildColumns();
    BuildDisplay();
    ::SetCursor(prev);

    UpdateTitle();
    UpdateStatus();
}

void CMainFrame::BuildDisplay()
{
    m_display.clear();
    for (int i = 0; i < (int)m_res.rows.size(); ++i)
    {
        m_display.push_back(i);
    }
    SortDisplay();
    m_list.SetItemCount((int)m_display.size());
    m_list.Invalidate();
}

void CMainFrame::SortDisplay()
{
    if (m_cols.empty() || m_display.empty())
    {
        return;
    }
    int col = m_cfg.sortColumn;
    if (col < 0 || col >= (int)m_res.columns.size())
    {
        return;
    }
    bool asc = m_cfg.sortAscending;
    DisplayOptions d = disp();
    std::stable_sort(m_display.begin(), m_display.end(),
                     [&](int a, int b)
                     {
                         const Cell& ca = m_res.rows[a].cells[col];
                         const Cell& cb = m_res.rows[b].cells[col];
                         long long ka, kb;
                         int c;
                         if (ca.numericKey(ka) && cb.numericKey(kb))
                         {
                             c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
                         }
                         else
                         {
                             c = _wcsicmp(ca.text(d).c_str(), cb.text(d).c_str());
                         }
                         return asc ? (c < 0) : (c > 0);
                     });
}

LRESULT CMainFrame::OnColumnClick(int, LPNMHDR p, BOOL&)
{
    int d = reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem;
    if (d < 0 || d >= (int)m_cols.size())
    {
        return 0;
    }
    int clicked = m_cols[d];
    if (clicked == m_cfg.sortColumn)
    {
        m_cfg.sortAscending = !m_cfg.sortAscending;
    }
    else
    {
        m_cfg.sortColumn = clicked;
        m_cfg.sortAscending = true;
    }
    SortDisplay();
    m_list.Invalidate();
    return 0;
}

// ---- owner-data feed ----------------------------------------------------
LRESULT CMainFrame::OnGetDispInfo(int, LPNMHDR p, BOOL&)
{
    auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
    int item = di->item.iItem;
    if (item < 0 || item >= (int)m_display.size())
    {
        return 0;
    }
    if (di->item.mask & LVIF_TEXT)
    {
        int sub = di->item.iSubItem;
        if (sub >= 0 && sub < (int)m_cols.size())
        {
            static std::wstring text;
            text = m_res.rows[m_display[item]].cells[m_cols[sub]].text(disp());
            di->item.pszText = const_cast<LPWSTR>(text.c_str());
        }
    }
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
        if (m_cfg.markOddEvenRows && !m_cfg.blackBackground && (cd->nmcd.dwItemSpec & 1))
        {
            cd->clrTextBk = RGB(240, 240, 240);
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}

LRESULT CMainFrame::OnStateChanged(int, LPNMHDR, BOOL&)
{
    UpdateStatus();
    return 0;
}
LRESULT CMainFrame::OnItemChanged(int, LPNMHDR, BOOL&)
{
    UpdateStatus();
    return 0;
}

// ---- status / title -----------------------------------------------------
void CMainFrame::UpdateStatus()
{
    wchar_t buf[128];
    int sel = m_list.GetSelectedCount();
    if (sel > 0)
    {
        wsprintfW(buf, L"%d item(s), %d Selected", (int)m_display.size(), sel);
    }
    else
    {
        wsprintfW(buf, L"%d item(s)", (int)m_display.size());
    }
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);

    wchar_t t[64];
    wsprintfW(t, L"%u ms", m_res.queryTimeMs);
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, (LPARAM)t);

    if (FAILED(m_res.hr) && m_display.empty())
    {
        wchar_t e[64];
        wsprintfW(e, L"Error 0x%8.8x", (unsigned)m_res.hr);
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 2, (LPARAM)e);
    }
    else
    {
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 2, (LPARAM)L"");
    }
}

void CMainFrame::UpdateTitle()
{
    std::wstring title = L"SimpleWMIView   -   [" + m_cfg.classOrQuery + L"]";
    if (m_cfg.connectComputer == 2 && !m_cfg.computerName.empty())
    {
        title += L"  " + m_cfg.computerName;
    }
    SetWindowTextW(title.c_str());
}

// ---- selection helpers --------------------------------------------------
std::vector<int> CMainFrame::SelectedRows()
{
    std::vector<int> v;
    for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
    {
        v.push_back(m_display[i]);
    }
    return v;
}

// Build a result subset holding the given display rows, keeping columns.
WmiResult CMainFrame::subset(const std::vector<int>& rowIdx)
{
    WmiResult r;
    r.columns = m_res.columns;
    for (int i : rowIdx)
    {
        r.rows.push_back(m_res.rows[i]);
    }
    return r;
}

ExportOptions CMainFrame::expOpt()
{
    ExportOptions o;
    o.display = disp();
    o.encoding = m_cfg.saveFileEncoding;
    o.columns = m_cols;
    return o;
}

// ---- commands -----------------------------------------------------------
LRESULT CMainFrame::OnRefresh(WORD, WORD, HWND, BOOL&)
{
    DoRefresh();
    return 0;
}
LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&)
{
    PostMessage(WM_CLOSE);
    return 0;
}

LRESULT CMainFrame::OnNewInstance(WORD, WORD, HWND, BOOL&)
{
    wchar_t path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    ::ShellExecuteW(m_hWnd, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);
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
LRESULT CMainFrame::OnMarkOddEven(WORD, WORD, HWND, BOOL&)
{
    m_cfg.markOddEvenRows = !m_cfg.markOddEvenRows;
    m_list.Invalidate();
    SyncMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnBlackBg(WORD, WORD, HWND, BOOL&)
{
    m_cfg.blackBackground = !m_cfg.blackBackground;
    ApplyColors();
    SyncMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnAlignNumeric(WORD, WORD, HWND, BOOL&)
{
    m_cfg.alignNumbersToRight = !m_cfg.alignNumbersToRight;
    RebuildColumns();
    SyncMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnShowGmt(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showTimeInGmt = !m_cfg.showTimeInGmt;
    m_list.Invalidate();
    SyncMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnShowMs(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showMilliseconds = !m_cfg.showMilliseconds;
    m_list.Invalidate();
    SyncMenuChecks();
    return 0;
}

LRESULT CMainFrame::OnAutoSize(WORD, WORD, HWND, BOOL&)
{
    for (int i = 0; i < (int)m_cols.size(); ++i)
    {
        m_list.SetColumnWidth(i, LVSCW_AUTOSIZE);
    }
    return 0;
}
LRESULT CMainFrame::OnAutoSizeHdr(WORD, WORD, HWND, BOOL&)
{
    for (int i = 0; i < (int)m_cols.size(); ++i)
    {
        m_list.SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER);
    }
    return 0;
}

LRESULT CMainFrame::OnCopy(WORD, WORD, HWND, BOOL&)
{
    auto rows = SelectedRows();
    if (rows.empty())
    {
        return 0;
    }
    CopyToClipboard(BuildReport(subset(rows), ExportFormat::Tab, expOpt()));
    return 0;
}

LRESULT CMainFrame::OnCopyCell(WORD, WORD, HWND, BOOL&)
{
    if (m_rclickRow >= 0 && m_rclickRow < (int)m_display.size() && m_rclickCol >= 0 && m_rclickCol < (int)m_cols.size())
    {
        CopyToClipboard(m_res.rows[m_display[m_rclickRow]].cells[m_cols[m_rclickCol]].text(disp()));
    }
    return 0;
}

LRESULT CMainFrame::OnCopySorted(WORD, WORD, HWND, BOOL&)
{
    auto rows = SelectedRows();
    if (rows.empty())
    {
        return 0;
    }
    int col = m_cfg.sortColumn;
    std::wstring out;
    for (int r : rows)
    {
        out += m_res.rows[r].cells[col].text(disp());
        out += L"\r\n";
    }
    CopyToClipboard(out);
    return 0;
}

LRESULT CMainFrame::OnSaveSelected(WORD, WORD, HWND, BOOL&)
{
    auto r = SelectedRows();
    return DoSave(r.empty() ? m_display : r);
}
LRESULT CMainFrame::OnSaveAll(WORD, WORD, HWND, BOOL&)
{
    return DoSave(m_display);
}

LRESULT CMainFrame::DoSave(std::vector<int> rows)
{
    CFileDialog dlg(FALSE, L"txt", L"SimpleWMIView.txt", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
                    L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
                    L"Tabular Text File (*.txt)\0*.txt\0Comma Delimited Text File (*.csv)\0*.csv\0"
                    L"HTML File - Horizontal (*.html)\0*.html\0HTML File - Vertical (*.html)\0*.html\0"
                    L"XML File (*.xml)\0*.xml\0JSON File (*.json)\0*.json\0",
                    m_hWnd);
    dlg.m_ofn.lpstrTitle = L"Select a filename to save";
    if (dlg.DoModal() != IDOK)
    {
        return 0;
    }
    static const ExportFormat byIdx[] = {
        ExportFormat::Text,           ExportFormat::Tab,          ExportFormat::Tabular, ExportFormat::Csv,
        ExportFormat::HtmlHorizontal, ExportFormat::HtmlVertical, ExportFormat::Xml,     ExportFormat::Json};
    DWORD fi = dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex - 1 : 0;
    WriteReport(dlg.m_szFileName, subset(rows), byIdx[std::min<DWORD>(fi, 7)], expOpt());
    return 0;
}

LRESULT CMainFrame::OnHtmlAll(WORD, WORD, HWND, BOOL&)
{
    ShowHtml(m_display);
    return 0;
}
LRESULT CMainFrame::OnHtmlSel(WORD, WORD, HWND, BOOL&)
{
    auto r = SelectedRows();
    ShowHtml(r.empty() ? m_display : r);
    return 0;
}

void CMainFrame::ShowHtml(const std::vector<int>& rows)
{
    wchar_t dir[MAX_PATH], tmp[MAX_PATH];
    ::GetTempPathW(MAX_PATH, dir);
    ::GetTempFileNameW(dir, L"swv", 0, tmp);
    std::wstring html = tmp;
    html += L".html";
    WriteReport(html, subset(rows), ExportFormat::HtmlHorizontal, expOpt());
    ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CMainFrame::OnChooseColumns(WORD, WORD, HWND, BOOL&)
{
    if (m_res.columns.empty())
    {
        return 0;
    }
    CColumnsDlg dlg(m_res, m_cols);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        if (std::find(m_cols.begin(), m_cols.end(), m_cfg.sortColumn) == m_cols.end() && !m_cols.empty())
        {
            m_cfg.sortColumn = m_cols.front();
        }
        RebuildColumns();
        m_list.Invalidate();
    }
    return 0;
}

LRESULT CMainFrame::OnProperties(WORD, WORD, HWND, BOOL&)
{
    int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
    if (i < 0 || i >= (int)m_display.size())
    {
        return 0;
    }
    CPropertiesDlg dlg(m_res, m_cols, disp(), m_display[i]);
    dlg.DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&)
{
    CAboutDlg().DoModal(m_hWnd);
    return 0;
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
    if (what.empty() || m_display.empty())
    {
        return;
    }
    std::wstring needle = what;
    std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
    int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
    int n = (int)m_display.size();
    DisplayOptions d = disp();
    for (int step = 1; step <= n; ++step)
    {
        int i = down ? (start + step) % n : ((start - step) % n + n) % n;
        for (int c : m_cols)
        {
            std::wstring v = m_res.rows[m_display[i]].cells[c].text(d);
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

// ---- notifications ------------------------------------------------------
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
    m_bar.ToConfig(m_cfg);
    m_cfg.placement.length = sizeof(m_cfg.placement);
    if (GetWindowPlacement(&m_cfg.placement))
    {
        m_cfg.havePlacement = true;
    }
    m_cfg.save();
    PostQuitMessage(0);
}
