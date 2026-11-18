// Dialogs.h -- About, Capture Options, Properties and Column Settings, reusing the
// original dialog templates (112, 108, 105, 1096). The Properties and Column Settings
// dialogs are generic over the column table. The Capture Options dialog shows the four
// capture methods and the local adapters, and reports whether the WinPcap driver loaded.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <iphlpapi.h>
#include <string>
#include <vector>
#include <utility>
#include "resource.h"
#include "Columns.h"
#include "Config.h"
#include "PcapLoader.h"

#pragma comment(lib, "iphlpapi.lib")

// ---- About ---------------------------------------------------------------
class CAboutDlg : public CDialogImpl<CAboutDlg>
{
  public:
    enum
    {
        IDD = IDD_ABOUT
    };

    BEGIN_MSG_MAP(CAboutDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About SmartSniff");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"SmartSniff v2.30");
        SetDlgItemTextW(IDC_ABOUT_DESC,
                        L"Independent source-accurate recreation of SmartSniff by NirSoft, through reverse engineering "
                        L"and rewritten in modern C++20 && WTL 10.\r\nAuthor: 0xntpower");
        if (CWindow t1 = GetDlgItem(1005))
        {
            t1.ShowWindow(SW_HIDE);
        }
        if (CWindow t2 = GetDlgItem(1006))
        {
            t2.ShowWindow(SW_HIDE);
        }
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

// ---- Capture Options -----------------------------------------------------
class CCaptureOptionsDlg : public CDialogImpl<CCaptureOptionsDlg>
{
  public:
    enum
    {
        IDD = IDD_CAPTURE_OPTIONS
    };

    Config& m_cfg;
    bool m_pcapAvailable;
    CListViewCtrl m_adapters;

    CCaptureOptionsDlg(Config& cfg, bool pcapAvailable)
        : m_cfg(cfg)
        , m_pcapAvailable(pcapAvailable)
    {
    }

    BEGIN_MSG_MAP(CCaptureOptionsDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnOK)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Capture Options");

        int method = m_cfg.captureMethod;
        CheckRadioButton(IDC_CAP_RAW, IDC_CAP_NETMON3, IDC_CAP_RAW + (method >= 1 && method <= 4 ? method - 1 : 0));

        // The WinPcap method is only usable when the driver loaded. Report its state.
        if (CWindow w = GetDlgItem(IDC_CAP_WINPCAP))
        {
            w.SetWindowTextW(m_pcapAvailable ? L"WinPcap Packet Capture Driver"
                                             : L"WinPcap Packet Capture Driver (not installed)");
        }
        if (!m_pcapAvailable)
        {
            ::EnableWindow(GetDlgItem(IDC_CAP_WINPCAP), FALSE);
        }
        // The Network Monitor methods need their own drivers, not present in this build.
        ::EnableWindow(GetDlgItem(IDC_CAP_NETMON), FALSE);
        ::EnableWindow(GetDlgItem(IDC_CAP_NETMON3), FALSE);

        CheckDlgButton(IDC_CAP_PROMISC, m_cfg.promiscuousMode ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_CAP_FIREWALL, m_cfg.addToWindowsFirewall ? BST_CHECKED : BST_UNCHECKED);

        m_adapters.Attach(GetDlgItem(IDC_CAP_ADAPTERS));
        m_adapters.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT);
        m_adapters.InsertColumn(0, L"IP Address", LVCFMT_LEFT, 110);
        m_adapters.InsertColumn(1, L"Adapter Name", LVCFMT_LEFT, 170);
        m_adapters.InsertColumn(2, L"MAC Address", LVCFMT_LEFT, 110);
        PopulateAdapters();

        if (CWindow info = GetDlgItem(IDC_CAP_INFO))
        {
            info.SetWindowTextW(m_pcapAvailable
                                    ? L"WinPcap/Npcap driver detected."
                                    : L"No WinPcap/Npcap driver. Raw Sockets is the only available method.\r\n"
                                      L"Live capture is disabled in this build. Use Load Packets Data From File.");
        }
        return TRUE;
    }

    // Enumerate local adapters with GetAdaptersInfo. This is a read-only query, it does
    // not open any adapter or capture traffic.
    void PopulateAdapters()
    {
        ULONG size = 0;
        if (::GetAdaptersInfo(nullptr, &size) != ERROR_BUFFER_OVERFLOW || size == 0)
        {
            return;
        }
        std::vector<BYTE> buf(size);
        auto* info = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
        if (::GetAdaptersInfo(info, &size) != NO_ERROR)
        {
            return;
        }

        int row = 0;
        for (IP_ADAPTER_INFO* a = info; a; a = a->Next)
        {
            wchar_t ip[64]{}, name[256]{}, mac[32]{};
            ::MultiByteToWideChar(CP_ACP, 0, a->IpAddressList.IpAddress.String, -1, ip, 64);
            ::MultiByteToWideChar(CP_ACP, 0, a->Description, -1, name, 256);
            if (a->AddressLength == 6)
            {
                swprintf(mac, 32, L"%02X-%02X-%02X-%02X-%02X-%02X", a->Address[0], a->Address[1], a->Address[2],
                         a->Address[3], a->Address[4], a->Address[5]);
            }
            int i = m_adapters.InsertItem(row++, ip);
            m_adapters.SetItemText(i, 1, name);
            m_adapters.SetItemText(i, 2, mac);
        }
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        if (IsDlgButtonChecked(IDC_CAP_RAW))
        {
            m_cfg.captureMethod = 1;
        }
        else if (IsDlgButtonChecked(IDC_CAP_WINPCAP))
        {
            m_cfg.captureMethod = 2;
        }
        else if (IsDlgButtonChecked(IDC_CAP_NETMON))
        {
            m_cfg.captureMethod = 3;
        }
        else if (IsDlgButtonChecked(IDC_CAP_NETMON3))
        {
            m_cfg.captureMethod = 4;
        }
        m_cfg.promiscuousMode = IsDlgButtonChecked(IDC_CAP_PROMISC) != 0;
        m_cfg.addToWindowsFirewall = IsDlgButtonChecked(IDC_CAP_FIREWALL) != 0;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};

// ---- Properties (Property / Value list for one record) -------------------
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>
{
  public:
    enum
    {
        IDD = IDD_PROPERTIES
    };

    std::vector<std::pair<std::wstring, std::wstring>> m_rows;
    CListViewCtrl m_list;

    explicit CPropertiesDlg(std::vector<std::pair<std::wstring, std::wstring>> rows)
        : m_rows(std::move(rows))
    {
    }

    BEGIN_MSG_MAP(CPropertiesDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"Properties");
        if (CWindow lbl = GetDlgItem(IDC_PROP_LABEL))
        {
            lbl.ShowWindow(SW_HIDE);
        }
        if (CWindow edit = GetDlgItem(IDC_PROP_VALUE))
        {
            edit.ShowWindow(SW_HIDE);
        }

        CRect rc;
        GetClientRect(&rc);
        rc.DeflateRect(7, 7);
        rc.bottom -= 24;
        m_list.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE,
                      IDC_PROP_VALUE + 100);
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_list.InsertColumn(0, L"Property", LVCFMT_LEFT, 140);
        m_list.InsertColumn(1, L"Value", LVCFMT_LEFT, 300);
        for (const auto& [name, value] : m_rows)
        {
            int row = m_list.InsertItem(m_list.GetItemCount(), name.c_str());
            m_list.SetItemText(row, 1, value.c_str());
        }
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

// ---- Column Settings -----------------------------------------------------
class CColumnsDlg : public CDialogImpl<CColumnsDlg>
{
  public:
    enum
    {
        IDD = IDD_COLUMNS
    };

    const ColumnDef* m_defs;
    int m_count;
    ColumnState m_state;
    CListViewCtrl m_list;

    CColumnsDlg(const ColumnDef* defs, int count, ColumnState state)
        : m_defs(defs)
        , m_count(count)
        , m_state(std::move(state))
    {
    }

    BEGIN_MSG_MAP(CColumnsDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDC_COL_MOVEUP, OnMoveUp)
    COMMAND_ID_HANDLER(IDC_COL_MOVEDOWN, OnMoveDown)
    COMMAND_ID_HANDLER(IDC_COL_SHOW, OnShow)
    COMMAND_ID_HANDLER(IDC_COL_HIDE, OnHide)
    COMMAND_ID_HANDLER(IDC_COL_DEFAULT, OnDefault)
    COMMAND_ID_HANDLER(IDOK, OnOK)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_list.Attach(GetDlgItem(IDC_COL_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        m_list.InsertColumn(0, L"Column", LVCFMT_LEFT, 138);
        Populate();
        return TRUE;
    }

    void Populate()
    {
        m_list.DeleteAllItems();
        std::vector<bool> shown(m_count, false);
        int row = 0;
        for (int c : m_state.visible)
        {
            if (c < 0 || c >= m_count)
            {
                continue;
            }
            m_list.InsertItem(row, m_defs[c].title);
            m_list.SetItemData(row, c);
            m_list.SetCheckState(row, TRUE);
            shown[c] = true;
            ++row;
        }
        for (int c = 0; c < m_count; ++c)
        {
            if (!shown[c])
            {
                m_list.InsertItem(row, m_defs[c].title);
                m_list.SetItemData(row, c);
                m_list.SetCheckState(row, FALSE);
                ++row;
            }
        }
    }

    int sel()
    {
        return m_list.GetNextItem(-1, LVNI_SELECTED);
    }

    void swapRows(int a, int b)
    {
        wchar_t ta[256], tb[256];
        m_list.GetItemText(a, 0, ta, 256);
        m_list.GetItemText(b, 0, tb, 256);
        DWORD_PTR da = m_list.GetItemData(a), db = m_list.GetItemData(b);
        BOOL ca = m_list.GetCheckState(a), cb = m_list.GetCheckState(b);
        m_list.SetItemText(a, 0, tb);
        m_list.SetItemData(a, db);
        m_list.SetCheckState(a, cb);
        m_list.SetItemText(b, 0, ta);
        m_list.SetItemData(b, da);
        m_list.SetCheckState(b, ca);
    }

    LRESULT OnMoveUp(WORD, WORD, HWND, BOOL&)
    {
        int i = sel();
        if (i > 0)
        {
            swapRows(i, i - 1);
            m_list.SetItemState(i - 1, LVIS_SELECTED, LVIS_SELECTED);
        }
        return 0;
    }
    LRESULT OnMoveDown(WORD, WORD, HWND, BOOL&)
    {
        int i = sel();
        if (i >= 0 && i < m_list.GetItemCount() - 1)
        {
            swapRows(i, i + 1);
            m_list.SetItemState(i + 1, LVIS_SELECTED, LVIS_SELECTED);
        }
        return 0;
    }
    LRESULT OnShow(WORD, WORD, HWND, BOOL&)
    {
        if (int i = sel(); i >= 0)
        {
            m_list.SetCheckState(i, TRUE);
        }
        return 0;
    }
    LRESULT OnHide(WORD, WORD, HWND, BOOL&)
    {
        if (int i = sel(); i >= 0)
        {
            m_list.SetCheckState(i, FALSE);
        }
        return 0;
    }
    LRESULT OnDefault(WORD, WORD, HWND, BOOL&)
    {
        m_state.reset(m_defs, m_count);
        Populate();
        return 0;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        std::vector<int> vis, wid;
        for (int r = 0; r < m_list.GetItemCount(); ++r)
        {
            if (m_list.GetCheckState(r))
            {
                int c = (int)m_list.GetItemData(r);
                vis.push_back(c);
                wid.push_back(m_defs[c].width);
            }
        }
        if (!vis.empty())
        {
            m_state.visible = vis;
            m_state.widths = wid;
        }
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }
};
