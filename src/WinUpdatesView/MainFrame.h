// MainFrame.h -- WTL main window for the WinUpdatesView recreation.
// Visual layout (menu 102, toolbar bitmap 110, status bar, owner-data report
// ListView) matches the original resources. Logic is clean C++20 over the
// EnumerateUpdateHistory() model rather than the decompiled offset code.
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
#include "UpdateRecord.h"
#include "UpdateEnumerator.h"
#include "Columns.h"
#include "Export.h"
#include "Config.h"
#include "Dialogs.h"

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"WinUpdatesView", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    std::vector<UpdateRecord> m_all;       // everything the data source returned
    std::vector<UpdateRecord> m_items;     // after filtering, in display order
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
        COMMAND_ID_HANDLER(ID_WUV_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER(ID_WUV_SAVE, OnSaveSelected)
        COMMAND_ID_HANDLER(ID_WUV_SAVEALL, OnSaveAll)
        COMMAND_ID_HANDLER(ID_WUV_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_WUV_COPYCELL, OnCopyCell)
        COMMAND_ID_HANDLER(ID_WUV_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_WUV_DESELECTALL, OnDeselectAll)
        COMMAND_ID_HANDLER(ID_WUV_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_WUV_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_WUV_MARKODDEVEN, OnMarkOddEven)
        COMMAND_ID_HANDLER(ID_WUV_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_WUV_AUTOSIZE_HDR, OnAutoSizeHdr)
        COMMAND_ID_HANDLER(ID_WUV_CHOOSECOLS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_WUV_HTML_ALL, OnHtmlAll)
        COMMAND_ID_HANDLER(ID_WUV_HTML_SEL, OnHtmlSel)
        COMMAND_ID_HANDLER(ID_WUV_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_WUV_ADVANCED, OnAdvanced)
        COMMAND_ID_HANDLER(ID_WUV_SHOWTIMEGMT, OnShowTimeGmt)
        COMMAND_ID_HANDLER(ID_WUV_HIDE_DEFENDER, OnHideDefender)
        COMMAND_ID_HANDLER(ID_WUV_HIDE_APICOMPAT, OnHideApiCompat)
        COMMAND_ID_HANDLER(ID_WUV_ADDHEADER, OnAddHeader)
        COMMAND_ID_HANDLER(ID_WUV_OPEN_INFO, OnOpenInfoUrl)
        COMMAND_ID_HANDLER(ID_WUV_OPEN_SUPPORT, OnOpenSupportUrl)
        COMMAND_ID_HANDLER(ID_WUV_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_WUV_EXIT, OnExit)
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
        int parts[] = { 400, -1 };
        ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 2, (LPARAM)parts);

        m_hWndClient = m_list.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
            WS_EX_CLIENTEDGE, IDC_LISTVIEW);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

        SetWindowTextW(L"WinUpdatesView");
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

        SetMsgHandled(FALSE);
        PostMessage(WM_COMMAND, MAKEWPARAM(ID_WUV_REFRESH, 0));
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
            { 0, ID_WUV_SAVE }, { 1, ID_WUV_COPY }, { 2, ID_WUV_REFRESH },
            { 3, ID_WUV_PROPERTIES }, { 4, ID_WUV_ADVANCED }, { 5, ID_WUV_CHOOSECOLS },
        };
        std::vector<TBBUTTON> b;
        for (auto& x : btns)
        {
            TBBUTTON t{}; t.iBitmap = x.bmp; t.idCommand = x.cmd;
            t.fsState = TBSTATE_ENABLED; t.fsStyle = TBSTYLE_BUTTON;
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
            m_list.InsertColumn(i++, kColumns[c].title, kColumns[c].format, w);
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
        menu.CheckMenuItem(ID_WUV_GRIDLINES, MF_BYCOMMAND | (m_cfg.showGridLines ? MF_CHECKED : MF_UNCHECKED));
        menu.CheckMenuItem(ID_WUV_MARKODDEVEN, MF_BYCOMMAND | (m_cfg.markOddEvenRows ? MF_CHECKED : MF_UNCHECKED));
        menu.CheckMenuItem(ID_WUV_SHOWTIMEGMT, MF_BYCOMMAND | (m_cfg.showTimeInGmt ? MF_CHECKED : MF_UNCHECKED));
        menu.CheckMenuItem(ID_WUV_HIDE_DEFENDER, MF_BYCOMMAND | (m_cfg.hideWindowsDefender ? MF_CHECKED : MF_UNCHECKED));
        menu.CheckMenuItem(ID_WUV_HIDE_APICOMPAT, MF_BYCOMMAND | (m_cfg.hideItemsCompatibility ? MF_CHECKED : MF_UNCHECKED));
        menu.CheckMenuItem(ID_WUV_ADDHEADER, MF_BYCOMMAND | (m_cfg.addExportHeaderLine ? MF_CHECKED : MF_UNCHECKED));
        menu.CheckMenuItem(ID_WUV_TOOLTIPS, MF_BYCOMMAND | (m_cfg.showInfoTip ? MF_CHECKED : MF_UNCHECKED));
    }

    DisplayOptions disp() const { DisplayOptions d; d.showTimeInGmt = m_cfg.showTimeInGmt; return d; }

    // ---- data -----------------------------------------------------------
    void Refresh()
    {
        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        if (m_cfg.loadFrom == 1)
        {
            HRESULT hr = S_OK;
            m_all = EnumerateUpdateHistory(&hr);
        }
        else
        {
            m_all.clear();
            MessageBoxW(L"Reading the Windows updates database directly (local database, external file or "
                        L"remote computer) is not implemented in this reconstruction. Use 'This computer, "
                        L"using API' in Advanced Options (F9).",
                        L"WinUpdatesView", MB_OK | MB_ICONINFORMATION);
        }
        ApplyFilterAndSort();
        ::SetCursor(prev);
    }

    bool passesFilter(const UpdateRecord& r) const
    {
        if (m_cfg.hideWindowsDefender &&
            (r.category == L"Microsoft Defender Antivirus" || r.category == L"Windows Defender"))
            return false;
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
        DisplayOptions d = disp();
        std::stable_sort(m_items.begin(), m_items.end(), [&](const UpdateRecord& a, const UpdateRecord& b) {
            bool na = false, nb = false;
            long long ka = a.sortKey(col, na), kb = b.sortKey(col, nb);
            int c;
            if (na && nb) c = (ka < kb) ? -1 : (ka > kb) ? 1 : 0;
            else          c = _wcsicmp(a.column(col, d).c_str(), b.column(col, d).c_str());
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
            text = m_items[item].column(col, disp());
            di->item.pszText = const_cast<LPWSTR>(text.c_str());
        }
        return 0;
    }

    LRESULT OnStateChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }
    LRESULT OnItemChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }

    // ---- status ---------------------------------------------------------
    void UpdateStatus()
    {
        wchar_t buf[128];
        int sel = m_list.GetSelectedCount();
        if (sel > 0) wsprintfW(buf, L"%d item(s), %d Selected", (int)m_items.size(), sel);
        else         wsprintfW(buf, L"%d item(s)", (int)m_items.size());
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, (LPARAM)osVersion().c_str());
    }

    std::wstring osVersion() const
    {
        OSVERSIONINFOEXW vi{ sizeof(vi) };
        #pragma warning(suppress : 4996)
        if (::GetVersionExW((LPOSVERSIONINFOW)&vi))
        {
            wchar_t b[64];
            wsprintfW(b, L"Windows %u.%u Build %u", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
            return b;
        }
        return L"";
    }

    // ---- selection helpers ---------------------------------------------
    std::vector<int> SelectedIndices()
    {
        std::vector<int> v;
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
            v.push_back(i);
        return v;
    }
    std::vector<UpdateRecord> SelectedItems()
    {
        std::vector<UpdateRecord> v;
        for (int i : SelectedIndices()) v.push_back(m_items[i]);
        return v;
    }

    ExportOptions expOpt() { ExportOptions o; o.addHeaderLine = m_cfg.addExportHeaderLine; o.display = disp(); return o; }

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&) { Refresh(); return 0; }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }

    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED); UpdateStatus(); return 0; }
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&) { m_list.SetItemState(-1, 0, LVIS_SELECTED); UpdateStatus(); return 0; }

    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&) { m_cfg.showGridLines = !m_cfg.showGridLines; ApplyGridLines(); SyncMenuChecks(); return 0; }
    LRESULT OnMarkOddEven(WORD, WORD, HWND, BOOL&) { m_cfg.markOddEvenRows = !m_cfg.markOddEvenRows; m_list.Invalidate(); SyncMenuChecks(); return 0; }
    LRESULT OnAddHeader(WORD, WORD, HWND, BOOL&) { m_cfg.addExportHeaderLine = !m_cfg.addExportHeaderLine; SyncMenuChecks(); return 0; }
    LRESULT OnShowTimeGmt(WORD, WORD, HWND, BOOL&) { m_cfg.showTimeInGmt = !m_cfg.showTimeInGmt; m_list.Invalidate(); SyncMenuChecks(); return 0; }
    LRESULT OnHideDefender(WORD, WORD, HWND, BOOL&) { m_cfg.hideWindowsDefender = !m_cfg.hideWindowsDefender; ApplyFilterAndSort(); SyncMenuChecks(); return 0; }
    LRESULT OnHideApiCompat(WORD, WORD, HWND, BOOL&) { m_cfg.hideItemsCompatibility = !m_cfg.hideItemsCompatibility; ApplyFilterAndSort(); SyncMenuChecks(); return 0; }

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
            CopyToClipboard(m_items[m_rclickRow].column(m_cfg.visibleColumns[m_rclickCol], disp()));
        return 0;
    }

    LRESULT OnSaveSelected(WORD, WORD, HWND, BOOL&) { return DoSave(SelectedItems()); }
    LRESULT OnSaveAll(WORD, WORD, HWND, BOOL&) { return DoSave(m_items); }

    LRESULT DoSave(std::vector<UpdateRecord> items)
    {
        if (items.empty()) items = m_items;
        CFileDialog dlg(FALSE, L"txt", L"WinUpdatesView.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
            L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
            L"Comma Delimited Text File (*.csv)\0*.csv\0HTML File - Horizontal (*.html)\0*.html\0"
            L"HTML File - Vertical (*.html)\0*.html\0XML File (*.xml)\0*.xml\0JSON File (*.json)\0*.json\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        if (dlg.DoModal() != IDOK) return 0;
        static const ExportFormat byIdx[] = { ExportFormat::Text, ExportFormat::Tab, ExportFormat::Csv,
            ExportFormat::HtmlHorizontal, ExportFormat::HtmlVertical, ExportFormat::Xml, ExportFormat::Json };
        DWORD fi = dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex - 1 : 0;
        WriteReport(dlg.m_szFileName, items, m_cfg.visibleColumns, byIdx[std::min<DWORD>(fi, 6)], expOpt());
        return 0;
    }

    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&) { ShowHtml(m_items, ExportFormat::HtmlHorizontal); return 0; }
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&) { auto s = SelectedItems(); ShowHtml(s.empty() ? m_items : s, ExportFormat::HtmlHorizontal); return 0; }

    void ShowHtml(const std::vector<UpdateRecord>& items, ExportFormat fmt)
    {
        wchar_t dir[MAX_PATH], tmp[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        ::GetTempFileNameW(dir, L"wuv", 0, tmp);
        std::wstring html = tmp; html += L".html";
        WriteReport(html, items, m_cfg.visibleColumns, fmt, expOpt());
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
        CPropertiesDlg dlg(m_items[i], m_cfg.visibleColumns, disp());
        dlg.DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnAdvanced(WORD, WORD, HWND, BOOL&)
    {
        int before = m_cfg.loadFrom;
        CAdvancedDlg dlg(m_cfg);
        if (dlg.DoModal(m_hWnd) == IDOK && m_cfg.loadFrom != before)
            Refresh();
        return 0;
    }

    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

    LRESULT OnOpenInfoUrl(WORD, WORD, HWND, BOOL&) { OpenUrl(15); return 0; }
    LRESULT OnOpenSupportUrl(WORD, WORD, HWND, BOOL&) { OpenUrl(5); return 0; }
    void OpenUrl(int col)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_items.size()) return;
        std::wstring url = m_items[i].column(col, disp());
        if (!url.empty()) ::ShellExecuteW(m_hWnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
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
                std::wstring v = m_items[i].column(c, disp());
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
