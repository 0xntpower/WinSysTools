// MainFrame.cpp -- the SmartSniff main window method bodies.
//
// Split out of MainFrame.h so the header carries only the class shape. The
// bodies are unchanged: the toolbar and status bar build, the owner-data upper
// list, the draggable splitter, the lower-pane hex/ascii/url render, the export
// and find commands, the config load and save, and the toolbar-light fix
// (PinToolbarLight after the toolbar is made, OnEraseBkgnd filling the frame
// with button-face).
#include "MainFrame.h"

#include "Columns.h"
#include "Config.h"
#include "Packet.h"
#include "Export.h"
#include "Dialogs.h"
#include "PcapLoader.h"
#include "../common/UiUtil.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

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
    if (!m_noLoadSettings)
    {
        m_cfg.load();
    }
    m_hAccel = ::LoadAcceleratorsW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_ACCEL));

    CreateToolbar();
    m_hWndStatusBar = ::CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", m_hWnd, IDC_STATUSBAR);
    int parts[] = {240, 420, -1};
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, 3, (LPARAM)parts);

    m_list.Create(m_hWnd, rcDefault, nullptr,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, IDC_LISTVIEW);
    m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);

    m_lower.Create(m_hWnd, rcDefault, nullptr,
                   WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                       ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                   0, IDC_LOWERPANE);
    LOGFONTW lf{};
    lf.lfHeight = -12;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    m_lowerFont = ::CreateFontIndirectW(&lf);
    m_lower.SetFont(m_lowerFont);

    SetWindowTextW(L"SmartSniff");
    if (HICON hi = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP)))
    {
        SetIcon(hi, TRUE);
        SetIcon(hi, FALSE);
    }

    RebuildColumns();
    ApplyGridLines();
    SyncChecks();
    ApplyAlwaysOnTop();
    if (m_cfg.splitLoc > 0 && m_cfg.splitLoc < 0x3FFF)
    {
        m_upperH = m_cfg.splitLoc;
    }

    if (m_cfg.havePlacement)
    {
        SetWindowPlacement(&m_cfg.placement);
    }

    if (CMessageLoop* pLoop = _Module.GetMessageLoop())
    {
        pLoop->AddMessageFilter(this);
    }
    UpdateStatus();
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
    struct
    {
        int bmp, cmd;
    } btns[] = {
        {5, ID_START_CAPTURE},
        {6, ID_STOP_CAPTURE},
        {10, ID_RESTART_CAPTURE},
        {-1, 0},
        {7, ID_MOVE_NEXT},
        {8, ID_MOVE_PREV},
        {-1, 0},
        {9, ID_HTML_REPORT},
        {1, ID_SAVE_PACKETS},
        {2, ID_COPY},
        {3, ID_PROPERTIES},
        {4, ID_EXIT},
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

// ---- layout ---------------------------------------------------------
int CMainFrame::toolbarHeight()
{
    if (!m_hWndToolBar)
    {
        return 0;
    }
    CRect rc;
    ::GetWindowRect(m_hWndToolBar, &rc);
    return rc.Height();
}
int CMainFrame::statusHeight()
{
    if (!m_hWndStatusBar)
    {
        return 0;
    }
    CRect rc;
    ::GetWindowRect(m_hWndStatusBar, &rc);
    return rc.Height();
}

void CMainFrame::OnSize(UINT, CSize)
{
    if (m_hWndToolBar)
    {
        ::SendMessageW(m_hWndToolBar, TB_AUTOSIZE, 0, 0);
    }
    if (m_hWndStatusBar)
    {
        ::SendMessageW(m_hWndStatusBar, WM_SIZE, 0, 0);
    }

    CRect rc;
    GetClientRect(&rc);
    int top = toolbarHeight();
    int bottom = rc.bottom - statusHeight();
    int usable = bottom - top;
    if (usable < 60)
    {
        return;
    }

    bool hideLower = m_cfg.hideLowerPane;
    if (hideLower)
    {
        m_list.SetWindowPos(nullptr, 0, top, rc.right, usable, SWP_NOZORDER);
        m_lower.ShowWindow(SW_HIDE);
        return;
    }
    m_lower.ShowWindow(SW_SHOW);

    if (m_upperH <= 0)
    {
        m_upperH = usable * 11 / 20;
    }
    int upperH = std::clamp(m_upperH, 40, usable - (kSplitBar + 40));
    m_upperH = upperH;

    int w = rc.right, y = top;
    m_list.SetWindowPos(nullptr, 0, y, w, upperH, SWP_NOZORDER);
    y += upperH + kSplitBar;
    m_lower.SetWindowPos(nullptr, 0, y, w, bottom - y, SWP_NOZORDER);
}

CRect CMainFrame::splitterRect()
{
    CRect rc;
    GetClientRect(&rc);
    int y = toolbarHeight() + m_upperH;
    return CRect(0, y, rc.right, y + kSplitBar);
}
BOOL CMainFrame::OnSetCursor(CWindow wnd, UINT hitTest, UINT)
{
    if (!m_cfg.hideLowerPane && wnd.m_hWnd == m_hWnd && hitTest == HTCLIENT)
    {
        POINT pt;
        ::GetCursorPos(&pt);
        ScreenToClient(&pt);
        if (splitterRect().PtInRect(pt))
        {
            ::SetCursor(::LoadCursorW(nullptr, IDC_SIZENS));
            return TRUE;
        }
    }
    SetMsgHandled(FALSE);
    return FALSE;
}
void CMainFrame::OnLButtonDown(UINT, CPoint pt)
{
    if (!m_cfg.hideLowerPane && splitterRect().PtInRect(pt))
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
    m_upperH = pt.y - toolbarHeight();
    OnSize(0, CSize());
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

// ---- columns --------------------------------------------------------
void CMainFrame::RebuildColumns()
{
    while (m_list.DeleteColumn(0))
    {
    }
    for (size_t k = 0; k < m_cfg.cols.visible.size(); ++k)
    {
        int c = m_cfg.cols.visible[k];
        if (c < 0 || c >= kColumnCount)
        {
            continue;
        }
        int w = k < m_cfg.cols.widths.size() ? m_cfg.cols.widths[k] : kColumns[c].width;
        m_list.InsertColumn((int)k, kColumns[c].title, kColumns[c].format, w);
    }
}
void CMainFrame::ApplyGridLines()
{
    DWORD ex = m_list.GetExtendedListViewStyle();
    ex = m_cfg.showGridLines ? (ex | LVS_EX_GRIDLINES) : (ex & ~LVS_EX_GRIDLINES);
    m_list.SetExtendedListViewStyle(ex);
}
void CMainFrame::ApplyAlwaysOnTop()
{
    SetWindowPos(m_cfg.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

// ---- data -----------------------------------------------------------
void CMainFrame::Rebuild()
{
    m_all = BuildConnections(m_packets, m_captureIp);
    ApplyFilterAndSort();
}

void CMainFrame::ApplyFilterAndSort()
{
    m_view.clear();
    for (const Connection& c : m_all)
    {
        if (c.protocol == PROTO_TCP && !m_cfg.displayTcp)
        {
            continue;
        }
        if (c.protocol == PROTO_UDP && !m_cfg.displayUdp)
        {
            continue;
        }
        if (c.protocol == PROTO_ICMP && !m_cfg.displayIcmp)
        {
            continue;
        }
        m_view.push_back(c);
    }
    SortView();
    m_list.SetItemCount((int)m_view.size());
    m_list.Invalidate();
    RenderLower();
    UpdateStatus();
}

void CMainFrame::SortView()
{
    int col = m_cfg.cols.sortColumn;
    bool asc = m_cfg.cols.sortAscending;
    DisplayOptions d = disp();
    std::stable_sort(m_view.begin(), m_view.end(),
                     [&](const Connection& a, const Connection& b)
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
                             c = _wcsicmp(a.column(col, d).c_str(), b.column(col, d).c_str());
                         }
                         return asc ? (c < 0) : (c > 0);
                     });
}

void CMainFrame::LoadPacketsFromFile(const std::wstring& path)
{
    std::vector<SsPacket> pkts;
    uint32_t capIp = 0;
    if (!LoadSspFile(path, pkts, capIp))
    {
        MessageBoxW(L"Failed to load the selected packets file.", L"SmartSniff", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    m_packets = std::move(pkts);
    m_captureIp = capIp;
    Rebuild();
}

// ---- lower pane rendering ------------------------------------------
void CMainFrame::RenderLower()
{
    int sel = m_list.GetNextItem(-1, LVNI_FOCUSED);
    if (sel < 0 || sel >= (int)m_view.size())
    {
        m_lower.SetWindowTextW(L"");
        return;
    }
    const std::vector<uint8_t>& data = m_view[sel].payload;

    std::wstring text;
    int mode = m_cfg.displayMode;
    if (mode == 1) // Automatic: hex when control bytes are present, otherwise ascii
    {
        bool binary = false;
        for (uint8_t b : data)
        {
            if (b < 0x20 && b != '\r' && b != '\n' && b != '\t')
            {
                binary = true;
                break;
            }
        }
        mode = binary ? 3 : 2;
    }

    if (mode == 4) // URL List
    {
        std::string s(data.begin(), data.end());
        for (const char* key : {"http://", "https://"})
        {
            size_t p = 0;
            while ((p = s.find(key, p)) != std::string::npos)
            {
                size_t e = p;
                while (e < s.size() && s[e] > 0x20 && s[e] != '"' && s[e] != '\'')
                {
                    ++e;
                }
                std::string url = s.substr(p, e - p);
                text.append(url.begin(), url.end());
                text += L"\r\n";
                p = e;
            }
        }
    }
    else if (mode == 2) // Ascii
    {
        for (uint8_t b : data)
        {
            if (b == '\r' || b == '\n' || b == '\t')
            {
                text += (wchar_t)b;
            }
            else if (b >= 0x20 && b < 0x7F)
            {
                text += (wchar_t)b;
            }
            else
            {
                text += L'.';
            }
        }
    }
    else // Hex dump
    {
        wchar_t line[128];
        for (size_t off = 0; off < data.size(); off += 16)
        {
            std::wstring hex, asc;
            for (size_t i = 0; i < 16; ++i)
            {
                if (off + i < data.size())
                {
                    uint8_t b = data[off + i];
                    swprintf(line, 128, L"%02X ", b);
                    hex += line;
                    asc += (b >= 0x20 && b < 0x7F) ? (wchar_t)b : L'.';
                }
                else
                {
                    hex += L"   ";
                }
            }
            swprintf(line, 128, L"%08X  ", (unsigned)off);
            text += line + hex + L" " + asc + L"\r\n";
        }
    }
    if (text.empty())
    {
        text = L"(no data in this conversation)";
    }
    m_lower.SetWindowTextW(text.c_str());
}

// ---- owner-data -----------------------------------------------------
LRESULT CMainFrame::OnGetDispInfo(int, LPNMHDR p, BOOL&)
{
    auto* di = reinterpret_cast<NMLVDISPINFOW*>(p);
    static std::wstring text;
    if (!(di->item.mask & LVIF_TEXT))
    {
        return 0;
    }
    int item = di->item.iItem;
    if (item < 0 || item >= (int)m_view.size())
    {
        return 0;
    }
    int col = (di->item.iSubItem < (int)m_cfg.cols.visible.size()) ? m_cfg.cols.visible[di->item.iSubItem] : 0;
    text = m_view[item].column(col, disp());
    di->item.pszText = const_cast<LPWSTR>(text.c_str());
    return 0;
}

LRESULT CMainFrame::OnColumnClick(int, LPNMHDR p, BOOL&)
{
    auto* nv = reinterpret_cast<LPNMLISTVIEW>(p);
    int d = nv->iSubItem;
    if (d < 0 || d >= (int)m_cfg.cols.visible.size())
    {
        return 0;
    }
    int clicked = m_cfg.cols.visible[d];
    if (clicked == m_cfg.cols.sortColumn)
    {
        m_cfg.cols.sortAscending = !m_cfg.cols.sortAscending;
    }
    else
    {
        m_cfg.cols.sortColumn = clicked;
        m_cfg.cols.sortAscending = true;
    }
    SortView();
    m_list.Invalidate();
    return 0;
}

LRESULT CMainFrame::OnItemChanged(int, LPNMHDR, BOOL&)
{
    RenderLower();
    UpdateStatus();
    return 0;
}

LRESULT CMainFrame::OnRClick(int, LPNMHDR, BOOL&)
{
    HMENU menu = ::LoadMenuW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCEW(IDR_CTX_MENU));
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

// ---- status ---------------------------------------------------------
void CMainFrame::UpdateStatus()
{
    wchar_t buf[128];
    swprintf(buf, 128, L"%d TCP/IP conversations", (int)m_view.size());
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
    int sel = m_list.GetSelectedCount();
    if (sel > 0)
    {
        swprintf(buf, 128, L"%d Selected", sel);
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, (LPARAM)buf);
    }
    else
    {
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 1, (LPARAM)L"");
    }
    swprintf(buf, 128, L"%d Packets", (int)m_packets.size());
    ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, 2, (LPARAM)buf);
}

// ---- export helpers -------------------------------------------------
std::vector<int> CMainFrame::selectedRows()
{
    std::vector<int> v;
    for (int i = m_list.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_list.GetNextItem(i, LVNI_SELECTED))
    {
        v.push_back(i);
    }
    return v;
}
ExportRequest CMainFrame::buildRequest()
{
    ExportRequest r;
    r.colDefs = kColumns;
    r.columns = m_cfg.cols.visible;
    r.reportTitle = L"Packets Summary";
    r.addHeaderLine = m_cfg.addExportHeaderLine;
    r.rowCount = (int)m_view.size();
    DisplayOptions d = disp();
    r.cell = [this, d](int row, int col)
    {
        return m_view[row].column(col, d);
    };
    return r;
}

// ---- commands -------------------------------------------------------
LRESULT CMainFrame::OnLoadPackets(WORD, WORD, HWND, BOOL&)
{
    CFileDialog dlg(TRUE, L"ssp", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"SmartSniff Packets File (*.ssp)\0*.ssp\0All Files (*.*)\0*.*\0", m_hWnd);
    if (dlg.DoModal() == IDOK)
    {
        LoadPacketsFromFile(dlg.m_szFileName);
    }
    return 0;
}
LRESULT CMainFrame::OnSavePackets(WORD, WORD, HWND, BOOL&)
{
    if (m_packets.empty())
    {
        MessageBoxW(L"There are no packets to save.", L"SmartSniff", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    CFileDialog dlg(FALSE, L"ssp", L"packets.ssp", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"SmartSniff Packets File (*.ssp)\0*.ssp\0", m_hWnd);
    if (dlg.DoModal() != IDOK)
    {
        return 0;
    }
    std::vector<uint8_t> bytes = BuildSspBytes(m_packets, m_captureIp);
    std::ofstream f(dlg.m_szFileName, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    return 0;
}

LRESULT CMainFrame::OnSaveSummaries(WORD, WORD, HWND, BOOL&)
{
    CFileDialog dlg(FALSE, L"txt", L"Packets.txt", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
                    L"Text File (*.txt)\0*.txt\0Tab Delimited Text File (*.txt)\0*.txt\0"
                    L"Tabular Text File (*.txt)\0*.txt\0HTML File - Horizontal (*.html)\0*.html\0"
                    L"HTML File - Vertical (*.html)\0*.html\0XML File (*.xml)\0*.xml\0"
                    L"Comma Delimited Text File (*.csv)\0*.csv\0",
                    m_hWnd);
    dlg.m_ofn.lpstrTitle = L"Select a filename to save";
    dlg.m_ofn.nFilterIndex = m_cfg.saveFilterIndex;
    if (dlg.DoModal() != IDOK)
    {
        return 0;
    }
    m_cfg.saveFilterIndex = dlg.m_ofn.nFilterIndex;
    WriteReport(dlg.m_szFileName, buildRequest(), FormatFromFilterIndex(dlg.m_ofn.nFilterIndex));
    return 0;
}

LRESULT CMainFrame::OnHtmlReport(WORD, WORD, HWND, BOOL&)
{
    wchar_t dir[MAX_PATH], tmp[MAX_PATH];
    ::GetTempPathW(MAX_PATH, dir);
    ::GetTempFileNameW(dir, L"sns", 0, tmp);
    std::wstring html = tmp;
    html += L".html";
    WriteReport(html, buildRequest(), ExportFormat::HtmlHorizontal);
    ::ShellExecuteW(m_hWnd, L"open", html.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return 0;
}

LRESULT CMainFrame::OnCopy(WORD, WORD, HWND, BOOL&)
{
    auto rows = selectedRows();
    if (rows.empty())
    {
        return 0;
    }
    ExportRequest base = buildRequest();
    base.addHeaderLine = false;
    ExportRequest r = base;
    r.rowCount = (int)rows.size();
    r.cell = [&](int row, int col)
    {
        return base.cell(rows[row], col);
    };
    std::string ansi = BuildReportA(r, ExportFormat::Tab);
    std::wstring wide;
    int n = ::MultiByteToWideChar(CP_ACP, 0, ansi.data(), (int)ansi.size(), nullptr, 0);
    wide.resize(n);
    ::MultiByteToWideChar(CP_ACP, 0, ansi.data(), (int)ansi.size(), wide.data(), n);
    CopyToClipboard(wide);
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

LRESULT CMainFrame::OnProperties(WORD, WORD, HWND, BOOL&)
{
    int i = m_list.GetNextItem(-1, LVNI_FOCUSED);
    if (i < 0 || i >= (int)m_view.size())
    {
        return 0;
    }
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    DisplayOptions d = disp();
    for (int c : m_cfg.cols.visible)
    {
        rows.emplace_back(kColumns[c].title, m_view[i].column(c, d));
    }
    CPropertiesDlg(std::move(rows)).DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnChooseColumns(WORD, WORD, HWND, BOOL&)
{
    CColumnsDlg dlg(kColumns, kColumnCount, m_cfg.cols);
    if (dlg.DoModal(m_hWnd) == IDOK)
    {
        m_cfg.cols = dlg.m_state;
        if (std::find(m_cfg.cols.visible.begin(), m_cfg.cols.visible.end(), m_cfg.cols.sortColumn) ==
                m_cfg.cols.visible.end() &&
            !m_cfg.cols.visible.empty())
        {
            m_cfg.cols.sortColumn = m_cfg.cols.visible.front();
        }
        RebuildColumns();
        m_list.Invalidate();
    }
    return 0;
}

void CMainFrame::AutoSize(int how)
{
    for (int i = 0; i < (int)m_cfg.cols.visible.size(); ++i)
    {
        m_list.SetColumnWidth(i, how);
    }
    for (size_t i = 0; i < m_cfg.cols.visible.size(); ++i)
    {
        if (i < m_cfg.cols.widths.size())
        {
            m_cfg.cols.widths[i] = m_list.GetColumnWidth((int)i);
        }
    }
}
LRESULT CMainFrame::OnAutoSize(WORD, WORD, HWND, BOOL&)
{
    AutoSize(LVSCW_AUTOSIZE);
    return 0;
}
LRESULT CMainFrame::OnAutoSizeHdr(WORD, WORD, HWND, BOOL&)
{
    AutoSize(LVSCW_AUTOSIZE_USEHEADER);
    return 0;
}

LRESULT CMainFrame::OnMoveNext(WORD, WORD, HWND, BOOL&)
{
    MoveSel(1);
    return 0;
}
LRESULT CMainFrame::OnMovePrev(WORD, WORD, HWND, BOOL&)
{
    MoveSel(-1);
    return 0;
}
void CMainFrame::MoveSel(int delta)
{
    int n = (int)m_view.size();
    if (n == 0)
    {
        return;
    }
    int cur = m_list.GetNextItem(-1, LVNI_FOCUSED);
    int next = std::clamp(cur + delta, 0, n - 1);
    m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    m_list.SetItemState(next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    m_list.EnsureVisible(next, FALSE);
}

LRESULT CMainFrame::OnGridLines(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showGridLines = !m_cfg.showGridLines;
    ApplyGridLines();
    SyncChecks();
    return 0;
}
LRESULT CMainFrame::OnTooltips(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showInfoTip = !m_cfg.showInfoTip;
    SyncChecks();
    return 0;
}
LRESULT CMainFrame::OnMarkOddEven(WORD, WORD, HWND, BOOL&)
{
    m_cfg.markOddEvenRows = !m_cfg.markOddEvenRows;
    m_list.Invalidate();
    SyncChecks();
    return 0;
}
LRESULT CMainFrame::OnOutgoingIncoming(WORD, WORD, HWND, BOOL&)
{
    m_cfg.showOutgoingIncoming = !m_cfg.showOutgoingIncoming;
    m_list.Invalidate();
    SyncChecks();
    return 0;
}
LRESULT CMainFrame::OnResolveIp(WORD, WORD, HWND, BOOL&)
{
    m_cfg.resolveIP = !m_cfg.resolveIP;
    SyncChecks();
    return 0;
}
LRESULT CMainFrame::OnAlwaysOnTop(WORD, WORD, HWND, BOOL&)
{
    m_cfg.alwaysOnTop = !m_cfg.alwaysOnTop;
    ApplyAlwaysOnTop();
    SyncChecks();
    return 0;
}
LRESULT CMainFrame::OnHideLower(WORD, WORD, HWND, BOOL&)
{
    m_cfg.hideLowerPane = !m_cfg.hideLowerPane;
    SyncChecks();
    OnSize(0, CSize());
    return 0;
}

LRESULT CMainFrame::OnDisplayMode(WORD, WORD id, HWND, BOOL&)
{
    m_cfg.displayMode = (id == ID_MODE_ASCII) ? 2 : (id == ID_MODE_HEX) ? 3 : (id == ID_MODE_URL) ? 4 : 1;
    SyncChecks();
    RenderLower();
    return 0;
}
LRESULT CMainFrame::OnProtoFilter(WORD, WORD id, HWND, BOOL&)
{
    if (id == ID_PROTO_TCP)
    {
        m_cfg.displayTcp = !m_cfg.displayTcp;
    }
    else if (id == ID_PROTO_UDP)
    {
        m_cfg.displayUdp = !m_cfg.displayUdp;
    }
    else if (id == ID_PROTO_ICMP)
    {
        m_cfg.displayIcmp = !m_cfg.displayIcmp;
    }
    SyncChecks();
    ApplyFilterAndSort();
    return 0;
}

LRESULT CMainFrame::OnCaptureOptions(WORD, WORD, HWND, BOOL&)
{
    CCaptureOptionsDlg(m_cfg, m_pcap && m_pcap->available).DoModal(m_hWnd);
    return 0;
}

LRESULT CMainFrame::OnStartCapture(WORD, WORD, HWND, BOOL&)
{
    // Live capture is intentionally not performed in this build. Report the backend
    // state the way the original would, then point at the file-load path.
    if (m_cfg.captureMethod == 2 && (!m_pcap || !m_pcap->available))
    {
        MessageBoxW(L"Failed to start capturing packets from the current network adapter.", L"SmartSniff",
                    MB_OK | MB_ICONEXCLAMATION);
        return 0;
    }
    MessageBoxW(L"Live capture is disabled in this build. Use File, Load Packets Data From File "
                L"to view captured packets from an .ssp file.",
                L"SmartSniff", MB_OK | MB_ICONINFORMATION);
    return 0;
}
LRESULT CMainFrame::OnStopCapture(WORD, WORD, HWND, BOOL&)
{
    return 0;
}

LRESULT CMainFrame::OnSaveConfig(WORD, WORD, HWND, BOOL&)
{
    CFileDialog dlg(FALSE, L"cfg", L"SmartSniff.cfg", OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                    L"SmartSniff Config File (*.cfg)\0*.cfg\0", m_hWnd);
    if (dlg.DoModal() != IDOK)
    {
        return 0;
    }
    Config c = m_cfg;
    c.path = dlg.m_szFileName;
    c.save();
    return 0;
}
LRESULT CMainFrame::OnLoadConfig(WORD, WORD, HWND, BOOL&)
{
    CFileDialog dlg(TRUE, L"cfg", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"SmartSniff Config File (*.cfg)\0*.cfg\0", m_hWnd);
    if (dlg.DoModal() != IDOK)
    {
        return 0;
    }
    Config c;
    c.path = dlg.m_szFileName;
    c.load();
    m_cfg = c;
    m_cfg.path = Config::DefaultPath();
    RebuildColumns();
    ApplyGridLines();
    SyncChecks();
    ApplyAlwaysOnTop();
    ApplyFilterAndSort();
    return 0;
}

LRESULT CMainFrame::OnCaptureFilter(WORD, WORD, HWND, BOOL&)
{
    EditFilter(m_cfg.captureFilter, L"Capture Filter");
    return 0;
}
LRESULT CMainFrame::OnDisplayFilter(WORD, WORD, HWND, BOOL&)
{
    EditFilter(m_cfg.displayFilter, L"Display Filter");
    ApplyFilterAndSort();
    return 0;
}
void CMainFrame::EditFilter(std::wstring& target, const wchar_t* title)
{
    // Minimal filter editor over the original's dialog template.
    struct FilterDlg : CDialogImpl<FilterDlg>
    {
        enum
        {
            IDD = IDD_FILTER
        };
        std::wstring& m_text;
        const wchar_t* m_title;
        FilterDlg(std::wstring& t, const wchar_t* ti)
            : m_text(t)
            , m_title(ti)
        {
        }
        BEGIN_MSG_MAP(FilterDlg)
        MSG_WM_INITDIALOG(OnInit)
        COMMAND_ID_HANDLER(IDOK, OnOk)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
        COMMAND_ID_HANDLER(IDC_FILTER_CLEAR, OnClear)
        END_MSG_MAP()
        BOOL OnInit(CWindow, LPARAM)
        {
            SetWindowTextW(m_title);
            SetDlgItemTextW(IDC_FILTER_EDIT, m_text.c_str());
            return TRUE;
        }
        LRESULT OnOk(WORD, WORD, HWND, BOOL&)
        {
            wchar_t buf[8192]{};
            GetDlgItemTextW(IDC_FILTER_EDIT, buf, 8192);
            m_text = buf;
            EndDialog(IDOK);
            return 0;
        }
        LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
        {
            EndDialog(IDCANCEL);
            return 0;
        }
        LRESULT OnClear(WORD, WORD, HWND, BOOL&)
        {
            SetDlgItemTextW(IDC_FILTER_EDIT, L"");
            return 0;
        }
    };
    FilterDlg(target, title).DoModal(m_hWnd);
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
    int n = (int)m_view.size();
    if (what.empty() || n == 0)
    {
        return;
    }
    std::wstring needle = what;
    std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
    DisplayOptions d = disp();
    int start = m_list.GetNextItem(-1, LVNI_FOCUSED);
    for (int step = 1; step <= n; ++step)
    {
        int i = down ? (start + step) % n : ((start - step) % n + n) % n;
        for (int c : m_cfg.cols.visible)
        {
            std::wstring v = m_view[i].column(c, d);
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

LRESULT CMainFrame::OnAbout(WORD, WORD, HWND, BOOL&)
{
    CAboutDlg().DoModal(m_hWnd);
    return 0;
}
LRESULT CMainFrame::OnExit(WORD, WORD, HWND, BOOL&)
{
    PostMessage(WM_CLOSE);
    return 0;
}

void CMainFrame::SyncChecks()
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
    chk(ID_GRIDLINES, m_cfg.showGridLines);
    chk(ID_TOOLTIPS, m_cfg.showInfoTip);
    chk(ID_MARK_ODDEVEN, m_cfg.markOddEvenRows);
    chk(ID_OUTGOING_INCOMING, m_cfg.showOutgoingIncoming);
    chk(ID_RESOLVE_IP, m_cfg.resolveIP);
    chk(ID_ALWAYS_ON_TOP, m_cfg.alwaysOnTop);
    chk(ID_HIDE_LOWER, m_cfg.hideLowerPane);
    chk(ID_PROTO_TCP, m_cfg.displayTcp);
    chk(ID_PROTO_UDP, m_cfg.displayUdp);
    chk(ID_PROTO_ICMP, m_cfg.displayIcmp);
    menu.CheckMenuRadioItem(ID_MODE_AUTO, ID_MODE_URL, ID_MODE_AUTO + (m_cfg.displayMode - 1), MF_BYCOMMAND);
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
    if (!m_noLoadSettings)
    {
        for (size_t i = 0; i < m_cfg.cols.visible.size(); ++i)
        {
            if (i < m_cfg.cols.widths.size())
            {
                m_cfg.cols.widths[i] = m_list.GetColumnWidth((int)i);
            }
        }
        m_cfg.splitLoc = m_upperH;
        m_cfg.placement.length = sizeof(m_cfg.placement);
        if (GetWindowPlacement(&m_cfg.placement))
        {
            m_cfg.havePlacement = true;
        }
        m_cfg.save();
    }
    if (m_lowerFont)
    {
        ::DeleteObject(m_lowerFont);
    }
    PostQuitMessage(0);
}
