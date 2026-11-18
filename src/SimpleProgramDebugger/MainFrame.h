// MainFrame.h -- WTL main window for the SimpleProgramDebugger recreation.
// Layout (menu 102, toolbar bitmap 110, status bar, owner-data report ListView)
// matches the original resources. The debug engine runs on its own thread and
// feeds events to this window through WM_SPD_EVENT, as the original does.
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
#include "DebugEvent.h"
#include "Debugger.h"
#include "Columns.h"
#include "Export.h"
#include "Config.h"
#include "Dialogs.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"SimpleProgramDebugger", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    std::vector<DbgRow> m_rows;
    Config m_cfg;
    Debugger m_debugger;
    LaunchSpec m_pendingLaunch;
    bool m_hasPending = false;
    std::wstring m_targetName;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    int m_rclickRow = -1;
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
        MESSAGE_HANDLER(WM_SPD_EVENT, OnDbgEvent)
        MESSAGE_HANDLER(WM_SPD_STATUS, OnDbgStatus)
        MESSAGE_HANDLER(WM_APP + 100, OnStartPending)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ODSTATECHANGED, OnStateChanged)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
        COMMAND_ID_HANDLER(ID_SPD_ATTACH, OnAttach)
        COMMAND_ID_HANDLER(ID_SPD_STARTNEW, OnStartNew)
        COMMAND_ID_HANDLER(ID_SPD_DETACH, OnDetach)
        COMMAND_ID_HANDLER(ID_SPD_CLEARALL, OnClearAll)
        COMMAND_ID_HANDLER(ID_SPD_SAVE, OnSave)
        COMMAND_ID_HANDLER(ID_SPD_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_SPD_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_SPD_DESELECTALL, OnDeselectAll)
        COMMAND_ID_HANDLER(ID_SPD_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_SPD_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_SPD_HTML_ALL, OnHtmlAll)
        COMMAND_ID_HANDLER(ID_SPD_HTML_SEL, OnHtmlSel)
        COMMAND_ID_HANDLER(ID_SPD_CHOOSECOLS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_SPD_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_SPD_AUTOSIZE_HDR, OnAutoSizeHdr)
        COMMAND_ID_HANDLER(ID_SPD_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_SPD_MARKODDEVEN, OnMarkOddEven)
        COMMAND_ID_HANDLER(ID_SPD_TOOLTIPS, OnTooltips)
        COMMAND_ID_HANDLER(ID_SPD_SCROLLDOWN, OnScrollDown)
        COMMAND_ID_HANDLER(ID_SPD_SORTUPDATE, OnSortUpdate)
        COMMAND_ID_HANDLER(ID_SPD_ADDHEADER, OnAddHeader)
        COMMAND_ID_HANDLER(ID_SPD_TRAYICON, OnTrayIcon)
        COMMAND_ID_HANDLER(ID_SPD_STARTHIDDEN, OnStartHidden)
        COMMAND_ID_HANDLER(ID_SPD_RUNASADMIN, OnRunAsAdmin)
        COMMAND_ID_HANDLER(ID_SPD_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER(ID_SPD_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_SPD_EXIT, OnExit)
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

        SetWindowTextW(L"SimpleProgramDebugger");
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        RebuildColumns();
        ApplyGridLines();
        SyncMenuChecks();

        if (m_cfg.havePlacement)
            SetWindowPlacement(&m_cfg.placement);

        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
            pLoop->AddMessageFilter(this);

        UpdateStatus();

        if (m_hasPending)
            PostMessageW(WM_APP + 100, 0, 0);  // start the pending launch once the window is up
        return 0;
    }

    LRESULT OnStartPending(UINT, WPARAM, LPARAM, BOOL&)
    {
        if (m_hasPending) { m_hasPending = false; beginSession(m_pendingLaunch); }
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
            { -1, 0 },
            { 6, ID_SPD_ATTACH }, { 8, ID_SPD_STARTNEW }, { 7, ID_SPD_DETACH }, { -1, 0 },
            { 1, ID_SPD_SAVE }, { 5, ID_SPD_REFRESH }, { 2, ID_SPD_COPY },
            { 3, ID_SPD_PROPERTIES }, { 0, ID_SPD_FIND }, { 4, ID_SPD_EXIT },
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
            m_list.InsertColumn(i++, kColumns[c].title, LVCFMT_LEFT, w);
        }
    }

    void ApplyGridLines()
    {
        DWORD ex = m_list.GetExtendedListViewStyle();
        ex = m_cfg.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
        m_list.SetExtendedListViewStyle(ex);
    }

    void SyncMenuChecks()
    {
        CMenuHandle menu = GetMenu();
        if (menu.IsNull()) return;
        auto chk = [&](UINT id, bool on) { menu.CheckMenuItem(id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
        chk(ID_SPD_GRIDLINES, m_cfg.showGridLines);
        chk(ID_SPD_TOOLTIPS, m_cfg.showInfoTip);
        chk(ID_SPD_MARKODDEVEN, m_cfg.markOddEvenRows);
        chk(ID_SPD_SCROLLDOWN, m_cfg.scrollDownOnNewLine);
        chk(ID_SPD_SORTUPDATE, m_cfg.sortOnEveryUpdate);
        chk(ID_SPD_ADDHEADER, m_cfg.addExportHeaderLine);
        chk(ID_SPD_TRAYICON, m_cfg.trayIcon);
        chk(ID_SPD_STARTHIDDEN, m_cfg.startAsHidden);
    }

    // ---- debug session --------------------------------------------------
    static std::wstring baseName(const std::wstring& path)
    {
        size_t p = path.find_last_of(L"\\/");
        return p == std::wstring::npos ? path : path.substr(p + 1);
    }

    void beginSession(const LaunchSpec& spec)
    {
        if (m_debugger.active())
            m_debugger.stop();
        if (spec.attach)
        {
            m_targetName = processName(spec.pid);
        }
        else
        {
            m_targetName = baseName(spec.path);
        }
        m_debugger.start(spec, m_hWnd);
    }

    static std::wstring processName(DWORD pid)
    {
        std::wstring name;
        if (HANDLE hp = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            wchar_t path[1024]; DWORD n = 1024;
            if (::QueryFullProcessImageNameW(hp, 0, path, &n)) name = baseName(std::wstring(path, n));
            ::CloseHandle(hp);
        }
        return name;
    }

    LRESULT OnDbgEvent(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        DbgRow* row = reinterpret_cast<DbgRow*>(lParam);
        if (!row) return 0;
        m_rows.push_back(std::move(*row));
        delete row;
        if (m_cfg.sorted && m_cfg.sortOnEveryUpdate)
            SortItems();
        m_list.SetItemCountEx((int)m_rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
        if (m_cfg.scrollDownOnNewLine && !m_rows.empty())
            m_list.EnsureVisible((int)m_rows.size() - 1, FALSE);
        UpdateStatus();
        return 0;
    }

    LRESULT OnDbgStatus(UINT, WPARAM wParam, LPARAM lParam, BOOL&)
    {
        switch (static_cast<SpdStatus>(wParam))
        {
        case SpdStatus::SPD_STARTED:
        {
            wchar_t buf[600];
            swprintf(buf, 600, L"SimpleProgramDebugger  -  [%u]  %s", static_cast<unsigned>(lParam), m_targetName.c_str());
            SetWindowTextW(buf);
            break;
        }
        case SpdStatus::SPD_ENDED:
            SetWindowTextW(L"SimpleProgramDebugger");
            break;
        case SpdStatus::SPD_ERROR:
        {
            wchar_t* msg = nullptr;
            ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, (DWORD)lParam, 0, (LPWSTR)&msg, 0, nullptr);
            wchar_t buf[600];
            swprintf(buf, 600, L"Error %u: %s", static_cast<unsigned>(lParam), msg ? msg : L"");
            if (msg) ::LocalFree(msg);
            SetWindowTextW(L"SimpleProgramDebugger");
            MessageBoxW(buf, L"Error", MB_OK | MB_ICONERROR);
            break;
        }
        }
        return 0;
    }

    // ---- sorting --------------------------------------------------------
    void SortItems()
    {
        if (!m_cfg.sorted) return;
        const int col = m_cfg.sortColumn;
        const bool asc = m_cfg.sortAscending;
        std::stable_sort(m_rows.begin(), m_rows.end(), [&](const DbgRow& a, const DbgRow& b) {
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
        if (m_cfg.sorted && clicked == m_cfg.sortColumn) m_cfg.sortAscending = !m_cfg.sortAscending;
        else { m_cfg.sorted = true; m_cfg.sortColumn = clicked; m_cfg.sortAscending = true; }
        SortItems();
        m_list.Invalidate();
        return 0;
    }

    // ---- owner-data feed ------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&)
    {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
        int item = di->item.iItem;
        if (item < 0 || item >= (int)m_rows.size()) return 0;
        if (di->item.mask & LVIF_TEXT)
        {
            int col = m_cfg.visibleColumns[di->item.iSubItem];
            static std::wstring text;
            text = m_rows[item].column(col);
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
        if (sel > 0) wsprintfW(buf, L"%d item(s), %d Selected", (int)m_rows.size(), sel);
        else         wsprintfW(buf, L"%d item(s)", (int)m_rows.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
    }

    // ---- selection helpers ---------------------------------------------
    std::vector<DbgRow> SelectedItems()
    {
        std::vector<DbgRow> v;
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
            if (i < (int)m_rows.size()) v.push_back(m_rows[i]);
        return v;
    }

    ExportOptions expOpt() { ExportOptions o; o.addHeaderLine = m_cfg.addExportHeaderLine; return o; }

    // ---- debug control commands ----------------------------------------
    LRESULT OnAttach(WORD, WORD, HWND, BOOL&)
    {
        CSelectProcessDlg dlg;
        if (dlg.DoModal(m_hWnd) == IDOK && dlg.m_pid)
        {
            LaunchSpec s; s.attach = true; s.pid = dlg.m_pid;
            beginSession(s);
        }
        return 0;
    }

    LRESULT OnStartNew(WORD, WORD, HWND, BOOL&)
    {
        CStartProcessDlg dlg(m_cfg.newProcessPath, m_cfg.newProcessParams, m_cfg.newProcessStartFolder);
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            m_cfg.newProcessPath = dlg.m_path;
            m_cfg.newProcessParams = dlg.m_params;
            m_cfg.newProcessStartFolder = dlg.m_folder;
            LaunchSpec s; s.attach = false; s.path = dlg.m_path; s.params = dlg.m_params; s.startFolder = dlg.m_folder;
            beginSession(s);
        }
        return 0;
    }

    LRESULT OnDetach(WORD, WORD, HWND, BOOL&) { m_debugger.stop(); SetWindowTextW(L"SimpleProgramDebugger"); return 0; }

    LRESULT OnClearAll(WORD, WORD, HWND, BOOL&)
    {
        m_rows.clear();
        m_list.SetItemCount(0);
        m_list.Invalidate();
        UpdateStatus();
        return 0;
    }

    // ---- list / file commands ------------------------------------------
    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED); UpdateStatus(); return 0; }
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, 0, LVIS_SELECTED); UpdateStatus(); return 0; }

    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&) { m_cfg.showGridLines = !m_cfg.showGridLines; ApplyGridLines(); SyncMenuChecks(); return 0; }
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&) { m_cfg.markOddEvenRows = !m_cfg.markOddEvenRows; m_list.Invalidate(); SyncMenuChecks(); return 0; }
    LRESULT OnTooltips(WORD, WORD, HWND, BOOL&) { m_cfg.showInfoTip = !m_cfg.showInfoTip; SyncMenuChecks(); return 0; }
    LRESULT OnScrollDown(WORD, WORD, HWND, BOOL&) { m_cfg.scrollDownOnNewLine = !m_cfg.scrollDownOnNewLine; SyncMenuChecks(); return 0; }
    LRESULT OnSortUpdate(WORD, WORD, HWND, BOOL&) { m_cfg.sortOnEveryUpdate = !m_cfg.sortOnEveryUpdate; SyncMenuChecks(); return 0; }
    LRESULT OnAddHeader(WORD, WORD, HWND, BOOL&) { m_cfg.addExportHeaderLine = !m_cfg.addExportHeaderLine; SyncMenuChecks(); return 0; }
    LRESULT OnTrayIcon(WORD, WORD, HWND, BOOL&) { m_cfg.trayIcon = !m_cfg.trayIcon; SyncMenuChecks(); return 0; }
    LRESULT OnStartHidden(WORD, WORD, HWND, BOOL&) { m_cfg.startAsHidden = !m_cfg.startAsHidden; SyncMenuChecks(); return 0; }
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&) { m_list.Invalidate(); return 0; }

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

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        const std::vector<DbgRow>& items = sel.empty() ? m_rows : sel;
        CFileDialog dlg(FALSE, L"txt", L"SimpleProgramDebugger.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
            L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
            L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.htm;*.html)\0*.htm;*.html\0"
            L"HTML File - Vertical (*.htm;*.html)\0*.htm;*.html\0XML File (*.xml)\0*.xml\0"
            L"Comma Delimited Text File (*.csv)\0*.csv\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        dlg.m_ofn.nFilterIndex = m_cfg.saveFilterIndex;
        if (dlg.DoModal(m_hWnd) != IDOK) return 0;
        m_cfg.saveFilterIndex = dlg.m_ofn.nFilterIndex;
        ExportFormat fmt = FormatFromFilterIndex(dlg.m_ofn.nFilterIndex);
        WriteReport(dlg.m_szFileName, items, m_cfg.visibleColumns, fmt, expOpt());
        return 0;
    }

    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&) { ShowHtml(m_rows); return 0; }
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&) { auto s = SelectedItems(); ShowHtml(s.empty() ? m_rows : s); return 0; }

    void ShowHtml(const std::vector<DbgRow>& items)
    {
        wchar_t dir[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        std::wstring html = std::wstring(dir) + L"report.html";
        WriteReport(html, items, m_cfg.visibleColumns, ExportFormat::HtmlHorizontal, expOpt());
        ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&)
    {
        CColumnsDlg dlg(m_cfg.visibleColumns, m_cfg.columnWidths);
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            m_cfg.visibleColumns = dlg.m_visible;
            m_cfg.columnWidths = dlg.m_widths;
            if (m_cfg.sorted && std::find(m_cfg.visibleColumns.begin(), m_cfg.visibleColumns.end(), m_cfg.sortColumn) == m_cfg.visibleColumns.end())
                m_cfg.sorted = false;
            RebuildColumns();
            m_list.Invalidate();
        }
        return 0;
    }

    LRESULT OnProperties(WORD, WORD, HWND, BOOL&)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_rows.size()) return 0;
        CPropertiesDlg dlg(m_rows[i], m_cfg.visibleColumns);
        dlg.DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnRunAsAdmin(WORD, WORD, HWND, BOOL&)
    {
        wchar_t self[MAX_PATH];
        ::GetModuleFileNameW(nullptr, self, MAX_PATH);
        ::ShellExecuteW(m_hWnd, L"runas", self, nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }

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
        if (what.empty() || m_rows.empty()) return;
        std::wstring needle = what;
        std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
        int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
        int n = (int)m_rows.size();
        for (int step = 1; step <= n; ++step)
        {
            int i = down ? (start + step) % n : ((start - step) % n + n) % n;
            for (int c : m_cfg.visibleColumns)
            {
                std::wstring v = m_rows[i].column(c);
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
        m_debugger.stop();
        SyncWidths();
        m_cfg.placement.length = sizeof(m_cfg.placement);
        if (GetWindowPlacement(&m_cfg.placement)) m_cfg.havePlacement = true;
        m_cfg.save();
        PostQuitMessage(0);
    }
};
