// MainFrame.h -- WTL main window for the TaskSchedulerView recreation.
// Layout (menu 102, toolbar bitmap 110, status bar, owner-data report ListView)
// matches the original resources. Logic is clean C++20 over the EnumerateTasks()
// model rather than the decompiled offset code.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <atlmisc.h>
#include <vector>
#include <string>
#include <algorithm>
#include "resource.h"
#include "../common/UiUtil.h"
#include "TaskRecord.h"
#include "TaskEnumerator.h"
#include "Columns.h"
#include "Export.h"
#include "Config.h"
#include "Dialogs.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"TaskSchedulerView", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    std::vector<TaskRecord> m_all;
    std::vector<TaskRecord> m_items;
    Config m_cfg;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    int m_rclickRow = -1, m_rclickCol = -1;
    UINT m_uFindMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg)) return TRUE;
        if (m_pFind && ::IsDialogMessageW(m_pFind->m_hWnd, pMsg)) return TRUE;
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        if (uMsg == m_uFindMsg) { OnFindReplaceMsg(lParam); lResult = 0; return TRUE; }
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ODSTATECHANGED, OnStateChanged)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
        COMMAND_ID_HANDLER(ID_TSV_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER(ID_TSV_SAVE, OnSaveSelected)
        COMMAND_ID_HANDLER(ID_TSV_SAVEALL, OnSaveAll)
        COMMAND_ID_HANDLER(ID_TSV_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_TSV_COPYCELL, OnCopyCell)
        COMMAND_ID_HANDLER(ID_TSV_COPY_XML, OnCopyXml)
        COMMAND_ID_HANDLER(ID_TSV_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_TSV_DESELECTALL, OnDeselectAll)
        COMMAND_ID_HANDLER(ID_TSV_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_TSV_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_TSV_MARKODDEVEN, OnMarkOddEven)
        COMMAND_ID_HANDLER(ID_TSV_BLACKBG, OnBlackBg)
        COMMAND_ID_HANDLER(ID_TSV_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_TSV_AUTOSIZE_HDR, OnAutoSizeHdr)
        COMMAND_ID_HANDLER(ID_TSV_CHOOSECOLS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_TSV_HTML_ALL, OnHtmlAll)
        COMMAND_ID_HANDLER(ID_TSV_HTML_SEL, OnHtmlSel)
        COMMAND_ID_HANDLER(ID_TSV_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_TSV_ADVANCED, OnAdvanced)
        COMMAND_ID_HANDLER(ID_TSV_SHOWTIMEGMT, OnShowTimeGmt)
        COMMAND_ID_HANDLER(ID_TSV_HIDE_DISABLED, OnHideDisabled)
        COMMAND_ID_HANDLER(ID_TSV_HIDE_MSOFT, OnHideMicrosoft)
        COMMAND_ID_HANDLER(ID_TSV_HIDE_HIDDEN, OnHideHidden)
        COMMAND_ID_HANDLER(ID_TSV_ONLYRUNNING, OnOnlyRunning)
        COMMAND_ID_HANDLER(ID_TSV_ALIGNNUMERIC, OnAlignNumeric)
        COMMAND_ID_HANDLER(ID_TSV_ADDHEADER, OnAddHeader)
        COMMAND_ID_HANDLER(ID_TSV_ENABLE, OnEnable)
        COMMAND_ID_HANDLER(ID_TSV_DISABLE, OnDisable)
        COMMAND_ID_HANDLER(ID_TSV_RUNNOW, OnRunNow)
        COMMAND_ID_HANDLER(ID_TSV_STOPTASK, OnStopTask)
        COMMAND_ID_HANDLER(ID_TSV_OPENFOLDER, OnOpenFolder)
        COMMAND_ID_HANDLER(ID_TSV_OPEN_TASKSCH, OnOpenTaskSch)
        COMMAND_ID_HANDLER(ID_TSV_SORTBY, OnSortBy)
        COMMAND_ID_HANDLER(ID_TSV_ENC_DEFAULT, OnEncoding)
        COMMAND_ID_HANDLER(ID_TSV_ENC_ANSI, OnEncoding)
        COMMAND_ID_HANDLER(ID_TSV_ENC_UTF16, OnEncoding)
        COMMAND_ID_HANDLER(ID_TSV_ENC_UTF8, OnEncoding)
        COMMAND_RANGE_HANDLER(50000, 50100, OnSortMenuPick)
        COMMAND_ID_HANDLER(ID_TSV_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_TSV_EXIT, OnExit)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()
    LRESULT OnEraseBkgnd(CDCHandle dc) { return FillFrameBtnFace(m_hWnd, dc); }

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT)
    {
        m_cfg.load();
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

        CreateToolbar();
        m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);
        int parts[] = { 300, -1 };
        ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, (LPARAM)parts);

        m_hWndClient = m_list.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
            WS_EX_CLIENTEDGE, IDC_LISTVIEW);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

        SetWindowTextW(L"TaskSchedulerView");
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        RebuildColumns();
        ApplyGridLines();
        ApplyColors();
        SyncMenuChecks();

        if (m_cfg.havePlacement)
            SetWindowPlacement(&m_cfg.placement);
        if (m_cfg.alwaysOnTop)
            SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
            pLoop->AddMessageFilter(this);

        SetMsgHandled(FALSE);
        PostMessage(WM_COMMAND, MAKEWPARAM(ID_TSV_REFRESH, 0));
        return 0;
    }

    void CreateToolbar()
    {
        CToolBarCtrl tb;
        tb.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER, 0, IDC_TOOLBAR);
        tb.SetButtonStructSize(sizeof(TBBUTTON));
        PinToolbarLight(tb);
        tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));
        struct { int bmp, cmd; } btns[] = {
            { 6, ID_TSV_ENABLE }, { 7, ID_TSV_DISABLE }, { -1, 0 },
            { 1, ID_TSV_SAVE }, { 5, ID_TSV_REFRESH }, { 2, ID_TSV_COPY },
            { 3, ID_TSV_PROPERTIES }, { 0, ID_TSV_FIND }, { 8, ID_TSV_SORTBY },
            { 4, ID_TSV_EXIT },
        };
        std::vector<TBBUTTON> b;
        for (auto& x : btns)
        {
            TBBUTTON t{};
            if (x.bmp < 0) { t.fsStyle = TBSTYLE_SEP; }
            else { t.iBitmap = x.bmp; t.idCommand = x.cmd; t.fsState = TBSTATE_ENABLED; t.fsStyle = TBSTYLE_BUTTON; }
            b.push_back(t);
        }
        tb.AddButtons((int)b.size(), b.data());
        m_hWndToolBar = tb;
    }

    // ---- columns --------------------------------------------------------
    void RebuildColumns()
    {
        while (m_list.DeleteColumn(0)) {}
        int i = 0;
        for (size_t k = 0; k < m_cfg.visibleColumns.size(); ++k)
        {
            int c = m_cfg.visibleColumns[k];
            int w = k < m_cfg.columnWidths.size() ? m_cfg.columnWidths[k] : kColumns[c].width;
            int fmt = (m_cfg.alignNumbersToRight && kColumns[c].kind == CK_NUMERIC) ? LVCFMT_RIGHT : LVCFMT_LEFT;
            m_list.InsertColumn(i++, kColumns[c].title, fmt, w);
        }
    }

    void ApplyGridLines()
    {
        DWORD ex = m_list.GetExtendedListViewStyle();
        ex = m_cfg.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
        m_list.SetExtendedListViewStyle(ex);
    }

    void ApplyColors()
    {
        COLORREF bk = m_cfg.blackBackground ? RGB(0, 0, 0) : ::GetSysColor(COLOR_WINDOW);
        COLORREF tx = m_cfg.blackBackground ? RGB(255, 255, 255) : ::GetSysColor(COLOR_WINDOWTEXT);
        m_list.SetBkColor(bk);
        m_list.SetTextBkColor(bk);
        m_list.SetTextColor(tx);
        m_list.Invalidate();
    }

    void SyncMenuChecks()
    {
        CMenuHandle menu = GetMenu();
        if (menu.IsNull()) return;
        auto chk = [&](UINT id, bool on) { menu.CheckMenuItem(id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
        chk(ID_TSV_GRIDLINES, m_cfg.showGridLines);
        chk(ID_TSV_TOOLTIPS, m_cfg.showInfoTip);
        chk(ID_TSV_MARKODDEVEN, m_cfg.markOddEvenRows);
        chk(ID_TSV_BLACKBG, m_cfg.blackBackground);
        chk(ID_TSV_SHOWTIMEGMT, m_cfg.showTimeInGmt);
        chk(ID_TSV_HIDE_DISABLED, m_cfg.hideDisabledTasks);
        chk(ID_TSV_HIDE_MSOFT, m_cfg.hideMicrosoftTasks);
        chk(ID_TSV_HIDE_HIDDEN, m_cfg.hideHiddenTasks);
        chk(ID_TSV_ONLYRUNNING, m_cfg.showOnlyRunningTasks);
        chk(ID_TSV_ALWAYSONTOP, m_cfg.alwaysOnTop);
        chk(ID_TSV_ALIGNNUMERIC, m_cfg.alignNumbersToRight);
        chk(ID_TSV_ADDHEADER, m_cfg.addExportHeaderLine);
        menu.CheckMenuRadioItem(ID_TSV_ENC_DEFAULT, ID_TSV_ENC_UTF8,
            ID_TSV_ENC_DEFAULT + m_cfg.saveFileEncoding, MF_BYCOMMAND);
    }

    DisplayOptions disp() const { DisplayOptions d; d.showTimeInGmt = m_cfg.showTimeInGmt; return d; }

    // ---- data -----------------------------------------------------------
    void Refresh()
    {
        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        HRESULT hr = S_OK;
        m_all = EnumerateTasks(disp(), m_cfg.loadFrom == 2 ? m_cfg.computerName : std::wstring(), &hr);
        ApplyFilterAndSort();
        ::SetCursor(prev);
    }

    bool passesFilter(const TaskRecord& r) const
    {
        if (m_cfg.hideDisabledTasks && r.cells[1] == L"Disabled") return false;
        if (m_cfg.hideHiddenTasks && r.cells[2] == L"Yes") return false;
        if (m_cfg.showOnlyRunningTasks && r.cells[1] != L"Running") return false;
        if (m_cfg.hideMicrosoftTasks && r.cells[7].rfind(L"\\Microsoft", 0) == 0) return false;
        return true;
    }

    void ApplyFilterAndSort()
    {
        m_items.clear();
        for (const auto& r : m_all)
            if (passesFilter(r)) m_items.push_back(r);
        SortItems();
        m_list.SetItemCount((int)m_items.size());
        m_list.Invalidate();
        UpdateStatus();
    }

    void SortItems()
    {
        const int col = m_cfg.sortColumn;
        const bool asc = m_cfg.sortAscending;
        std::stable_sort(m_items.begin(), m_items.end(), [&](const TaskRecord& a, const TaskRecord& b) {
            bool na = false, nb = false;
            long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
            int c;
            if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
            else          c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }

    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&)
    {
        int disp = reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem;
        if (disp < 0 || disp >= (int)m_cfg.visibleColumns.size()) return 0;
        int clicked = m_cfg.visibleColumns[disp];
        if (clicked == m_cfg.sortColumn) m_cfg.sortAscending = !m_cfg.sortAscending;
        else { m_cfg.sortColumn = clicked; m_cfg.sortAscending = true; }
        SortItems();
        m_list.Invalidate();
        return 0;
    }

    // ---- owner-data feed ------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&)
    {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
        int item = di->item.iItem;
        if (item < 0 || item >= (int)m_items.size()) return 0;
        if (di->item.mask & LVIF_TEXT)
        {
            int col = m_cfg.visibleColumns[di->item.iSubItem];
            static std::wstring text;
            text = m_items[item].column(col);
            di->item.pszText = const_cast<LPWSTR>(text.c_str());
        }
        return 0;
    }

    LRESULT OnStateChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }

    void UpdateStatus()
    {
        wchar_t buf[128];
        int sel = m_list.GetSelectedCount();
        if (sel > 0) wsprintfW(buf, L"%d item(s), %d Selected", (int)m_items.size(), sel);
        else         wsprintfW(buf, L"%d item(s)", (int)m_items.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
    }

    // ---- selection helpers ---------------------------------------------
    std::vector<int> SelectedIndices()
    {
        std::vector<int> v;
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
            v.push_back(i);
        return v;
    }
    std::vector<TaskRecord> SelectedItems()
    {
        std::vector<TaskRecord> v;
        for (int i : SelectedIndices()) v.push_back(m_items[i]);
        return v;
    }

    ExportOptions expOpt() { ExportOptions o; o.addHeaderLine = m_cfg.addExportHeaderLine; o.encoding = m_cfg.saveFileEncoding; return o; }

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&) { Refresh(); return 0; }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }

    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED); UpdateStatus(); return 0; }
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, 0, LVIS_SELECTED); UpdateStatus(); return 0; }

    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&) { m_cfg.showGridLines = !m_cfg.showGridLines; ApplyGridLines(); SyncMenuChecks(); return 0; }
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&) { m_cfg.markOddEvenRows = !m_cfg.markOddEvenRows; m_list.Invalidate(); SyncMenuChecks(); return 0; }
    LRESULT OnBlackBg(WORD, WORD, HWND, BOOL&) { m_cfg.blackBackground = !m_cfg.blackBackground; ApplyColors(); SyncMenuChecks(); return 0; }
    LRESULT OnAddHeader(WORD, WORD, HWND, BOOL&) { m_cfg.addExportHeaderLine = !m_cfg.addExportHeaderLine; SyncMenuChecks(); return 0; }
    LRESULT OnShowTimeGmt(WORD, WORD, HWND, BOOL&) { m_cfg.showTimeInGmt = !m_cfg.showTimeInGmt; SyncMenuChecks(); Refresh(); return 0; }
    LRESULT OnHideDisabled(WORD, WORD, HWND, BOOL&) { m_cfg.hideDisabledTasks = !m_cfg.hideDisabledTasks; ApplyFilterAndSort(); SyncMenuChecks(); return 0; }
    LRESULT OnHideMicrosoft(WORD, WORD, HWND, BOOL&) { m_cfg.hideMicrosoftTasks = !m_cfg.hideMicrosoftTasks; ApplyFilterAndSort(); SyncMenuChecks(); return 0; }
    LRESULT OnHideHidden(WORD, WORD, HWND, BOOL&) { m_cfg.hideHiddenTasks = !m_cfg.hideHiddenTasks; ApplyFilterAndSort(); SyncMenuChecks(); return 0; }
    LRESULT OnOnlyRunning(WORD, WORD, HWND, BOOL&) { m_cfg.showOnlyRunningTasks = !m_cfg.showOnlyRunningTasks; ApplyFilterAndSort(); SyncMenuChecks(); return 0; }
    LRESULT OnAlignNumeric(WORD, WORD, HWND, BOOL&) { m_cfg.alignNumbersToRight = !m_cfg.alignNumbersToRight; RebuildColumns(); m_list.Invalidate(); SyncMenuChecks(); return 0; }

    LRESULT OnEncoding(WORD, WORD id, HWND, BOOL&) { m_cfg.saveFileEncoding = id - ID_TSV_ENC_DEFAULT; SyncMenuChecks(); return 0; }

    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&)
    {
        for (int i = 0; i < (int)m_cfg.visibleColumns.size(); ++i) m_list.SetColumnWidth(i, LVSCW_AUTOSIZE);
        SyncWidths();
        return 0;
    }
    LRESULT OnAutoSizeHdr(WORD, WORD, HWND, BOOL&)
    {
        for (int i = 0; i < (int)m_cfg.visibleColumns.size(); ++i) m_list.SetColumnWidth(i, LVSCW_AUTOSIZE_USEHEADER);
        SyncWidths();
        return 0;
    }
    void SyncWidths()
    {
        for (size_t i = 0; i < m_cfg.visibleColumns.size(); ++i)
            if (i < m_cfg.columnWidths.size()) m_cfg.columnWidths[i] = m_list.GetColumnWidth((int)i);
    }

    LRESULT OnCopy(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        if (sel.empty()) return 0;
        CopyToClipboard(BuildReport(sel, m_cfg.visibleColumns, ExportFormat::Tab, expOpt()));
        return 0;
    }
    LRESULT OnCopyCell(WORD, WORD, HWND, BOOL&)
    {
        if (m_rclickRow >= 0 && m_rclickRow < (int)m_items.size() &&
            m_rclickCol >= 0 && m_rclickCol < (int)m_cfg.visibleColumns.size())
            CopyToClipboard(m_items[m_rclickRow].column(m_cfg.visibleColumns[m_rclickCol]));
        return 0;
    }
    LRESULT OnCopyXml(WORD, WORD, HWND, BOOL&)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_items.size()) return 0;
        std::wstring xml = TaskOperation(TaskOp::GetXml, m_items[i].taskPath,
                                         m_cfg.loadFrom == 2 ? m_cfg.computerName : std::wstring());
        if (!xml.empty()) CopyToClipboard(xml);
        return 0;
    }

    LRESULT OnSaveSelected(WORD, WORD, HWND, BOOL&) { return DoSave(SelectedItems()); }
    LRESULT OnSaveAll(WORD, WORD, HWND, BOOL&) { return DoSave(m_items); }

    LRESULT DoSave(std::vector<TaskRecord> items)
    {
        if (items.empty()) items = m_items;
        CFileDialog dlg(FALSE, L"txt", L"TaskSchedulerView.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
            L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
            L"Tabular Text File (*.txt)\0*.txt\0Comma Delimited Text File (*.csv)\0*.csv\0"
            L"HTML File - Horizontal (*.html)\0*.html\0HTML File - Vertical (*.html)\0*.html\0"
            L"XML File (*.xml)\0*.xml\0JSON File (*.json)\0*.json\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        dlg.m_ofn.nFilterIndex = m_cfg.saveFilterIndex;
        if (dlg.DoModal() != IDOK) return 0;
        static const ExportFormat byIdx[] = { ExportFormat::Text, ExportFormat::Tab, ExportFormat::Tabular,
            ExportFormat::Csv, ExportFormat::HtmlHorizontal, ExportFormat::HtmlVertical, ExportFormat::Xml, ExportFormat::Json };
        DWORD fi = dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex - 1 : 0;
        m_cfg.saveFilterIndex = dlg.m_ofn.nFilterIndex;
        WriteReport(dlg.m_szFileName, items, m_cfg.visibleColumns, byIdx[std::min<DWORD>(fi, 7)], expOpt());
        return 0;
    }

    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&) { ShowHtml(m_items); return 0; }
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&) { auto s = SelectedItems(); ShowHtml(s.empty() ? m_items : s); return 0; }

    void ShowHtml(const std::vector<TaskRecord>& items)
    {
        wchar_t dir[MAX_PATH], tmp[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        ::GetTempFileNameW(dir, L"tsv", 0, tmp);
        std::wstring html = tmp; html += L".html";
        ExportOptions o = expOpt(); o.encoding = 0;
        WriteReport(html, items, m_cfg.visibleColumns, ExportFormat::HtmlHorizontal, o);
        ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&)
    {
        CColumnsDlg dlg(m_cfg.visibleColumns, m_cfg.columnWidths);
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            m_cfg.visibleColumns = dlg.m_visible;
            m_cfg.columnWidths = dlg.m_widths;
            if (std::find(m_cfg.visibleColumns.begin(), m_cfg.visibleColumns.end(), m_cfg.sortColumn) == m_cfg.visibleColumns.end()
                && !m_cfg.visibleColumns.empty())
                m_cfg.sortColumn = m_cfg.visibleColumns.front();
            RebuildColumns();
            m_list.Invalidate();
        }
        return 0;
    }

    LRESULT OnProperties(WORD, WORD, HWND, BOOL&)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_items.size()) return 0;
        CPropertiesDlg dlg(m_items[i], m_cfg.visibleColumns);
        dlg.DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnAdvanced(WORD, WORD, HWND, BOOL&)
    {
        Config before = m_cfg;
        CAdvancedDlg dlg(m_cfg);
        if (dlg.DoModal(m_hWnd) == IDOK &&
            (m_cfg.loadFrom != before.loadFrom || m_cfg.computerName != before.computerName))
            Refresh();
        return 0;
    }

    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

    // Task operations (enable/disable/run/stop) over the selection.
    void RunOp(TaskOp op)
    {
        auto sel = SelectedItems();
        if (sel.empty()) return;
        std::wstring comp = m_cfg.loadFrom == 2 ? m_cfg.computerName : std::wstring();
        bool anyFail = false;
        for (const auto& r : sel) { bool ok = false; TaskOperation(op, r.taskPath, comp, &ok); if (!ok) anyFail = true; }
        if (anyFail)
            MessageBoxW(L"One or more tasks could not be changed. Some tasks require running "
                        L"TaskSchedulerView as administrator (Ctrl+F11).", L"TaskSchedulerView",
                        MB_OK | MB_ICONINFORMATION);
        Refresh();
    }
    LRESULT OnEnable(WORD, WORD, HWND, BOOL&) { RunOp(TaskOp::Enable); return 0; }
    LRESULT OnDisable(WORD, WORD, HWND, BOOL&) { RunOp(TaskOp::Disable); return 0; }
    LRESULT OnRunNow(WORD, WORD, HWND, BOOL&) { RunOp(TaskOp::Run); return 0; }
    LRESULT OnStopTask(WORD, WORD, HWND, BOOL&) { RunOp(TaskOp::Stop); return 0; }

    LRESULT OnOpenFolder(WORD, WORD, HWND, BOOL&)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_items.size()) return 0;
        std::wstring arg = L"/select,\"" + m_items[i].cells[36] + L"\"";
        ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        return 0;
    }
    LRESULT OnOpenTaskSch(WORD, WORD, HWND, BOOL&)
    {
        ::ShellExecuteW(m_hWnd, L"open", L"taskschd.msc", nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    // Sort By toolbar/menu: pop a menu of the visible columns.
    LRESULT OnSortBy(WORD, WORD, HWND, BOOL&)
    {
        HMENU menu = ::CreatePopupMenu();
        for (size_t k = 0; k < m_cfg.visibleColumns.size(); ++k)
            ::AppendMenuW(menu, MF_STRING, 50000 + (UINT)k, kColumns[m_cfg.visibleColumns[k]].title);
        POINT pt; ::GetCursorPos(&pt);
        ::TrackPopupMenu(menu, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
        ::DestroyMenu(menu);
        return 0;
    }
    LRESULT OnSortMenuPick(WORD, WORD id, HWND, BOOL&)
    {
        size_t k = id - 50000;
        if (k < m_cfg.visibleColumns.size())
        {
            int col = m_cfg.visibleColumns[k];
            if (col == m_cfg.sortColumn) m_cfg.sortAscending = !m_cfg.sortAscending;
            else { m_cfg.sortColumn = col; m_cfg.sortAscending = true; }
            SortItems();
            m_list.Invalidate();
        }
        return 0;
    }

    // ---- find -----------------------------------------------------------
    LRESULT OnFind(WORD, WORD, HWND, BOOL&)
    {
        if (m_pFind) { m_pFind->SetFocus(); return 0; }
        m_pFind = new CFindReplaceDialog();
        m_pFind->Create(TRUE, L"", nullptr, FR_DOWN, m_hWnd);
        return 0;
    }
    void OnFindReplaceMsg(LPARAM lParam)
    {
        auto* fr = reinterpret_cast<LPFINDREPLACEW>(lParam);
        if (fr->Flags & FR_DIALOGTERM) { m_pFind = nullptr; return; }
        if (fr->Flags & FR_FINDNEXT) FindText(fr->lpstrFindWhat, (fr->Flags & FR_DOWN) != 0);
    }
    void FindText(const std::wstring& what, bool down)
    {
        if (what.empty() || m_items.empty()) return;
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
    LRESULT OnDblClk(int, LPNMHDR, BOOL&) { BOOL b; OnProperties(0, 0, nullptr, b); return 0; }

    LRESULT OnRClick(int, LPNMHDR p, BOOL&)
    {
        auto* ia = reinterpret_cast<LPNMITEMACTIVATE>(p);
        m_rclickRow = ia->iItem;
        m_rclickCol = ia->iSubItem;
        HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CONTEXT_MENU));
        if (!menu) return 0;
        HMENU sub = ::GetSubMenu(menu, 0);
        POINT pt; ::GetCursorPos(&pt);
        ::TrackPopupMenu(sub, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
        ::DestroyMenu(menu);
        return 0;
    }

    void CopyToClipboard(const std::wstring& text)
    {
        if (!::OpenClipboard(m_hWnd)) return;
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

    void OnDestroy()
    {
        SyncWidths();
        m_cfg.placement.length = sizeof(m_cfg.placement);
        if (GetWindowPlacement(&m_cfg.placement)) m_cfg.havePlacement = true;
        m_cfg.save();
        PostQuitMessage(0);
    }
};
