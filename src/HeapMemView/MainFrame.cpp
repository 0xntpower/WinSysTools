// MainFrame.cpp -- the HeapMemView main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the toolbar and panes built in OnCreate, the draggable
// horizontal divider, the heap-block scan and sort, the hex-dump lower pane, the
// find, copy, save and export commands, the config load and save, and the
// toolbar-light fix (PinToolbarLight after the toolbar is made, OnEraseBkgnd
// filling the frame with button-face).
#include "MainFrame.h"

#include "HeapEnumerator.h"
#include "HeapBlock.h"
#include "ProcessList.h"
#include "HexDump.h"
#include "Columns.h"
#include "Config.h"
#include "Export.h"
#include "Dialogs.h"
#include "../common/UiUtil.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_set>

CMainFrame::CMainFrame()
{
    m_cfgPath = ConfigPath();
    m_sortCol = 0;
}

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

// ---- creation -------------------------------------------------------
int CMainFrame::OnCreate(LPCREATESTRUCT)
{
    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));
    m_splitCursor = ::LoadCursorW(nullptr, IDC_SIZENS);

    CreateToolbar();
    m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);

    m_list.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
                  WS_EX_CLIENTEDGE, IDC_LISTVIEW);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);

    m_images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 1);
    if (HICON hi =
            (HICON)::LoadImageW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_ITEM), IMAGE_ICON, 16, 16, 0))
    {
        m_images.AddIcon(hi);
    }
    m_list.SetImageList(m_images, LVSIL_SMALL);

    // Lower pane: read-only multiline edit in Courier New, like the original.
    m_edit.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, WS_EX_CLIENTEDGE,
                  IDC_HEXEDIT);
    HDC dc = ::GetDC(nullptr);
    int h = -10 * ::GetDeviceCaps(dc, LOGPIXELSY) / 72;
    ::ReleaseDC(nullptr, dc);
    m_editFont.CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
    m_edit.SetFont(m_editFont);

    SetWindowTextW(L"HeapMemView");
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    LoadConfig();
    RebuildColumns();
    ApplyGridLines();
    UpdateMenuChecks();

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    UpdateStatus();
    // The main window opens empty, like the original. The user picks a process
    // with File / Select Process (Ctrl+P) or the first toolbar button.
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

    // Button order and bitmap indices recovered from the original TBBUTTON array.
    struct B
    {
        int bmp;
        int cmd;
    };
    const B buttons[] = {
        {6, ID_FILE_SELECTPROC}, {-1, 0}, {1, ID_FILE_SAVE}, {7, ID_OPT_FILTER},
        {5, ID_VIEW_REFRESH},    {-1, 0}, {2, ID_EDIT_COPY}, {3, ID_FILE_PROPERTIES},
        {0, ID_EDIT_FIND},       {-1, 0}, {4, ID_FILE_EXIT},
    };
    std::vector<TBBUTTON> tbb;
    for (const B& b : buttons)
    {
        TBBUTTON t{};
        if (b.bmp < 0)
        {
            t.fsStyle = TBSTYLE_SEP;
        }
        else
        {
            t.iBitmap = b.bmp;
            t.idCommand = b.cmd;
            t.fsState = TBSTATE_ENABLED;
            t.fsStyle = TBSTYLE_BUTTON;
        }
        tbb.push_back(t);
    }
    tb.AddButtons((int)tbb.size(), tbb.data());
    m_hWndToolBar = tb;
}

// ---- layout ---------------------------------------------------------
void CMainFrame::OnSize(UINT type, CSize)
{
    if (type == SIZE_MINIMIZED)
    {
        return;
    }
    if (m_hWndToolBar)
    {
        ::SendMessageW(m_hWndToolBar, WM_SIZE, 0, 0);
    }
    if (m_hWndStatusBar)
    {
        ::SendMessageW(m_hWndStatusBar, WM_SIZE, 0, 0);
        LayoutStatusParts();
    }

    RECT rc;
    GetClientRect(&rc);
    int tbH = 0, sbH = 0;
    if (m_hWndToolBar && ::IsWindowVisible(m_hWndToolBar))
    {
        RECT r;
        ::GetWindowRect(m_hWndToolBar, &r);
        tbH = r.bottom - r.top;
    }
    if (m_hWndStatusBar && ::IsWindowVisible(m_hWndStatusBar))
    {
        RECT r;
        ::GetWindowRect(m_hWndStatusBar, &r);
        sbH = r.bottom - r.top;
    }

    m_bandTop = rc.top + tbH;
    m_bandBottom = rc.bottom - sbH;
    int bandH = m_bandBottom - m_bandTop;
    if (bandH < kDivider + 20)
    {
        return;
    }

    int upper = static_cast<int>((int64_t)(bandH - kDivider) * m_s.verSplitLoc / kSplitRange);
    upper = std::clamp(upper, 20, bandH - kDivider - 20);

    m_list.SetWindowPos(nullptr, rc.left, m_bandTop, rc.right - rc.left, upper, SWP_NOZORDER);
    int editTop = m_bandTop + upper + kDivider;
    m_edit.SetWindowPos(nullptr, rc.left, editTop, rc.right - rc.left, m_bandBottom - editTop, SWP_NOZORDER);
}

int CMainFrame::DividerY() const
{
    int bandH = m_bandBottom - m_bandTop;
    int upper = static_cast<int>((int64_t)(bandH - kDivider) * m_s.verSplitLoc / kSplitRange);
    upper = std::clamp(upper, 20, bandH - kDivider - 20);
    return m_bandTop + upper;
}

bool CMainFrame::OverDivider(int y) const
{
    int dy = DividerY();
    return y >= dy && y < dy + kDivider;
}

BOOL CMainFrame::OnSetCursor(CWindow wnd, UINT, UINT)
{
    POINT pt;
    ::GetCursorPos(&pt);
    ScreenToClient(&pt);
    if (wnd.m_hWnd == m_hWnd && OverDivider(pt.y))
    {
        ::SetCursor(m_splitCursor);
        return TRUE;
    }
    SetMsgHandled(FALSE);
    return FALSE;
}

void CMainFrame::OnLButtonDown(UINT, CPoint pt)
{
    if (OverDivider(pt.y))
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
    int bandH = m_bandBottom - m_bandTop;
    if (bandH <= kDivider)
    {
        return;
    }
    int upper = std::clamp<int>(pt.y - m_bandTop, 20, bandH - kDivider - 20);
    m_s.verSplitLoc = static_cast<int>((int64_t)upper * kSplitRange / (bandH - kDivider));
    RECT rc;
    GetClientRect(&rc);
    OnSize(SIZE_RESTORED, CSize(rc.right, rc.bottom));
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

void CMainFrame::LayoutStatusParts()
{
    RECT rc;
    ::GetClientRect(m_hWndStatusBar, &rc);
    int parts[2] = {rc.right - 140, -1};
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, (LPARAM)parts);
}

void CMainFrame::OnSetFocus(CWindow)
{
    m_list.SetFocus();
}

void CMainFrame::UpdateMenuChecks()
{
    HMENU menu = GetMenu();
    if (!menu)
    {
        return;
    }
    ::CheckMenuItem(menu, ID_VIEW_GRIDLINES, MF_BYCOMMAND | (m_s.showGridLines ? MF_CHECKED : MF_UNCHECKED));
    ::CheckMenuItem(menu, ID_OPT_DISPLAY_HEADER, MF_BYCOMMAND | (m_s.displayMemHeader ? MF_CHECKED : MF_UNCHECKED));
    ::CheckMenuItem(menu, ID_OPT_MARK_NEW, MF_BYCOMMAND | (m_s.markNew ? MF_CHECKED : MF_UNCHECKED));
    ::CheckMenuItem(menu, ID_OPT_ABOVE127, MF_BYCOMMAND | (m_s.displayAbove127 ? MF_CHECKED : MF_UNCHECKED));
    ::CheckMenuItem(menu, ID_OPT_AUTOSORT, MF_BYCOMMAND | (m_s.autoSortOnRefresh ? MF_CHECKED : MF_UNCHECKED));
    ::CheckMenuRadioItem(menu, ID_PREVIEW_NONE, ID_PREVIEW_HEX, ID_PREVIEW_NONE + m_s.dataPreviewMode, MF_BYCOMMAND);
}

// ---- columns --------------------------------------------------------
void CMainFrame::RebuildColumns()
{
    while (m_list.DeleteColumn(0))
    {
    }
    int i = 0;
    for (int c : m_s.visible)
    {
        m_list.InsertColumn(i++, kColumns[c].title, kColumns[c].format, m_s.width[c]);
    }
}

void CMainFrame::ApplyGridLines()
{
    DWORD ex = m_list.GetExtendedListViewStyle();
    ex = m_s.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
    m_list.SetExtendedListViewStyle(ex);
}

// ---- data -----------------------------------------------------------
HeapScanOptions CMainFrame::ScanOptions() const
{
    HeapScanOptions o;
    o.useFromSize = m_s.filterUseFrom;
    o.fromSize = (uint32_t)m_s.filterFromSize;
    o.useToSize = m_s.filterUseTo;
    o.toSize = (uint32_t)m_s.filterToSize;
    return o;
}

void CMainFrame::Refresh()
{
    if (m_pid == 0)
    {
        m_items.clear();
        m_list.SetItemCount(0);
        m_list.Invalidate();
        UpdateStatus();
        return;
    }

    // Keep the previous (heap, header) keys to mark newly appeared blocks.
    std::unordered_set<uint64_t> previous;
    if (m_s.markNew && !m_firstLoad)
    {
        for (const auto& b : m_items)
        {
            previous.insert(b.header ^ (b.heap << 1));
        }
    }

    HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
    auto fresh = EnumerateHeapBlocks(m_pid, ScanOptions());
    ::SetCursor(prev);

    if (m_s.markNew && !m_firstLoad)
    {
        for (auto& b : fresh)
        {
            b.isNew = previous.find(b.header ^ (b.heap << 1)) == previous.end();
        }
    }

    m_items = std::move(fresh);
    m_firstLoad = false;

    if (m_s.autoSortOnRefresh)
    {
        SortItems();
    }
    m_list.SetItemCount((int)m_items.size());
    m_list.Invalidate();
    UpdateStatus();
    UpdateLowerPane();
}

void CMainFrame::SortItems()
{
    const int col = m_sortCol;
    const bool asc = m_sortAsc;
    std::stable_sort(m_items.begin(), m_items.end(),
                     [col, asc](const HeapBlock& a, const HeapBlock& b)
                     {
                         int c = a.compare(b, col);
                         return asc ? (c < 0) : (c > 0);
                     });
}

LRESULT CMainFrame::OnColumnClick(int, LPNMHDR p, BOOL&)
{
    int clicked = m_s.visible[reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem];
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

// Lazily compute the Data Preview text for a row the first time it is shown.
void CMainFrame::EnsurePreview(HeapBlock& b)
{
    if (b.previewDone)
    {
        return;
    }
    b.previewDone = true;
    if (m_s.dataPreviewMode == 0)
    {
        return;
    }
    int n = std::clamp(m_s.dataPreviewBytes, 0, 4096);
    if (n <= 0)
    {
        return;
    }
    std::vector<BYTE> buf(n);
    SIZE_T got = ReadProcessBytes(m_pid, b.dataAddress() + (uint32_t)m_s.dataPreviewStartByte, buf.data(), n);
    buf.resize(got);
    b.preview = BuildPreview(buf, static_cast<PreviewMode>(m_s.dataPreviewMode), m_s.displayAbove127);
}

LRESULT CMainFrame::OnGetDispInfo(int, LPNMHDR p, BOOL&)
{
    auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
    int item = di->item.iItem;
    if (item < 0 || item >= (int)m_items.size())
    {
        return 0;
    }
    int col = m_s.visible[di->item.iSubItem];
    if (di->item.mask & LVIF_TEXT)
    {
        if (col == 6)
        {
            EnsurePreview(m_items[item]);
        }
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

LRESULT CMainFrame::OnCustomDraw(int, LPNMHDR p, BOOL&)
{
    auto* cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(p);
    switch (cd->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
    {
        int i = (int)cd->nmcd.dwItemSpec;
        if (m_s.markNew && i >= 0 && i < (int)m_items.size() && m_items[i].isNew)
        {
            cd->clrTextBk = m_s.newMarkColor;
        }
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

LRESULT CMainFrame::OnItemChanged(int, LPNMHDR, BOOL&)
{
    UpdateStatus();
    UpdateLowerPane();
    return 0;
}

void CMainFrame::UpdateStatus()
{
    wchar_t buf[64];
    swprintf(buf, 64, L"%d Memory Blocks", (int)m_items.size());
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
    int sel = m_list.GetSelectedCount();
    if (sel > 0)
    {
        swprintf(buf, 64, L"%d Selected", sel);
    }
    else
    {
        buf[0] = 0;
    }
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, (LPARAM)buf);
}

// ---- lower pane -----------------------------------------------------
void CMainFrame::UpdateLowerPane()
{
    int i = FocusedIndex();
    if (i < 0 || i >= (int)m_items.size())
    {
        m_edit.SetWindowTextW(L"");
        return;
    }
    const HeapBlock& b = m_items[i];
    bool header = m_s.displayMemHeader;
    uint64_t base = header ? b.header : b.dataAddress();
    uint32_t len = header ? b.rawSize : b.blockSize();
    len = std::min<uint32_t>(len, 0x10000); // cap the dump at 64 KB
    std::vector<BYTE> buf(len);
    SIZE_T got = ReadProcessBytes(m_pid, base, buf.data(), len);
    buf.resize(got);
    std::wstring dump = BuildHexDump(buf, base, m_s.bytesPerLine, m_s.displayMemoryAddress, m_s.displayAbove127);
    m_edit.SetWindowTextW(dump.c_str());
}

// ---- selection helpers ---------------------------------------------
std::vector<int> CMainFrame::SelectedIndices()
{
    std::vector<int> v;
    for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
    {
        v.push_back(i);
    }
    return v;
}
std::vector<HeapBlock> CMainFrame::SelectedItems()
{
    std::vector<HeapBlock> v;
    for (int i : SelectedIndices())
    {
        v.push_back(m_items[i]);
    }
    return v;
}
int CMainFrame::FocusedIndex()
{
    return m_list.GetNextItem(-1, LVNI_FOCUSED);
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

LRESULT CMainFrame::OnSelectProcess(WORD, WORD, HWND, BOOL&)
{
    CSelectProcessDlg dlg;
    dlg.m_initialPid = m_pid;
    dlg.m_sortCol = m_s.selectProcessSort;
    if (dlg.DoModal(m_hWnd) == IDOK && dlg.m_pid)
    {
        m_pid = dlg.m_pid;
        m_s.selectProcessSort = dlg.m_sortCol;
        m_firstLoad = true;
        for (const auto& pr : dlg.m_procs)
        {
            if (pr.pid == m_pid)
            {
                m_procName = pr.name;
                m_s.lastProcess = pr.name;
                break;
            }
        }
        std::wstring title = L"HeapMemView - " + m_procName;
        wchar_t pidtxt[32];
        swprintf(pidtxt, 32, L" [%u]", m_pid);
        title += pidtxt;
        SetWindowTextW(title.c_str());
        Refresh();
    }
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
    m_s.showGridLines = !m_s.showGridLines;
    ApplyGridLines();
    UpdateMenuChecks();
    return 0;
}

LRESULT CMainFrame::OnAutoSize(WORD, WORD, HWND, BOOL&)
{
    for (int i = 0; i < (int)m_s.visible.size(); ++i)
    {
        m_list.SetColumnWidth(i, LVSCW_AUTOSIZE);
    }
    return 0;
}

LRESULT CMainFrame::OnPreviewMode(WORD, WORD id, HWND, BOOL&)
{
    m_s.dataPreviewMode = id - ID_PREVIEW_NONE; // 0/1/2
    for (auto& b : m_items)
    {
        b.previewDone = false;
        b.preview.clear();
    }
    UpdateMenuChecks();
    m_list.Invalidate();
    return 0;
}
LRESULT CMainFrame::OnToggleHeader(WORD, WORD, HWND, BOOL&)
{
    m_s.displayMemHeader = !m_s.displayMemHeader;
    UpdateMenuChecks();
    UpdateLowerPane();
    return 0;
}
LRESULT CMainFrame::OnToggleMarkNew(WORD, WORD, HWND, BOOL&)
{
    m_s.markNew = !m_s.markNew;
    UpdateMenuChecks();
    m_list.Invalidate();
    return 0;
}
LRESULT CMainFrame::OnToggleAbove127(WORD, WORD, HWND, BOOL&)
{
    m_s.displayAbove127 = !m_s.displayAbove127;
    for (auto& b : m_items)
    {
        b.previewDone = false;
        b.preview.clear();
    }
    UpdateMenuChecks();
    UpdateLowerPane();
    m_list.Invalidate();
    return 0;
}
LRESULT CMainFrame::OnToggleAutoSort(WORD, WORD, HWND, BOOL&)
{
    m_s.autoSortOnRefresh = !m_s.autoSortOnRefresh;
    UpdateMenuChecks();
    return 0;
}
LRESULT CMainFrame::OnClearNew(WORD, WORD, HWND, BOOL&)
{
    for (auto& b : m_items)
    {
        b.isNew = false;
    }
    m_list.Invalidate();
    return 0;
}

LRESULT CMainFrame::OnFilter(WORD, WORD, HWND, BOOL&)
{
    CFilterDlg dlg(m_s);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        Refresh();
    }
    return 0;
}
LRESULT CMainFrame::OnAdvanced(WORD, WORD, HWND, BOOL&)
{
    CAdvancedDlg dlg(m_s);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        for (auto& b : m_items)
        {
            b.previewDone = false;
            b.preview.clear();
        }
        m_list.Invalidate();
        UpdateLowerPane();
    }
    return 0;
}

LRESULT CMainFrame::OnCopy(WORD, WORD, HWND, BOOL&)
{
    auto sel = SelectedItems();
    if (sel.empty())
    {
        return 0;
    }
    CopyToClipboard(BuildReport(sel, m_s.visible, ExportFormat::Tab));
    return 0;
}

LRESULT CMainFrame::OnSave(WORD, WORD, HWND, BOOL&)
{
    auto sel = SelectedItems();
    if (sel.empty())
    {
        sel = m_items;
    }

    CFileDialog dlg(FALSE, L"txt", L"HeapMemView.txt", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"Text File (*.txt)\0*.txt\0Tab delimited text file (*.txt)\0*.txt\0"
                    L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.htm;*.html)\0*.htm;*.html\0"
                    L"HTML File - Vertical (*.htm;*.html)\0*.htm;*.html\0XML File (*.xml)\0*.xml\0"
                    L"Comma delimited text file (*.csv)\0*.csv\0",
                    m_hWnd);
    dlg.m_ofn.lpstrTitle = L"Select a filename to save";
    dlg.m_ofn.nFilterIndex = m_s.saveFilterIndex ? m_s.saveFilterIndex : 1;
    if (dlg.DoModal() != IDOK)
    {
        return 0;
    }
    m_s.saveFilterIndex = dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex : 1;
    ExportFormat fmt;
    if (!FormatFromFilterIndex(m_s.saveFilterIndex, fmt))
    {
        fmt = ExportFormat::Text;
    }
    WriteReport(dlg.m_szFileName, sel, m_s.visible, fmt);
    return 0;
}

// Export Memory Data To File: dump the raw bytes of selected blocks to .bin files.
LRESULT CMainFrame::OnExportData(WORD, WORD, HWND, BOOL&)
{
    auto idx = SelectedIndices();
    if (idx.empty())
    {
        return 0;
    }

    if (idx.size() == 1)
    {
        const HeapBlock& b = m_items[idx[0]];
        wchar_t name[64];
        swprintf(name, 64, L"%016llx.%s", (unsigned long long)b.dataAddress(), m_s.defaultExportExt.c_str());
        CFileDialog dlg(FALSE, m_s.defaultExportExt.c_str(), name, OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                        L"Memory Binary File (*.bin)\0*.bin\0All Files (*.*)\0*.*\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to export the selected memory block";
        if (dlg.DoModal() == IDOK)
        {
            DumpBlock(b, dlg.m_szFileName);
        }
    }
    else
    {
        BROWSEINFOW bi{};
        bi.hwndOwner = m_hWnd;
        bi.lpszTitle = L"Select a folder to export all selected memory blocks";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
        if (!pidl)
        {
            return 0;
        }
        wchar_t folder[MAX_PATH];
        if (::SHGetPathFromIDListW(pidl, folder))
        {
            for (int i : idx)
            {
                const HeapBlock& b = m_items[i];
                wchar_t path[MAX_PATH];
                swprintf(path, MAX_PATH, L"%s\\%016llx.%s", folder, (unsigned long long)b.dataAddress(),
                         m_s.defaultExportExt.c_str());
                DumpBlock(b, path);
            }
        }
        ::CoTaskMemFree(pidl);
    }
    return 0;
}

void CMainFrame::DumpBlock(const HeapBlock& b, const wchar_t* path)
{
    bool header = m_s.displayMemHeader;
    uint64_t base = header ? b.header : b.dataAddress();
    uint32_t len = header ? b.rawSize : b.blockSize();
    std::vector<BYTE> buf(len);
    SIZE_T got = ReadProcessBytes(m_pid, base, buf.data(), len);
    HANDLE h = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD w;
        ::WriteFile(h, buf.data(), (DWORD)got, &w, nullptr);
        ::CloseHandle(h);
    }
}

LRESULT CMainFrame::OnHtmlAll(WORD, WORD, HWND, BOOL&)
{
    ShowHtmlReport(m_items, true);
    return 0;
}
LRESULT CMainFrame::OnHtmlSel(WORD, WORD, HWND, BOOL&)
{
    auto sel = SelectedItems();
    if (sel.empty())
    {
        sel = m_items;
    }
    ShowHtmlReport(sel, true);
    return 0;
}
void CMainFrame::ShowHtmlReport(const std::vector<HeapBlock>& items, bool horizontal)
{
    wchar_t tmp[MAX_PATH], dir[MAX_PATH];
    ::GetTempPathW(MAX_PATH, dir);
    ::GetTempFileNameW(dir, L"hmv", 0, tmp);
    std::wstring html = tmp;
    html += L".html";
    WriteReport(html, items, m_s.visible, horizontal ? ExportFormat::HtmlHorizontal : ExportFormat::HtmlVertical);
    ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CMainFrame::OnChooseColumns(WORD, WORD, HWND, BOOL&)
{
    CColumnsDlg dlg(m_s.visible);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        m_s.visible = dlg.m_visible;
        if (std::find(m_s.visible.begin(), m_s.visible.end(), m_sortCol) == m_s.visible.end() && !m_s.visible.empty())
        {
            m_sortCol = m_s.visible.front();
        }
        RebuildColumns();
        m_list.Invalidate();
    }
    return 0;
}

LRESULT CMainFrame::OnProperties(WORD, WORD, HWND, BOOL&)
{
    int i = FocusedIndex();
    if (i < 0 || i >= (int)m_items.size())
    {
        return 0;
    }
    EnsurePreview(m_items[i]);
    CPropertiesDlg dlg(m_items[i], m_s.visible);
    dlg.DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&)
{
    CAboutDlg().DoModal(m_hWnd);
    return 0;
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
    int start = FocusedIndex();
    int n = (int)m_items.size();
    std::wstring low = needle;
    std::transform(low.begin(), low.end(), low.begin(), towlower);
    for (int step = 1; step <= n; ++step)
    {
        int i = down ? (start + step) % n : (start - step + n) % n;
        std::wstring row;
        for (int c : m_s.visible)
        {
            if (c == 6)
            {
                EnsurePreview(m_items[i]);
            }
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

// ---- config ---------------------------------------------------------
void CMainFrame::LoadConfig()
{
    if (m_noSaveLoad)
    {
        return;
    }
    LoadSettings(m_s, m_cfgPath);
    m_sortCol = m_s.sortColumn;
    if (m_sortCol < 0 || m_sortCol >= kColumnCount)
    {
        m_sortCol = 0;
    }
    if (m_s.havePlacement)
    {
        SetWindowPlacement(&m_s.placement);
    }
}

void CMainFrame::SaveConfig()
{
    if (m_noSaveLoad)
    {
        return;
    }
    m_s.sortColumn = m_sortCol;
    WINDOWPLACEMENT p{sizeof(p)};
    GetWindowPlacement(&p);
    m_s.placement = p;
    m_s.havePlacement = true;

    std::vector<int> widths;
    int hdr = m_list.GetHeader().GetItemCount();
    for (int i = 0; i < (int)m_s.visible.size(); ++i)
    {
        widths.push_back(i < hdr ? m_list.GetColumnWidth(i) : m_s.width[m_s.visible[i]]);
    }
    SaveSettings(m_s, m_cfgPath, widths);
}

void CMainFrame::OnDestroy()
{
    SaveConfig();
    PostQuitMessage(0);
}
