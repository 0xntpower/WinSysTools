// MainFrame.h -- WTL main window for the MIMEView recreation.
// Visual layout (menu 102, toolbar bitmap 104, status bar, report ListView with
// per-type icons) matches the original resources. The logic is clean C++20 over
// the EnumerateMimeTypes() model rather than the decompiled offset code.
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
#include "MimeType.h"
#include "Columns.h"
#include "Export.h"
#include "RegKey.h"
#include "Dialogs.h"

constexpr wchar_t kConfigKey[] = L"Software\\NirSoft\\MIMEView";

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"MIMEView", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    CImageList    m_images;
    std::vector<MimeType> m_items;
    std::vector<int>      m_itemImage;   // small-image index per item
    std::vector<int>      m_visible;     // visible column indices, in display order
    int  m_sortCol = 0;
    bool m_sortAsc = true;
    bool m_gridLines = true;
    int  m_startShowCmd = SW_SHOWNORMAL;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;

    CMainFrame()
    {
        for (int i = 0; i < kColumnCount; ++i) m_visible.push_back(i);
    }

    BOOL PreTranslateMessage(MSG* pMsg) override
    {
        if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg)) return TRUE;
        if (m_pFind && ::IsDialogMessageW(m_pFind->m_hWnd, pMsg)) return TRUE;
        return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
    }

    BEGIN_MSG_MAP(CMainFrame)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
        COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
        COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_EDIT_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_VIEW_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_VIEW_COLUMNS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_VIEW_HTML_HORZ, OnHtmlHorz)
        COMMAND_ID_HANDLER(ID_VIEW_HTML_VERT, OnHtmlVert)
        COMMAND_ID_HANDLER(ID_FILE_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_FILE_ENABLE_DLG, OnEnableDlg)
        COMMAND_ID_HANDLER(ID_FILE_DISABLE_DLG, OnDisableDlg)
        COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    // ---- creation -------------------------------------------------------
    int OnCreate(LPCREATESTRUCT)
    {
        m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

        CreateToolbar();
        m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                                L"", m_hWnd, IDC_STATUSBAR);

        m_hWndClient = m_list.Create(m_hWnd, rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA,
            WS_EX_CLIENTEDGE, IDC_LISTVIEW);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        m_images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 4, 8);
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_DEFAULT_TYPE)))
            m_images.AddIcon(hi);
        m_list.SetImageList(m_images, LVSIL_SMALL);

        SetWindowTextW(L"MIMEView");
        // Title-bar / taskbar icon 101 (the frame class otherwise uses the menu id 102).
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
        {
            SetIcon(hi, TRUE);
            SetIcon(hi, FALSE);
        }

        LoadConfig();
        RebuildColumns();
        ApplyGridLines();

        if (CMessageLoop* pLoop = _Module.GetMessageLoop())
            pLoop->AddMessageFilter(this);

        Refresh();
        return 0;
    }

    void CreateToolbar()
    {
        CToolBarCtrl tb;
        tb.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER,
                  0, IDC_TOOLBAR);
        tb.SetButtonStructSize(sizeof(TBBUTTON));
        tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));
        TBBUTTON b[5] = {};
        auto set = [&](int i, int bmp, int cmd, BYTE style) {
            b[i].iBitmap = bmp; b[i].idCommand = cmd; b[i].fsState = TBSTATE_ENABLED; b[i].fsStyle = style;
        };
        set(0, 0, ID_FILE_SAVE, TBSTYLE_BUTTON);
        set(1, 1, ID_VIEW_REFRESH, TBSTYLE_BUTTON);
        set(2, 2, ID_EDIT_COPY, TBSTYLE_BUTTON);
        set(3, 0, 0, TBSTYLE_SEP);
        set(4, 3, ID_FILE_PROPERTIES, TBSTYLE_BUTTON);
        tb.AddButtons(5, b);
        m_hWndToolBar = tb;
    }

    // ---- columns --------------------------------------------------------
    void RebuildColumns()
    {
        while (m_list.DeleteColumn(0)) {}
        int i = 0;
        for (int c : m_visible)
            m_list.InsertColumn(i++, kColumns[c].title, kColumns[c].format, kColumns[c].width);
    }

    void ApplyGridLines()
    {
        DWORD ex = m_list.GetExtendedListViewStyle();
        ex = m_gridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
        m_list.SetExtendedListViewStyle(ex);
    }

    // ---- data -----------------------------------------------------------
    void Refresh()
    {
        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        std::wstring keepSel = SelectedType();

        m_items = EnumerateMimeTypes();
        BuildImages();
        SortItems();

        m_list.SetItemCount(static_cast<int>(m_items.size()));
        m_list.Invalidate();
        if (!keepSel.empty()) SelectType(keepSel);
        UpdateStatus();
        ::SetCursor(prev);
    }

    void BuildImages()
    {
        m_images.RemoveAll();
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_DEFAULT_TYPE)))
            m_images.AddIcon(hi);
        m_itemImage.assign(m_items.size(), 0);
        for (size_t i = 0; i < m_items.size(); ++i)
            m_itemImage[i] = ExtractTypeIcon(m_items[i].defaultIcon);
    }

    int ExtractTypeIcon(const std::wstring& defIcon)
    {
        if (defIcon.empty()) return 0;
        std::wstring path = defIcon;
        int index = 0;
        if (auto comma = defIcon.find_last_of(L','); comma != std::wstring::npos)
        {
            path = defIcon.substr(0, comma);
            index = _wtoi(defIcon.c_str() + comma + 1);
        }
        if (!path.empty() && path.front() == L'"') path.erase(0, 1);
        if (!path.empty() && path.back() == L'"') path.pop_back();

        wchar_t expanded[MAX_PATH];
        ::ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH);

        HICON hSmall = nullptr;
        if (::ExtractIconExW(expanded, index, nullptr, &hSmall, 1) >= 1 && hSmall)
        {
            int idx = m_images.AddIcon(hSmall);
            ::DestroyIcon(hSmall);
            return idx > 0 ? idx : 0;
        }
        return 0;
    }

    // ---- sorting --------------------------------------------------------
    void SortItems()
    {
        const int col = m_sortCol;
        const bool asc = m_sortAsc;
        std::stable_sort(m_items.begin(), m_items.end(), [col, asc](const MimeType& a, const MimeType& b) {
            int c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }

    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&)
    {
        int clicked = m_visible[reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem];
        if (clicked == m_sortCol) m_sortAsc = !m_sortAsc;
        else { m_sortCol = clicked; m_sortAsc = true; }
        std::wstring keep = SelectedType();
        SortItems();
        m_list.Invalidate();
        if (!keep.empty()) SelectType(keep);
        return 0;
    }

    // ---- owner-data feed ------------------------------------------------
    LRESULT OnGetDispInfo(int, LPNMHDR p, BOOL&)
    {
        auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
        int item = di->item.iItem;
        if (item < 0 || item >= static_cast<int>(m_items.size())) return 0;
        int col = m_visible[di->item.iSubItem];
        if (di->item.mask & LVIF_TEXT)
        {
            static std::wstring text;
            text = m_items[item].column(col);
            di->item.pszText = const_cast<LPWSTR>(text.c_str());
        }
        if ((di->item.mask & LVIF_IMAGE) && di->item.iSubItem == 0)
            di->item.iImage = m_itemImage[item];
        return 0;
    }

    // ---- status ---------------------------------------------------------
    void UpdateStatus()
    {
        wchar_t buf[128];
        int sel = m_list.GetSelectedCount();
        if (sel > 0) wsprintfW(buf, L"%d item(s), %d Selected", (int)m_items.size(), sel);
        else         wsprintfW(buf, L"%d item(s)", (int)m_items.size());
        ::SetWindowTextW(m_hWndStatusBar, buf);
    }

    // ---- selection helpers ---------------------------------------------
    std::wstring SelectedType()
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        return (i >= 0 && i < (int)m_items.size()) ? m_items[i].contentType : std::wstring();
    }
    void SelectType(const std::wstring& ct)
    {
        for (size_t i = 0; i < m_items.size(); ++i)
            if (m_items[i].contentType == ct)
            {
                m_list.SetItemState((int)i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                m_list.EnsureVisible((int)i, FALSE);
                break;
            }
    }
    std::vector<int> SelectedIndices()
    {
        std::vector<int> v;
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
            v.push_back(i);
        return v;
    }

    // ---- commands -------------------------------------------------------
    LRESULT OnRefresh(WORD, WORD, HWND, BOOL&) { Refresh(); return 0; }
    LRESULT OnExit(WORD, WORD, HWND, BOOL&) { PostMessage(WM_CLOSE); return 0; }

    LRESULT OnSelectAll(WORD, WORD, HWND, BOOL&)
    {
        m_list.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
        UpdateStatus();
        return 0;
    }

    LRESULT OnGridLines(WORD, WORD, HWND, BOOL&) { m_gridLines = !m_gridLines; ApplyGridLines(); return 0; }

    LRESULT OnAutoSize(WORD, WORD, HWND, BOOL&)
    {
        for (int i = 0; i < (int)m_visible.size(); ++i)
            m_list.SetColumnWidth(i, LVSCW_AUTOSIZE);
        return 0;
    }

    LRESULT OnCopy(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        if (sel.empty()) return 0;
        std::wstring text = BuildReport(sel, m_visible, ExportFormat::Tab);
        CopyToClipboard(text);
        return 0;
    }

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        if (sel.empty()) sel = m_items;

        CFileDialog dlg(FALSE, L"txt", L"mimetypes.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
            L"Tab delimited file (*.txt)\0*.txt\0Tabular Text File (*.txt)\0*.txt\0"
            L"Text Files (*.txt)\0*.txt\0HTML File - Horizontal (*.htm;*.html)\0*.htm;*.html\0"
            L"HTML File - Vertical (*.htm;*.html)\0*.htm;*.html\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        if (dlg.DoModal() != IDOK) return 0;

        static const ExportFormat byIndex[] = { ExportFormat::Tab, ExportFormat::Tabular,
            ExportFormat::Text, ExportFormat::HtmlHorizontal, ExportFormat::HtmlVertical };
        ExportFormat fmt = byIndex[std::min<DWORD>(dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex - 1 : 0, 4)];
        WriteReport(dlg.m_szFileName, sel, m_visible, fmt);
        return 0;
    }

    LRESULT OnHtmlHorz(WORD, WORD, HWND, BOOL&) { ShowHtmlReport(ExportFormat::HtmlHorizontal); return 0; }
    LRESULT OnHtmlVert(WORD, WORD, HWND, BOOL&) { ShowHtmlReport(ExportFormat::HtmlVertical); return 0; }

    void ShowHtmlReport(ExportFormat fmt)
    {
        auto sel = SelectedItems();
        if (sel.empty()) sel = m_items;
        wchar_t tmp[MAX_PATH], dir[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        ::GetTempFileNameW(dir, L"mrp", 0, tmp);
        std::wstring html = tmp; html += L".html";
        WriteReport(html, sel, m_visible, fmt);
        ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    LRESULT OnChooseColumns(WORD, WORD, HWND, BOOL&)
    {
        CColumnsDlg dlg(m_visible);
        if (dlg.DoModal(m_hWnd) == IDOK)
        {
            m_visible = dlg.m_visible;
            if (std::find(m_visible.begin(), m_visible.end(), m_sortCol) == m_visible.end() && !m_visible.empty())
                m_sortCol = m_visible.front();
            RebuildColumns();
            m_list.Invalidate();
        }
        return 0;
    }

    LRESULT OnProperties(WORD, WORD, HWND, BOOL&)
    {
        int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (i < 0 || i >= (int)m_items.size()) return 0;
        CPropertiesDlg dlg(m_items[i], m_visible);
        dlg.DoModal(m_hWnd);
        return 0;
    }

    LRESULT OnAbout(WORD, WORD, HWND, BOOL&) { CAboutDlg().DoModal(m_hWnd); return 0; }

    LRESULT OnFind(WORD, WORD, HWND, BOOL&)
    {
        if (m_pFind) { m_pFind->SetFocus(); return 0; }
        m_pFind = new CFindReplaceDialog();
        m_pFind->Create(TRUE, L"", nullptr, FR_DOWN, m_hWnd);
        return 0;
    }

    // ---- enable / disable file dialog (writes EditFlags) ----------------
    LRESULT OnEnableDlg(WORD, WORD, HWND, BOOL&) { return ToggleFileDialog(true); }
    LRESULT OnDisableDlg(WORD, WORD, HWND, BOOL&) { return ToggleFileDialog(false); }

    LRESULT ToggleFileDialog(bool enable)
    {
        auto sel = SelectedIndices();
        if (sel.empty()) return 0;
        const wchar_t* q = enable ? L"Do you want to enable the file dialog in the selected MIME types ?"
                                  : L"Do you want to disable the file dialog in the selected MIME types ?";
        if (MessageBox(q, L"MIMEView", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;

        for (int i : sel) SetFileDialogFlag(m_items[i], enable);
        Refresh();
        return 0;
    }

    // "File dialog enabled" means FTA_OpenIsSafe (0x10000) is CLEARED on the ProgID's EditFlags.
    void SetFileDialogFlag(const MimeType& m, bool enable)
    {
        if (m.extension.empty()) return;
        RegKey ext;
        if (!ext.open(HKEY_CLASSES_ROOT, m.extension)) return;
        auto progId = ext.queryString(L"");
        if (!progId || progId->empty()) return;
        RegKey prog;
        if (!prog.create(HKEY_CLASSES_ROOT, *progId)) return;
        DWORD flags = prog.queryDword(L"EditFlags").value_or(0);
        if (enable) flags &= ~0x00010000u; else flags |= 0x00010000u;
        prog.setDword(L"EditFlags", flags);
    }

    // ---- notifications --------------------------------------------------
    LRESULT OnDblClk(int, LPNMHDR, BOOL&) { BOOL b; OnProperties(0, 0, nullptr, b); return 0; }

    LRESULT OnRClick(int, LPNMHDR, BOOL&)
    {
        HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CONTEXT_MENU));
        if (!menu) return 0;
        HMENU sub = ::GetSubMenu(menu, 0);
        POINT pt; ::GetCursorPos(&pt);
        ::TrackPopupMenu(sub, TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, nullptr);
        ::DestroyMenu(menu);
        return 0;
    }

    // ---- helpers --------------------------------------------------------
    std::vector<MimeType> SelectedItems()
    {
        std::vector<MimeType> v;
        for (int i : SelectedIndices()) v.push_back(m_items[i]);
        return v;
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

    // ---- config ---------------------------------------------------------
    void LoadConfig()
    {
        RegKey k;
        if (!k.open(HKEY_CURRENT_USER, kConfigKey)) return;
        if (auto v = k.queryDword(L"Sort1")) m_sortCol = std::clamp((int)*v, 0, kColumnCount - 1);
        if (auto v = k.queryDword(L"ShowGridLines")) m_gridLines = (*v != 0);
        if (auto wp = k.queryBinary(L"WinPos"); wp && wp->size() >= sizeof(WINDOWPLACEMENT))
        {
            WINDOWPLACEMENT p{};
            memcpy(&p, wp->data(), sizeof(p));
            p.length = sizeof(p);
            SetWindowPlacement(&p);
            m_startShowCmd = p.showCmd;
        }
    }

    void SaveConfig()
    {
        RegKey k;
        if (!k.create(HKEY_CURRENT_USER, kConfigKey)) return;
        k.setDword(L"Sort1", (DWORD)m_sortCol);
        k.setDword(L"ShowGridLines", m_gridLines ? 1 : 0);
        WINDOWPLACEMENT p{ sizeof(p) };
        GetWindowPlacement(&p);
        k.setBinary(L"WinPos", &p, sizeof(p));
    }

    void OnDestroy()
    {
        SaveConfig();
        PostQuitMessage(0);
    }
};
