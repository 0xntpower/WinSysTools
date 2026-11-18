// MainFrame.cpp -- the ProductKeyScanner main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The bodies
// are unchanged: the worker-thread scan over EnumerateProductKeys(), the owner-data
// ListView feed, the sort, the clipboard and save commands, the HTML report, the
// find dialog, the config load and save, and the toolbar-light fix (PinToolbarLight
// after the toolbar is made, OnEraseBkgnd filling the frame with button-face).
#include "MainFrame.h"

#include "ProductKey.h"
#include "Columns.h"
#include "Export.h"
#include "Scanner.h"
#include "Dialogs.h"
#include "../common/UiUtil.h"
#include "../common/CfgIni.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

CMainFrame::CMainFrame()
{
    for (int i = 0; i < kColumnCount; ++i)
    {
        m_visible.push_back(i);
        m_widths.push_back(kColumns[i].width);
    }
}

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
    if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE && m_scanning.load())
    {
        m_stop.store(true);
        return TRUE;
    }
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
    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

    CreateToolbar();
    m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);

    m_hWndClient = m_list.Create(m_hWnd, rcDefault, nullptr,
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                                 WS_EX_CLIENTEDGE, IDC_LISTVIEW);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP);

    m_images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 1);
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_TYPE)))
    {
        m_images.AddIcon(hi);
    }
    m_list.SetImageList(m_images, LVSIL_SMALL);

    SetWindowTextW(L"ProductKeyScanner");
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    LoadConfig();
    RebuildColumns();
    ApplyGridLines();
    ApplyFont();
    UpdateMenuChecks();

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    // The original shows the Scan Options dialog on startup (readme).
    PostMessageW(WM_COMMAND, MAKEWPARAM(ID_FILE_SCANOPTIONS, 0));
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
    const int cmds[] = {ID_FILE_SCANOPTIONS, ID_FILE_SAVE, ID_EDIT_COPY,
                        ID_FILE_PROPERTIES,  ID_EDIT_FIND, ID_VIEW_REFRESH};
    TBBUTTON b[6] = {};
    for (int i = 0; i < 6; ++i)
    {
        b[i].iBitmap = i;
        b[i].idCommand = cmds[i];
        b[i].fsState = TBSTATE_ENABLED;
        b[i].fsStyle = TBSTYLE_BUTTON;
    }
    tb.AddButtons(6, b);
    m_hWndToolBar = tb;
}

void CMainFrame::UpdateMenuChecks()
{
    if (HMENU menu = GetMenu())
    {
        ::CheckMenuItem(menu, ID_VIEW_GRIDLINES, MF_BYCOMMAND | (m_gridLines ? MF_CHECKED : MF_UNCHECKED));
        ::CheckMenuItem(menu, ID_VIEW_TOOLTIPS, MF_BYCOMMAND | (m_showInfoTip ? MF_CHECKED : MF_UNCHECKED));
        ::CheckMenuItem(menu, ID_VIEW_MARKODDEVEN, MF_BYCOMMAND | (m_markOddEven ? MF_CHECKED : MF_UNCHECKED));
        ::CheckMenuItem(menu, ID_OPTIONS_ADDHEADER, MF_BYCOMMAND | (m_addHeader ? MF_CHECKED : MF_UNCHECKED));
    }
}

// ---- columns ------------------------------------------------------------
void CMainFrame::RebuildColumns()
{
    while (m_list.DeleteColumn(0))
    {
    }
    int i = 0;
    for (int c : m_visible)
    {
        const int w = (i < static_cast<int>(m_widths.size())) ? m_widths[i] : kColumns[c].width;
        m_list.InsertColumn(i, kColumns[c].title, kColumns[c].format, w);
        ++i;
    }
}

void CMainFrame::ApplyGridLines()
{
    DWORD ex = m_list.GetExtendedListViewStyle();
    ex = m_gridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
    m_list.SetExtendedListViewStyle(ex);
}

void CMainFrame::ApplyFont()
{
    if (m_font)
    {
        ::DeleteObject(m_font);
        m_font = nullptr;
    }
    if (m_haveFont)
    {
        m_font = ::CreateFontIndirectW(&m_logFont);
        m_list.SetFont(m_font, TRUE);
    }
    else
    {
        m_list.SetFont((HFONT)::GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
}

// ---- scanning (worker thread) ------------------------------------------
DWORD WINAPI CMainFrame::ScanProc(LPVOID p)
{
    auto* arg = static_cast<ScanArg*>(p);
    auto* result = new std::vector<ProductKeyRecord>(EnumerateProductKeys(arg->opts, &arg->self->m_stop));
    ::PostMessageW(arg->self->m_hWnd, WM_APP_SCANDONE, 0, reinterpret_cast<LPARAM>(result));
    delete arg;
    return 0;
}

void CMainFrame::StartScan()
{
    if (m_scanning.load())
    {
        return;
    }
    WaitThread();
    m_items.clear();
    m_list.SetItemCount(0);
    m_list.Invalidate();
    m_stop.store(false);
    m_scanning.store(true);
    ::SetWindowTextW(m_hWndStatusBar, L"Loading...");
    ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
    auto* arg = new ScanArg{this, m_opts};
    m_thread = ::CreateThread(nullptr, 0, ScanProc, arg, 0, nullptr);
    if (!m_thread)
    {
        delete arg;
        m_scanning.store(false);
    }
}

LRESULT CMainFrame::OnScanDone(UINT, WPARAM, LPARAM lParam, BOOL&)
{
    std::unique_ptr<std::vector<ProductKeyRecord>> result(reinterpret_cast<std::vector<ProductKeyRecord>*>(lParam));
    m_scanning.store(false);
    WaitThread();
    m_items = std::move(*result);
    SortItems();
    m_list.SetItemCount(static_cast<int>(m_items.size()));
    m_list.Invalidate();
    UpdateStatus();
    ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
    return 0;
}

void CMainFrame::WaitThread()
{
    if (m_thread)
    {
        ::WaitForSingleObject(m_thread, INFINITE);
        ::CloseHandle(m_thread);
        m_thread = nullptr;
    }
}

// ---- sorting ------------------------------------------------------------
void CMainFrame::SortItems()
{
    const int col = m_sortCol, dir = m_sortAsc ? 1 : -1;
    std::stable_sort(m_items.begin(), m_items.end(),
                     [col, dir](const ProductKeyRecord& a, const ProductKeyRecord& b)
                     {
                         return dir * _wcsicmp(a.column(col).c_str(), b.column(col).c_str()) < 0;
                     });
}

LRESULT CMainFrame::OnColumnClick(int, LPNMHDR p, BOOL&)
{
    int clicked = m_visible[reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem];
    if (clicked == m_sortCol)
    {
        m_sortAsc = !m_sortAsc;
    }
    else
    {
        m_sortCol = clicked;
        m_sortAsc = true;
    }
    SortItems();
    m_list.Invalidate();
    return 0;
}

// ---- owner-data feed ----------------------------------------------------
LRESULT CMainFrame::OnGetDispInfo(int, LPNMHDR p, BOOL&)
{
    auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
    int item = di->item.iItem;
    if (item < 0 || item >= static_cast<int>(m_items.size()))
    {
        return 0;
    }
    int col = m_visible[di->item.iSubItem];
    if (di->item.mask & LVIF_TEXT)
    {
        static std::wstring text;
        text = m_items[item].column(col);
        di->item.pszText = const_cast<LPWSTR>(text.c_str());
    }
    if ((di->item.mask & LVIF_IMAGE) && di->item.iSubItem == 0)
    {
        di->item.iImage = 0;
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
        if (m_markOddEven && (cd->nmcd.dwItemSpec & 1))
        {
            cd->clrTextBk = RGB(240, 240, 240);
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}

// ---- status -------------------------------------------------------------
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
    ::SetWindowTextW(m_hWndStatusBar, buf);
}

// ---- selection helpers --------------------------------------------------
std::vector<int> CMainFrame::SelectedIndices()
{
    std::vector<int> v;
    for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
    {
        v.push_back(i);
    }
    return v;
}
std::vector<ProductKeyRecord> CMainFrame::SelectedItems()
{
    std::vector<ProductKeyRecord> v;
    for (int i : SelectedIndices())
    {
        v.push_back(m_items[i]);
    }
    return v;
}

// ---- commands -----------------------------------------------------------
LRESULT CMainFrame::OnScanOptions(WORD, WORD, HWND, BOOL&)
{
    CScanOptionsDlg dlg(m_opts);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        m_opts = dlg.m_opts;
        StartScan();
    }
    return 0;
}
LRESULT CMainFrame::OnRefresh(WORD, WORD, HWND, BOOL&)
{
    StartScan();
    return 0;
}
LRESULT CMainFrame::OnStop(WORD, WORD, HWND, BOOL&)
{
    if (m_scanning.load())
    {
        m_stop.store(true);
    }
    return 0;
}
LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&)
{
    PostMessage(WM_CLOSE);
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
    m_gridLines = !m_gridLines;
    ApplyGridLines();
    UpdateMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnTooltips(WORD, WORD, HWND, BOOL&)
{
    m_showInfoTip = !m_showInfoTip;
    UpdateMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnMarkOddEven(WORD, WORD, HWND, BOOL&)
{
    m_markOddEven = !m_markOddEven;
    UpdateMenuChecks();
    m_list.Invalidate();
    return 0;
}
LRESULT CMainFrame::OnAddHeader(WORD, WORD, HWND, BOOL&)
{
    m_addHeader = !m_addHeader;
    UpdateMenuChecks();
    return 0;
}

LRESULT CMainFrame::OnAutoSize(WORD, WORD, HWND, BOOL&)
{
    for (int i = 0; i < (int)m_visible.size(); ++i)
    {
        m_list.SetColumnWidth(i, LVSCW_AUTOSIZE);
    }
    return 0;
}
LRESULT CMainFrame::OnAutoSizeHdr(WORD, WORD, HWND, BOOL&)
{
    for (int i = 0; i < (int)m_visible.size(); ++i)
    {
        m_list.SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER);
    }
    return 0;
}

LRESULT CMainFrame::OnSelectFont(WORD, WORD, HWND, BOOL&)
{
    LOGFONTW lf = m_haveFont ? m_logFont : LOGFONTW{};
    if (!m_haveFont)
    {
        HFONT cur = m_list.GetFont();
        if (!cur)
        {
            cur = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
        }
        ::GetObjectW(cur, sizeof(lf), &lf);
    }
    CHOOSEFONTW cf{sizeof(cf)};
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
LRESULT CMainFrame::OnDefaultFont(WORD, WORD, HWND, BOOL&)
{
    m_haveFont = false;
    ApplyFont();
    return 0;
}

LRESULT CMainFrame::OnCopy(WORD, WORD, HWND, BOOL&)
{
    auto sel = SelectedItems();
    if (sel.empty())
    {
        return 0;
    }
    CopyToClipboard(BuildReport(sel, m_visible, ExportFormat::Tab, {m_addHeader}));
    return 0;
}

// Copy Product Key (Ctrl+K): only the Product Key column of the selection.
LRESULT CMainFrame::OnCopyKey(WORD, WORD, HWND, BOOL&)
{
    std::wstring text;
    for (int i : SelectedIndices())
    {
        text += m_items[i].productKey + L"\r\n";
    }
    if (!text.empty())
    {
        CopyToClipboard(text);
    }
    return 0;
}

LRESULT CMainFrame::OnSave(WORD, WORD, HWND, BOOL&)
{
    auto sel = SelectedItems();
    if (sel.empty())
    {
        sel = m_items;
    }

    CFileDialog dlg(FALSE, L"txt", L"ProductKeys.txt", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
                    L"JSON File (*.json)\0*.json\0HTML File - Horizontal (*.htm;*.html)\0*.htm;*.html\0"
                    L"HTML File - Vertical (*.htm;*.html)\0*.htm;*.html\0XML File (*.xml)\0*.xml\0"
                    L"Comma Delimited Text File (*.csv)\0*.csv\0",
                    m_hWnd);
    dlg.m_ofn.lpstrTitle = L"Select a filename to save";
    dlg.m_ofn.nFilterIndex = m_saveFilterIndex;
    if (dlg.DoModal() != IDOK)
    {
        return 0;
    }

    m_saveFilterIndex = dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex : 1;
    static const ExportFormat byIndex[] = {
        ExportFormat::Text,         ExportFormat::Tab, ExportFormat::Json, ExportFormat::HtmlHorizontal,
        ExportFormat::HtmlVertical, ExportFormat::Xml, ExportFormat::Csv};
    ExportFormat fmt = byIndex[std::min<DWORD>(m_saveFilterIndex - 1, 6)];
    WriteReport(dlg.m_szFileName, sel, m_visible, fmt, {m_addHeader});
    return 0;
}

LRESULT CMainFrame::OnHtmlAll(WORD, WORD, HWND, BOOL&)
{
    ShowHtmlReport(m_items);
    return 0;
}
LRESULT CMainFrame::OnHtmlSel(WORD, WORD, HWND, BOOL&)
{
    auto sel = SelectedItems();
    if (sel.empty())
    {
        sel = m_items;
    }
    ShowHtmlReport(sel);
    return 0;
}
void CMainFrame::ShowHtmlReport(const std::vector<ProductKeyRecord>& items)
{
    wchar_t dir[MAX_PATH];
    ::GetTempPathW(MAX_PATH, dir);
    std::wstring html = std::wstring(dir) + L"report.html";
    WriteReport(html, items, m_visible, ExportFormat::HtmlHorizontal, {m_addHeader});
    ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CMainFrame::OnChooseColumns(WORD, WORD, HWND, BOOL&)
{
    CColumnsDlg dlg(m_visible);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        m_visible = dlg.m_visible;
        m_widths.clear();
        for (int c : m_visible)
        {
            m_widths.push_back(kColumns[c].width);
        }
        if (std::find(m_visible.begin(), m_visible.end(), m_sortCol) == m_visible.end() && !m_visible.empty())
        {
            m_sortCol = m_visible.front();
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
    CPropertiesDlg dlg(m_items[i], m_visible);
    dlg.DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&)
{
    CAboutDlg().DoModal(m_hWnd);
    return 0;
}

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
LRESULT CMainFrame::OnFindReplace(UINT, WPARAM, LPARAM lParam, BOOL&)
{
    auto* fr = reinterpret_cast<LPFINDREPLACEW>(lParam);
    if (fr->Flags & FR_DIALOGTERM)
    {
        m_pFind = nullptr;
        return 0;
    }
    if (fr->Flags & FR_FINDNEXT)
    {
        FindNext(fr->lpstrFindWhat, (fr->Flags & FR_DOWN) != 0);
    }
    return 0;
}
void CMainFrame::FindNext(const std::wstring& needle, bool down)
{
    if (needle.empty() || m_items.empty())
    {
        return;
    }
    int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
    int n = (int)m_items.size();
    std::wstring low = needle;
    std::transform(low.begin(), low.end(), low.begin(), towlower);
    for (int step = 1; step <= n; ++step)
    {
        int i = down ? (start + step) % n : (start - step + n) % n;
        std::wstring row;
        for (int c : m_visible)
        {
            row += m_items[i].column(c) + L" ";
        }
        std::transform(row.begin(), row.end(), row.begin(), towlower);
        if (row.find(low) != std::wstring::npos)
        {
            m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            m_list.EnsureVisible(i, FALSE);
            return;
        }
    }
}

LRESULT CMainFrame::OnDblClk(int, LPNMHDR, BOOL&)
{
    BOOL b;
    OnProperties(0, 0, nullptr, b);
    return 0;
}
LRESULT CMainFrame::OnRClick(int, LPNMHDR, BOOL&)
{
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

// ---- config -------------------------------------------------------------
// The original keeps settings in a .cfg INI file next to the executable,
// section [General] (path builder sub_14000D300, value table sub_1400021BC).
// Scalars are decimal, SoftwareHivePath is a string, and MainFont / Columns /
// WinPos are the original uppercase space-separated hex blobs. Columns is an
// array of (WORD width, WORD columnIndex) pairs in display order.
void CMainFrame::SetCfgPath(const std::wstring& arg)
{
    m_cfgPath = cfgini::Resolve(arg);
}

void CMainFrame::LoadConfig()
{
    m_opts.useWMI = true;
    m_opts.useBIOS = true;
    if (m_noSaveLoad)
    {
        return;
    }
    const std::wstring& p = m_cfgPath;
    m_sortCol = std::clamp(cfgini::GetInt(p, L"Sort", m_sortCol), 0, kColumnCount - 1);
    m_gridLines = cfgini::GetInt(p, L"ShowGridLines", m_gridLines) != 0;
    m_showInfoTip = cfgini::GetInt(p, L"ShowInfoTip", m_showInfoTip) != 0;
    m_markOddEven = cfgini::GetInt(p, L"MarkOddEvenRows", m_markOddEven) != 0;
    m_addHeader = cfgini::GetInt(p, L"AddExportHeaderLine", m_addHeader) != 0;
    m_saveFilterIndex = std::clamp(cfgini::GetInt(p, L"SaveFilterIndex", m_saveFilterIndex), 1, 7);
    m_opts.useWMI = cfgini::GetInt(p, L"UseWMI", m_opts.useWMI) != 0;
    m_opts.useBIOS = cfgini::GetInt(p, L"UseBIOS", m_opts.useBIOS) != 0;
    m_opts.source = (cfgini::GetInt(p, L"DataSource", 1) == 2) ? ScanSource::ExternalHive : ScanSource::CurrentSystem;
    if (std::wstring hive = cfgini::GetStr(p, L"SoftwareHivePath"); !hive.empty())
    {
        m_opts.hivePath = hive;
    }
    if (auto f = cfgini::GetBlob(p, L"MainFont"); f.size() >= sizeof(LOGFONTW))
    {
        memcpy(&m_logFont, f.data(), sizeof(LOGFONTW));
        m_haveFont = true;
    }
    if (auto cols = cfgini::GetBlob(p, L"Columns"); cols.size() >= 4)
    {
        std::vector<int> vis, wid;
        for (size_t i = 0; i + 4 <= cols.size(); i += 4)
        {
            const int w = cols[i] | (cols[i + 1] << 8);
            const int c = cols[i + 2] | (cols[i + 3] << 8);
            if (c >= 0 && c < kColumnCount)
            {
                vis.push_back(c);
                wid.push_back(w > 0 ? w : kColumns[c].width);
            }
        }
        if (!vis.empty())
        {
            m_visible = vis;
            m_widths = wid;
        }
    }
    if (auto wp = cfgini::GetBlob(p, L"WinPos"); wp.size() >= sizeof(WINDOWPLACEMENT))
    {
        WINDOWPLACEMENT pl{};
        memcpy(&pl, wp.data(), sizeof(pl));
        pl.length = sizeof(pl);
        SetWindowPlacement(&pl);
        m_startShowCmd = pl.showCmd;
    }
}

void CMainFrame::SaveConfig()
{
    if (m_noSaveLoad)
    {
        return;
    }
    const std::wstring& p = m_cfgPath;
    cfgini::PutInt(p, L"Sort", m_sortCol);
    cfgini::PutInt(p, L"ShowGridLines", m_gridLines ? 1 : 0);
    cfgini::PutInt(p, L"ShowInfoTip", m_showInfoTip ? 1 : 0);
    cfgini::PutInt(p, L"MarkOddEvenRows", m_markOddEven ? 1 : 0);
    cfgini::PutInt(p, L"AddExportHeaderLine", m_addHeader ? 1 : 0);
    cfgini::PutInt(p, L"SaveFilterIndex", m_saveFilterIndex);
    cfgini::PutInt(p, L"UseWMI", m_opts.useWMI ? 1 : 0);
    cfgini::PutInt(p, L"UseBIOS", m_opts.useBIOS ? 1 : 0);
    cfgini::PutInt(p, L"DataSource", m_opts.source == ScanSource::ExternalHive ? 2 : 1);
    cfgini::PutStr(p, L"SoftwareHivePath", m_opts.hivePath);
    if (m_haveFont)
    {
        cfgini::PutBlob(p, L"MainFont", &m_logFont, sizeof(m_logFont));
    }

    std::vector<BYTE> blob;
    for (size_t i = 0; i < m_visible.size(); ++i)
    {
        const int c = m_visible[i];
        int w = (i < m_widths.size()) ? m_widths[i] : kColumns[c].width;
        if (m_list.IsWindow())
        {
            const int live = m_list.GetColumnWidth(static_cast<int>(i));
            if (live > 0)
            {
                w = live;
            }
        }
        const WORD ww = static_cast<WORD>(w);
        const WORD cc = static_cast<WORD>(c);
        blob.push_back(ww & 0xFF);
        blob.push_back((ww >> 8) & 0xFF);
        blob.push_back(cc & 0xFF);
        blob.push_back((cc >> 8) & 0xFF);
    }
    if (!blob.empty())
    {
        cfgini::PutBlob(p, L"Columns", blob.data(), blob.size());
    }

    WINDOWPLACEMENT pl{sizeof(pl)};
    GetWindowPlacement(&pl);
    cfgini::PutBlob(p, L"WinPos", &pl, sizeof(pl));
}

void CMainFrame::OnDestroy()
{
    m_stop.store(true);
    WaitThread();
    SaveConfig();
    if (m_font)
    {
        ::DeleteObject(m_font);
        m_font = nullptr;
    }
    PostQuitMessage(0);
}
