// MainFrame.cpp -- the Autoruns main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the autostart enumeration and the background
// Authenticode verify, the tab-driven list rebuild with its bold location
// headers, the Jump, Verify, Properties, Copy and Find commands, the registry
// config load and save, and the toolbar-light fix (PinToolbarLight after the
// toolbar is made, OnEraseBkgnd filling the frame with button-face).
#include "MainFrame.h"

#include "AutorunEntry.h"
#include "Enumerators.h"
#include "Signature.h"
#include "Dialogs.h"
#include "Eula.h"
#include "../common/UiUtil.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

BOOL CMainFrame::PreTranslateMessage(MSG* pMsg)
{
    if (m_hAccel && ::TranslateAcceleratorW(m_hWnd, m_hAccel, pMsg))
    {
        return TRUE;
    }
    if (m_findDlg && m_findDlg->IsWindow() && m_findDlg->IsDialogMessageW(pMsg))
    {
        return TRUE;
    }
    return CFrameWindowImpl<CMainFrame>::PreTranslateMessage(pMsg);
}

// ---- creation -----------------------------------------------------------
int CMainFrame::OnCreate(LPCREATESTRUCT)
{
    m_findMsg = ::RegisterWindowMessageW(FINDMSGSTRINGW);
    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_MAINFRAME));

    wchar_t w[MAX_PATH]{};
    ::GetWindowsDirectoryW(w, MAX_PATH);
    m_winDir = w;

    LoadConfig();
    CreateToolbar();

    m_tabs.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS, 0, IDC_TABS);
    m_tabs.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
    m_tabs.InsertItem(0, L"Everything");
    for (int c = 0; c < static_cast<int>(ar::Category::Count); ++c)
    {
        m_tabs.InsertItem(c + 1, ar::CategoryName(static_cast<ar::Category>(c)));
    }

    m_list.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, IDC_LIST);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);
    m_list.SetFont(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
    BuildColumns();

    // The shell small-icon image list draws each image's own icon.
    SHFILEINFOW sfi{};
    if (HIMAGELIST sys = reinterpret_cast<HIMAGELIST>(
            ::SHGetFileInfoW(L"", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON)))
    {
        m_list.SetImageList(sys, LVSIL_SMALL);
    }

    m_status.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | CCS_BOTTOM | SBARS_SIZEGRIP, 0, IDC_STATUSBAR);

    // A bold font for the location header rows.
    LOGFONTW lf{};
    ::GetObjectW(reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)), sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    m_boldFont = ::CreateFontIndirectW(&lf);

    if (HICON hi = static_cast<HICON>(::LoadImageW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_MAINFRAME),
                                                   IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    SetWindowTextW(L"Autoruns - Sysinternals: www.sysinternals.com");

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }

    SyncMenu();
    if (m_alwaysOnTop)
    {
        SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    if (m_havePlacement)
    {
        SetWindowPlacement(&m_placement);
    }

    Scan();
    return 0;
}

void CMainFrame::CreateToolbar()
{
    m_toolbar.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_TOP, 0,
                     IDC_TOOLBAR);
    m_toolbar.SetButtonStructSize(sizeof(TBBUTTON));

    // Reuse the original's own icon resources for the toolbar glyphs.
    const UINT iconIds[] = {230, 232, 233, 234, 235, 237, 238, 239, 240, 241};
    m_tbImages = ::ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, static_cast<int>(std::size(iconIds)), 0);
    for (UINT id : iconIds)
    {
        HICON ic = static_cast<HICON>(::LoadImageW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(id),
                                                   IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        if (ic)
        {
            ::ImageList_AddIcon(m_tbImages, ic);
            ::DestroyIcon(ic);
        }
    }
    m_toolbar.SetImageList(m_tbImages);

    struct
    {
        int image;
        int cmd;
    } defs[] = {{0, ID_FILE_SAVE},
                {1, ID_VIEW_REFRESH},
                {-1, 0},
                {2, ID_HIDE_WINDOWS},
                {3, ID_HIDE_MICROSOFT},
                {-1, 0},
                {4, ID_EDIT_FIND},
                {-1, 0},
                {5, ID_JUMP_ENTRY},
                {6, ID_JUMP_IMAGE},
                {7, ID_VERIFY_IMAGE},
                {8, ID_PROPERTIES},
                {-1, 0},
                {9, ID_APP_ABOUT}};
    for (auto& d : defs)
    {
        TBBUTTON b{};
        if (d.image < 0)
        {
            b.fsStyle = BTNS_SEP;
        }
        else
        {
            b.iBitmap = d.image;
            b.idCommand = d.cmd;
            b.fsState = TBSTATE_ENABLED;
            b.fsStyle = BTNS_BUTTON;
        }
        m_toolbar.AddButtons(1, &b);
    }
    m_toolbar.AutoSize();

    // Drop the themed band so the toolbar reads light on a dark-mode system.
    PinToolbarLight(m_toolbar);
}

void CMainFrame::BuildColumns()
{
    m_list.InsertColumn(0, L"Autorun Entry", LVCFMT_LEFT, 250);
    m_list.InsertColumn(1, L"Description", LVCFMT_LEFT, 180);
    m_list.InsertColumn(2, L"Publisher", LVCFMT_LEFT, 180);
    m_list.InsertColumn(3, L"Image Path", LVCFMT_LEFT, 300);
    m_list.InsertColumn(4, L"Timestamp", LVCFMT_LEFT, 130);
    // The original's VirusTotal column is shown for fidelity and left empty,
    // the VirusTotal integration itself is out of scope.
    m_list.InsertColumn(5, L"Virus Total", LVCFMT_LEFT, 110);
}

LRESULT CMainFrame::OnEraseBkgnd(CDCHandle dc)
{
    return FillFrameBtnFace(m_hWnd, dc);
}

// ---- scan and verify ----------------------------------------------------
void CMainFrame::Scan()
{
    StopVerify();
    {
        ScopedWait wc;
        m_locations = ar::EnumerateAll();
    }
    RebuildList();
    StartVerify();
}

void CMainFrame::StartVerify()
{
    // The set of distinct image paths to check, so a shared image such as
    // svchost is verified once.
    std::vector<std::wstring> paths;
    for (const ar::Location& loc : m_locations)
    {
        for (const ar::AutorunEntry& e : loc.entries)
        {
            if (!e.imagePath.empty())
            {
                paths.push_back(e.imagePath);
            }
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    if (paths.empty())
    {
        SetStatus(L"Ready.");
        return;
    }
    SetStatus(L"Verifying image signatures...");
    m_stop = false;
    const unsigned gen = ++m_scanGen;
    HWND hwnd = m_hWnd;
    m_verify = std::thread(
        [this, hwnd, gen, paths = std::move(paths)]() mutable
        {
            auto* results = new std::map<std::wstring, ar::sig::Info>();
            for (const std::wstring& p : paths)
            {
                if (m_stop.load())
                {
                    break;
                }
                (*results)[p] = ar::sig::Query(p);
            }
            if (m_stop.load())
            {
                delete results;
            }
            else
            {
                ::PostMessageW(hwnd, WM_APP_VERIFY_DONE, gen, reinterpret_cast<LPARAM>(results));
            }
        });
}

void CMainFrame::StopVerify()
{
    m_stop = true;
    if (m_verify.joinable())
    {
        m_verify.join();
    }
    m_stop = false;
}

LRESULT CMainFrame::OnVerifyDone(UINT, WPARAM wParam, LPARAM lParam, BOOL&)
{
    auto* results = reinterpret_cast<std::map<std::wstring, ar::sig::Info>*>(lParam);
    if (static_cast<unsigned>(wParam) != m_scanGen.load())
    {
        delete results;
        return 0;
    }
    for (ar::Location& loc : m_locations)
    {
        for (ar::AutorunEntry& e : loc.entries)
        {
            auto it = results->find(e.imagePath);
            if (it == results->end())
            {
                continue;
            }
            const ar::sig::Info& info = it->second;
            e.sig = info.state;
            e.signer = info.signer;
            e.version = info.version;
            if (!info.description.empty())
            {
                e.description = info.description;
            }
            if (!info.company.empty())
            {
                e.company = info.company;
            }
        }
    }
    delete results;
    if (m_verify.joinable())
    {
        m_verify.join();
    }
    RebuildList();
    SetStatus(L"Ready.");
    return 0;
}

// ---- filters ------------------------------------------------------------
// A Microsoft entry is one whose Authenticode signer or company names
// Microsoft once verification has run. A Windows entry is a Microsoft one
// whose image lives under the Windows directory. Both key off the signature
// result, so nothing is hidden until verification completes.
bool CMainFrame::IsMicrosoftEntry(const ar::AutorunEntry& e) const
{
    return e.sig == ar::SigState::Verified && e.IsMicrosoft();
}

bool CMainFrame::IsWindowsEntry(const ar::AutorunEntry& e) const
{
    if (!IsMicrosoftEntry(e) || e.imagePath.empty() || m_winDir.empty())
    {
        return false;
    }
    return _wcsnicmp(e.imagePath.c_str(), m_winDir.c_str(), m_winDir.size()) == 0;
}

bool CMainFrame::EntryVisible(const ar::AutorunEntry& e) const
{
    if (m_hideWindows && IsWindowsEntry(e))
    {
        return false;
    }
    if (m_hideMicrosoft && IsMicrosoftEntry(e))
    {
        return false;
    }
    return true;
}

// ---- list build ---------------------------------------------------------
void CMainFrame::RebuildList()
{
    const int tab = m_tabs.GetCurSel();
    const bool everything = (tab <= 0);
    const ar::Category cat = static_cast<ar::Category>(tab - 1);

    m_list.SetRedraw(FALSE);
    m_list.DeleteAllItems();
    m_rows.clear();

    int row = 0;
    int shown = 0;
    for (int li = 0; li < static_cast<int>(m_locations.size()); ++li)
    {
        const ar::Location& loc = m_locations[li];
        if (!everything && loc.category != cat)
        {
            continue;
        }
        std::vector<int> vis;
        for (int ei = 0; ei < static_cast<int>(loc.entries.size()); ++ei)
        {
            if (EntryVisible(loc.entries[ei]))
            {
                vis.push_back(ei);
            }
        }
        if (m_hideEmpty && vis.empty())
        {
            continue;
        }

        m_list.InsertItem(row, loc.label.c_str());
        m_list.SetItemData(row, static_cast<DWORD_PTR>(m_rows.size()));
        m_rows.push_back({true, li, -1});
        ++row;

        for (int ei : vis)
        {
            const ar::AutorunEntry& e = loc.entries[ei];
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(e.name.c_str());
            item.iImage = IconIndex(e.imagePath);
            item.lParam = static_cast<LPARAM>(m_rows.size());
            m_list.InsertItem(&item);
            m_list.SetItemText(row, 1, e.description.c_str());
            m_list.SetItemText(row, 2, e.Publisher().c_str());
            m_list.SetItemText(row, 3, e.imagePath.c_str());
            m_list.SetItemText(row, 4, e.timestamp.c_str());
            m_list.SetCheckState(row, e.enabled);
            m_rows.push_back({false, li, ei});
            ++row;
            ++shown;
        }
    }
    m_list.SetRedraw(TRUE);
    m_list.Invalidate();

    wchar_t s[64]{};
    wsprintfW(s, L"%d entr%s", shown, shown == 1 ? L"y" : L"ies");
    SetStatus(s);
}

int CMainFrame::IconIndex(const std::wstring& path)
{
    if (path.empty())
    {
        return -1;
    }
    SHFILEINFOW sfi{};
    if (::SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
    {
        return sfi.iIcon;
    }
    return -1;
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
        const size_t ri = static_cast<size_t>(cd->nmcd.lItemlParam);
        if (ri < m_rows.size() && m_rows[ri].header)
        {
            ::SelectObject(cd->nmcd.hdc, m_boldFont);
            return CDRF_NEWFONT;
        }
        return CDRF_DODEFAULT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

LRESULT CMainFrame::OnDblClick(int, LPNMHDR, BOOL&)
{
    DoJumpEntry();
    return 0;
}

// ---- row helpers --------------------------------------------------------
const ar::AutorunEntry* CMainFrame::FocusedEntry()
{
    const int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0)
    {
        return nullptr;
    }
    const size_t ri = static_cast<size_t>(m_list.GetItemData(sel));
    if (ri >= m_rows.size() || m_rows[ri].header)
    {
        return nullptr;
    }
    return &m_locations[m_rows[ri].loc].entries[m_rows[ri].entry];
}

const ar::Location* CMainFrame::FocusedLocation()
{
    const int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0)
    {
        return nullptr;
    }
    const size_t ri = static_cast<size_t>(m_list.GetItemData(sel));
    if (ri >= m_rows.size())
    {
        return nullptr;
    }
    return &m_locations[m_rows[ri].loc];
}

// ---- commands -----------------------------------------------------------
LRESULT CMainFrame::OnRefresh(WORD, WORD, HWND, BOOL&)
{
    Scan();
    return 0;
}

LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&)
{
    PostMessage(WM_CLOSE);
    return 0;
}

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&)
{
    CAboutDlg().DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnHelp(WORD, WORD, HWND, BOOL&)
{
    ::ShellExecuteW(m_hWnd, L"open", L"https://learn.microsoft.com/sysinternals/downloads/autoruns", nullptr, nullptr,
                    SW_SHOWNORMAL);
    return 0;
}

LRESULT CMainFrame::OnHideWindows(WORD, WORD, HWND, BOOL&)
{
    m_hideWindows = !m_hideWindows;
    SyncMenu();
    RebuildList();
    return 0;
}

LRESULT CMainFrame::OnHideMicrosoft(WORD, WORD, HWND, BOOL&)
{
    m_hideMicrosoft = !m_hideMicrosoft;
    SyncMenu();
    RebuildList();
    return 0;
}

LRESULT CMainFrame::OnHideEmpty(WORD, WORD, HWND, BOOL&)
{
    m_hideEmpty = !m_hideEmpty;
    SyncMenu();
    RebuildList();
    return 0;
}

LRESULT CMainFrame::OnAlwaysOnTop(WORD, WORD, HWND, BOOL&)
{
    m_alwaysOnTop = !m_alwaysOnTop;
    SetWindowPos(m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SyncMenu();
    return 0;
}

LRESULT CMainFrame::OnTabChange(int, LPNMHDR, BOOL&)
{
    RebuildList();
    return 0;
}

// Jump to Entry opens the configuring location: regedit at the key, or the
// folder in Explorer. Setting regedit's LastKey preference is the documented
// way to point it at a key, and touches no autorun entry.
void CMainFrame::DoJumpEntry()
{
    const ar::Location* loc = FocusedLocation();
    if (!loc)
    {
        return;
    }
    if (loc->isRegistry)
    {
        HKEY k = nullptr;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit", 0,
                              nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS)
        {
            ::RegSetValueExW(k, L"LastKey", 0, REG_SZ, reinterpret_cast<const BYTE*>(loc->jumpPath.c_str()),
                             static_cast<DWORD>((loc->jumpPath.size() + 1) * sizeof(wchar_t)));
            ::RegCloseKey(k);
        }
        ::ShellExecuteW(m_hWnd, L"open", L"regedit.exe", nullptr, nullptr, SW_SHOWNORMAL);
    }
    else
    {
        ::ShellExecuteW(m_hWnd, L"open", loc->jumpPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

LRESULT CMainFrame::OnJumpEntry(WORD, WORD, HWND, BOOL&)
{
    DoJumpEntry();
    return 0;
}

LRESULT CMainFrame::OnJumpImage(WORD, WORD, HWND, BOOL&)
{
    const ar::AutorunEntry* e = FocusedEntry();
    if (e && !e->imagePath.empty())
    {
        std::wstring arg = L"/select,\"" + e->imagePath + L"\"";
        ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    }
    return 0;
}

// Verify Image re-checks the focused entry's image on the spot.
LRESULT CMainFrame::OnVerifyImage(WORD, WORD, HWND, BOOL&)
{
    const int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    const ar::AutorunEntry* ce = FocusedEntry();
    if (!ce || ce->imagePath.empty())
    {
        return 0;
    }
    const size_t ri = static_cast<size_t>(m_list.GetItemData(sel));
    ar::AutorunEntry& e = m_locations[m_rows[ri].loc].entries[m_rows[ri].entry];
    {
        ScopedWait wc;
        const ar::sig::Info info = ar::sig::Query(e.imagePath);
        e.sig = info.state;
        e.signer = info.signer;
        if (!info.description.empty())
        {
            e.description = info.description;
        }
        if (!info.company.empty())
        {
            e.company = info.company;
        }
    }
    m_list.SetItemText(sel, 1, e.description.c_str());
    m_list.SetItemText(sel, 2, e.Publisher().c_str());
    return 0;
}

LRESULT CMainFrame::OnProperties(WORD, WORD, HWND, BOOL&)
{
    const ar::AutorunEntry* e = FocusedEntry();
    if (e && !e->imagePath.empty())
    {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.hwnd = m_hWnd;
        sei.lpVerb = L"properties";
        sei.lpFile = e->imagePath.c_str();
        sei.nShow = SW_SHOW;
        ::ShellExecuteExW(&sei);
    }
    return 0;
}

LRESULT CMainFrame::OnCopy(WORD, WORD, HWND, BOOL&)
{
    const int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
    if (sel < 0)
    {
        return 0;
    }
    const ar::AutorunEntry* e = FocusedEntry();
    std::wstring text;
    if (e)
    {
        text = e->name + L"\t" + e->description + L"\t" + e->Publisher() + L"\t" + e->imagePath + L"\t" + e->timestamp;
    }
    else if (const ar::Location* loc = FocusedLocation())
    {
        text = loc->label;
    }
    CopyToClipboard(text);
    return 0;
}

// The original can delete and disable entries. This recreation never writes,
// so Delete is refused explicitly.
LRESULT CMainFrame::OnReadOnly(WORD, WORD, HWND, BOOL&)
{
    ::MessageBoxW(m_hWnd,
                  L"This recreation is read-only. It enumerates and displays autostart "
                  L"entries, and does not delete, disable or modify them.",
                  L"Autoruns", MB_OK | MB_ICONINFORMATION);
    return 0;
}

LRESULT CMainFrame::OnOutOfScope(WORD, WORD, HWND, BOOL&)
{
    ::MessageBoxW(m_hWnd,
                  L"This feature is out of scope in this core-subset recreation. See "
                  L"analysis/Autoruns.md for the full out-of-scope list.",
                  L"Autoruns", MB_OK | MB_ICONINFORMATION);
    return 0;
}

// ---- find ---------------------------------------------------------------
LRESULT CMainFrame::OnFind(WORD, WORD, HWND, BOOL&)
{
    if (m_findDlg && m_findDlg->IsWindow())
    {
        m_findDlg->SetActiveWindow();
        return 0;
    }
    m_findDlg = new CFindReplaceDialog();
    m_findDlg->Create(TRUE, m_lastFind.c_str(), nullptr, FR_DOWN, m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnFindNext(WORD, WORD, HWND, BOOL&)
{
    if (!m_lastFind.empty())
    {
        DoFind(m_lastFind);
    }
    return 0;
}

LRESULT CMainFrame::OnFindMsg(UINT, WPARAM, LPARAM lParam, BOOL&)
{
    auto* fr = reinterpret_cast<LPFINDREPLACEW>(lParam);
    if (fr->Flags & FR_DIALOGTERM)
    {
        m_findDlg = nullptr;
        return 0;
    }
    if (fr->Flags & FR_FINDNEXT)
    {
        m_lastFind = fr->lpstrFindWhat ? fr->lpstrFindWhat : L"";
        DoFind(m_lastFind);
    }
    return 0;
}

void CMainFrame::DoFind(const std::wstring& needle)
{
    if (needle.empty() || m_rows.empty())
    {
        return;
    }
    std::wstring low = needle;
    std::transform(low.begin(), low.end(), low.begin(), towlower);
    const int n = m_list.GetItemCount();
    const int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
    for (int step = 1; step <= n; ++step)
    {
        const int i = (start + step) % n;
        const size_t ri = static_cast<size_t>(m_list.GetItemData(i));
        if (ri >= m_rows.size() || m_rows[ri].header)
        {
            continue;
        }
        const ar::AutorunEntry& e = m_locations[m_rows[ri].loc].entries[m_rows[ri].entry];
        std::wstring hay = e.name + L" " + e.description + L" " + e.Publisher() + L" " + e.imagePath;
        std::transform(hay.begin(), hay.end(), hay.begin(), towlower);
        if (hay.find(low) != std::wstring::npos)
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
    if (text.empty() || !::OpenClipboard(m_hWnd))
    {
        return;
    }
    ::EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes))
    {
        if (void* dst = ::GlobalLock(h))
        {
            memcpy(dst, text.c_str(), bytes);
            ::GlobalUnlock(h);
            ::SetClipboardData(CF_UNICODETEXT, h);
        }
    }
    ::CloseClipboard();
}

void CMainFrame::SetStatus(const wchar_t* text)
{
    m_status.SetText(0, text);
}

void CMainFrame::SyncMenu()
{
    CMenuHandle menu = GetMenu();
    if (menu.IsNull())
    {
        return;
    }
    menu.CheckMenuItem(ID_HIDE_WINDOWS, MF_BYCOMMAND | (m_hideWindows ? MF_CHECKED : MF_UNCHECKED));
    menu.CheckMenuItem(ID_HIDE_MICROSOFT, MF_BYCOMMAND | (m_hideMicrosoft ? MF_CHECKED : MF_UNCHECKED));
    menu.CheckMenuItem(ID_HIDE_EMPTY, MF_BYCOMMAND | (m_hideEmpty ? MF_CHECKED : MF_UNCHECKED));
    menu.CheckMenuItem(ID_ALWAYS_ONTOP, MF_BYCOMMAND | (m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED));
}

// ---- layout -------------------------------------------------------------
void CMainFrame::OnSize(UINT wParam, CSize)
{
    if (wParam == SIZE_MINIMIZED)
    {
        return;
    }
    m_toolbar.SendMessage(WM_SIZE);
    m_status.SendMessage(WM_SIZE);

    CRect rc;
    GetClientRect(&rc);
    CRect tbr;
    m_toolbar.GetWindowRect(&tbr);
    CRect sbr;
    m_status.GetWindowRect(&sbr);
    const int top = tbr.Height();
    const int bottom = rc.bottom - sbr.Height();

    CRect area(0, top, rc.right, bottom);
    m_tabs.SetWindowPos(nullptr, area.left, area.top, area.Width(), area.Height(), SWP_NOZORDER);

    CRect disp = area;
    m_tabs.AdjustRect(FALSE, &disp);
    disp.DeflateRect(2, 2);
    m_list.SetWindowPos(nullptr, disp.left, disp.top, disp.Width(), disp.Height(), SWP_NOZORDER);
}

// ---- config (registry) --------------------------------------------------
void CMainFrame::LoadConfig()
{
    HKEY k = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
    {
        return;
    }
    auto dword = [&](const wchar_t* name, bool& flag)
    {
        DWORD dw = 0, cb = sizeof(dw), type = 0;
        if (::RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<LPBYTE>(&dw), &cb) == ERROR_SUCCESS)
        {
            flag = dw != 0;
        }
    };
    dword(L"HideWindowsEntries", m_hideWindows);
    dword(L"HideMicrosoftEntries", m_hideMicrosoft);
    dword(L"HideEmptyEntries", m_hideEmpty);
    dword(L"AlwaysOnTop", m_alwaysOnTop);

    WINDOWPLACEMENT wp{};
    DWORD cb = sizeof(wp), type = 0;
    if (::RegQueryValueExW(k, L"WindowPlacement", nullptr, &type, reinterpret_cast<LPBYTE>(&wp), &cb) ==
            ERROR_SUCCESS &&
        cb == sizeof(wp) && wp.length == sizeof(wp))
    {
        m_placement = wp;
        m_havePlacement = true;
    }
    ::RegCloseKey(k);
}

void CMainFrame::SaveConfig()
{
    HKEY k = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
    {
        return;
    }
    auto dword = [&](const wchar_t* name, bool flag)
    {
        DWORD dw = flag ? 1 : 0;
        ::RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
    };
    dword(L"HideWindowsEntries", m_hideWindows);
    dword(L"HideMicrosoftEntries", m_hideMicrosoft);
    dword(L"HideEmptyEntries", m_hideEmpty);
    dword(L"AlwaysOnTop", m_alwaysOnTop);

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(&wp))
    {
        ::RegSetValueExW(k, L"WindowPlacement", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp), sizeof(wp));
    }
    ::RegCloseKey(k);
}

void CMainFrame::OnDestroy()
{
    StopVerify();
    SaveConfig();
    if (m_tbImages)
    {
        ::ImageList_Destroy(m_tbImages);
        m_tbImages = nullptr;
    }
    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->RemoveMessageFilter(this);
    }
    PostQuitMessage(0);
}
