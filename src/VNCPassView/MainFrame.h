// MainFrame.h -- WTL main window for the VNCPassView recreation.
// Visual layout (menu 102, toolbar bitmap 104, status bar, report ListView) matches
// the original resources and WM_CREATE (sub_406185). Logic is clean C++20 over the
// EnumerateVncPasswords() model rather than the decompiled framework code.
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
#include "VncPassword.h"
#include "Columns.h"
#include "Export.h"
#include "Dialogs.h"

// Legacy registry root, present-but-dead in the original binary. The live settings
// store is BasicProg.cfg next to the exe (see ConfigPath). Kept only for /deleteregkey.
constexpr wchar_t kConfigKey[] = L"Software\\0xntpower\\VNCPassView";

extern CAppModule _Module;

class CMainFrame : public CFrameWindowImpl<CMainFrame>, public CMessageFilter
{
public:
    DECLARE_FRAME_WND_CLASS(L"VNCPassView", IDR_MAIN_MENU)

    CListViewCtrl m_list;
    CImageList    m_images;
    std::vector<VncPassword> m_items;
    std::vector<int>      m_visible;     // visible column indices, in display order
    int  m_sortCol = 0;
    bool m_sortAsc = true;
    bool m_gridLines = true;
    bool m_noSaveLoad = false;   // /nosaveload: skip loading and saving settings
    int  m_saveFilterIndex = 1;
    int  m_startShowCmd = SW_SHOWNORMAL;
    std::wstring m_cfgPath;
    HACCEL m_hAccel = nullptr;
    CFindReplaceDialog* m_pFind = nullptr;
    UINT m_findMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);  // the Find dialog's registered message

    CMainFrame()
    {
        for (int i = 0; i < kColumnCount; ++i) m_visible.push_back(i);
        m_cfgPath = ConfigPath();
    }

    // The original hardcodes the leftover skeleton name BasicProg.cfg, in the exe's folder.
    [[nodiscard]] static std::wstring ConfigPath()
    {
        wchar_t exe[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring p = exe;
        size_t s = p.find_last_of(L"\\/");
        if (s != std::wstring::npos) p.resize(s + 1);
        return p + L"BasicProg.cfg";
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
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MESSAGE_HANDLER(m_findMsg, OnFindReplace)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_GETDISPINFO, OnGetDispInfo)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_COLUMNCLICK, OnColumnClick)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_DBLCLK, OnDblClk)
        NOTIFY_HANDLER(IDC_LISTVIEW, NM_RCLICK, OnRClick)
        NOTIFY_HANDLER(IDC_LISTVIEW, LVN_ITEMCHANGED, OnItemChanged)
        COMMAND_ID_HANDLER(ID_VIEW_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
        COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
        COMMAND_ID_HANDLER(ID_EDIT_SELECTALL, OnSelectAll)
        COMMAND_ID_HANDLER(ID_EDIT_DESELECTALL, OnDeselectAll)
        COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
        COMMAND_ID_HANDLER(ID_VIEW_GRIDLINES, OnGridLines)
        COMMAND_ID_HANDLER(ID_VIEW_AUTOSIZE, OnAutoSize)
        COMMAND_ID_HANDLER(ID_VIEW_COLUMNS, OnChooseColumns)
        COMMAND_ID_HANDLER(ID_VIEW_HTML_ALL, OnHtmlAll)
        COMMAND_ID_HANDLER(ID_VIEW_HTML_SEL, OnHtmlSel)
        COMMAND_ID_HANDLER(ID_FILE_PROPERTIES, OnProperties)
        COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
        CHAIN_MSG_MAP(CFrameWindowImpl<CMainFrame>)
    END_MSG_MAP()

    LRESULT OnEraseBkgnd(CDCHandle dc) { return FillFrameBtnFace(m_hWnd, dc); }

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

        m_images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 1);
        if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_TYPE)))
            m_images.AddIcon(hi);
        m_list.SetImageList(m_images, LVSIL_SMALL);

        SetWindowTextW(L"VNCPassView");
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
        PinToolbarLight(tb);
        tb.SetImageList(LoadToolbarImageList(ModuleHelper::GetResourceInstance(), IDB_TOOLBAR));
        TBBUTTON b[5] = {};
        auto set = [&](int i, int bmp, int cmd) {
            b[i].iBitmap = bmp; b[i].idCommand = cmd; b[i].fsState = TBSTATE_ENABLED; b[i].fsStyle = TBSTYLE_BUTTON;
        };
        set(0, 0, ID_FILE_SAVE);
        set(1, 1, ID_VIEW_REFRESH);
        set(2, 2, ID_EDIT_COPY);
        set(3, 3, ID_FILE_PROPERTIES);
        set(4, 4, ID_FILE_EXIT);
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
        m_items = EnumerateVncPasswords();
        SortItems();
        m_list.SetItemCount(static_cast<int>(m_items.size()));
        m_list.Invalidate();
        UpdateStatus();
        ::SetCursor(prev);
    }

    // ---- sorting --------------------------------------------------------
    void SortItems()
    {
        const int col = m_sortCol;
        const bool asc = m_sortAsc;
        std::stable_sort(m_items.begin(), m_items.end(), [col, asc](const VncPassword& a, const VncPassword& b) {
            int c = _wcsicmp(a.column(col).c_str(), b.column(col).c_str());
            return asc ? (c < 0) : (c > 0);
        });
    }

    LRESULT OnColumnClick(int, LPNMHDR p, BOOL&)
    {
        int clicked = m_visible[reinterpret_cast<LPNMLISTVIEW>(p)->iSubItem];
        if (clicked == m_sortCol) m_sortAsc = !m_sortAsc;
        else { m_sortCol = clicked; m_sortAsc = true; }
        SortItems();
        m_list.Invalidate();
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
            di->item.iImage = 0;
        return 0;
    }

    LRESULT OnItemChanged(int, LPNMHDR, BOOL&) { UpdateStatus(); return 0; }

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
    std::vector<int> SelectedIndices()
    {
        std::vector<int> v;
        for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
            v.push_back(i);
        return v;
    }
    std::vector<VncPassword> SelectedItems()
    {
        std::vector<VncPassword> v;
        for (int i : SelectedIndices()) v.push_back(m_items[i]);
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
    LRESULT OnDeselectAll(WORD, WORD, HWND, BOOL&)
    {
        m_list.SetItemState(-1, 0, LVIS_SELECTED);
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
        CopyToClipboard(BuildReport(sel, m_visible, ExportFormat::Tab));
        return 0;
    }

    LRESULT OnSave(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        if (sel.empty()) sel = m_items;

        CFileDialog dlg(FALSE, L"txt", L"VNCPassView.txt",
            OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
            L"Text File (*.txt)\0*.txt\0Tab delimited text file (*.txt)\0*.txt\0"
            L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.htm;*.html)\0*.htm;*.html\0"
            L"HTML File - Vertical (*.htm;*.html)\0*.htm;*.html\0XML File (*.xml)\0*.xml\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a filename to save";
        dlg.m_ofn.nFilterIndex = m_saveFilterIndex;
        if (dlg.DoModal() != IDOK) return 0;

        m_saveFilterIndex = dlg.m_ofn.nFilterIndex ? dlg.m_ofn.nFilterIndex : 1;
        static const ExportFormat byIndex[] = { ExportFormat::Text, ExportFormat::Tab, ExportFormat::Tabular,
            ExportFormat::HtmlHorizontal, ExportFormat::HtmlVertical, ExportFormat::Xml };
        ExportFormat fmt = byIndex[std::min<DWORD>(m_saveFilterIndex - 1, 5)];
        WriteReport(dlg.m_szFileName, sel, m_visible, fmt);
        return 0;
    }

    LRESULT OnHtmlAll(WORD, WORD, HWND, BOOL&) { ShowHtmlReport(m_items); return 0; }
    LRESULT OnHtmlSel(WORD, WORD, HWND, BOOL&)
    {
        auto sel = SelectedItems();
        if (sel.empty()) sel = m_items;
        ShowHtmlReport(sel);
        return 0;
    }

    void ShowHtmlReport(const std::vector<VncPassword>& items)
    {
        wchar_t tmp[MAX_PATH], dir[MAX_PATH];
        ::GetTempPathW(MAX_PATH, dir);
        ::GetTempFileNameW(dir, L"vnc", 0, tmp);
        std::wstring html = tmp; html += L".html";
        WriteReport(html, items, m_visible, ExportFormat::HtmlHorizontal);
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

    LRESULT OnFindReplace(UINT, WPARAM, LPARAM lParam, BOOL&)
    {
        auto* fr = reinterpret_cast<LPFINDREPLACEW>(lParam);
        if (fr->Flags & FR_DIALOGTERM) { m_pFind = nullptr; return 0; }
        if (fr->Flags & FR_FINDNEXT)   FindNext(fr->lpstrFindWhat, (fr->Flags & FR_DOWN) != 0);
        return 0;
    }

    void FindNext(const std::wstring& needle, bool down)
    {
        if (needle.empty() || m_items.empty()) return;
        int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
        int n = (int)m_items.size();
        std::wstring low = needle; std::transform(low.begin(), low.end(), low.begin(), towlower);
        for (int step = 1; step <= n; ++step)
        {
            int i = down ? (start + step) % n : (start - step + n) % n;
            std::wstring row;
            for (int c : m_visible) row += m_items[i].column(c) + L" ";
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

    // ---- config (BasicProg.cfg INI, [General], matching the original store) ----
    [[nodiscard]] static std::vector<BYTE> parseHexBytes(const std::wstring& s)
    {
        std::vector<BYTE> out;
        for (size_t i = 0; i < s.size(); )
        {
            while (i < s.size() && s[i] == L' ') ++i;
            if (i >= s.size()) break;
            out.push_back(static_cast<BYTE>(wcstoul(s.substr(i, 2).c_str(), nullptr, 16)));
            while (i < s.size() && s[i] != L' ') ++i;
        }
        return out;
    }
    [[nodiscard]] static std::wstring hexBytes(const void* data, size_t n)
    {
        const BYTE* p = static_cast<const BYTE*>(data);
        std::wstring s;
        wchar_t b[4] = {};
        for (size_t i = 0; i < n; ++i) { wsprintfW(b, i ? L" %02X" : L"%02X", p[i]); s += b; }
        return s;
    }
    [[nodiscard]] std::wstring iniGet(const wchar_t* key)
    {
        wchar_t buf[1024] = {};
        ::GetPrivateProfileStringW(L"General", key, L"", buf, static_cast<DWORD>(std::size(buf)), m_cfgPath.c_str());
        return buf;
    }

    void LoadConfig()
    {
        if (m_noSaveLoad) return;
        m_sortCol = std::clamp(static_cast<int>(::GetPrivateProfileIntW(L"General", L"Sort", m_sortCol, m_cfgPath.c_str())), 0, kColumnCount - 1);
        m_gridLines = ::GetPrivateProfileIntW(L"General", L"ShowGridLines", m_gridLines ? 1 : 0, m_cfgPath.c_str()) != 0;
        m_saveFilterIndex = std::clamp(static_cast<int>(::GetPrivateProfileIntW(L"General", L"SaveFilterIndex", m_saveFilterIndex, m_cfgPath.c_str())), 1, 6);
        auto wp = parseHexBytes(iniGet(L"WinPos"));
        if (wp.size() >= sizeof(WINDOWPLACEMENT))
        {
            WINDOWPLACEMENT p{};
            memcpy(&p, wp.data(), sizeof(p));
            p.length = sizeof(p);
            SetWindowPlacement(&p);
            m_startShowCmd = p.showCmd;
        }
    }

    void SaveConfig()
    {
        if (m_noSaveLoad) return;
        wchar_t num[16] = {};
        wsprintfW(num, L"%d", m_sortCol);         ::WritePrivateProfileStringW(L"General", L"Sort", num, m_cfgPath.c_str());
        ::WritePrivateProfileStringW(L"General", L"ShowGridLines", m_gridLines ? L"1" : L"0", m_cfgPath.c_str());
        wsprintfW(num, L"%d", m_saveFilterIndex); ::WritePrivateProfileStringW(L"General", L"SaveFilterIndex", num, m_cfgPath.c_str());
        WINDOWPLACEMENT p{ sizeof(p) };
        GetWindowPlacement(&p);
        ::WritePrivateProfileStringW(L"General", L"WinPos", hexBytes(&p, sizeof(p)).c_str(), m_cfgPath.c_str());
    }

    void OnDestroy()
    {
        SaveConfig();
        PostQuitMessage(0);
    }
};
