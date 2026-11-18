// Dialogs.h -- the About box, the Connect dialog, the Search dialog and the
// read-only Attribute Properties view.
//
// About reuses the original template, enlarged to carry the recreation credit.
// Connect mirrors the original CONNECT template: a server or domain name, an
// optional user and password, and the save-and-snapshot controls kept for
// fidelity. Search runs a raw LDAP filter under a chosen scope. Attribute
// Properties shows one attribute's name, syntax, count and values.
#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <string>
#include <vector>
#include "resource.h"
#include "AdClient.h"

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
    NOTIFY_CODE_HANDLER(NM_CLICK, OnLink)
    NOTIFY_CODE_HANDLER(NM_RETURN, OnLink)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About Active Directory Explorer");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"Active Directory Explorer");
        SetDlgItemTextW(IDC_ABOUT_VERSION, L"v1.52");
        // A lone & is an accelerator prefix in a static control, so the
        // ampersand is doubled and renders as a single &.
        SetDlgItemTextW(IDC_ABOUT_DESC, L"Independent source-accurate recreation of AD Explorer by Sysinternals, "
                                        L"through reverse engineering and rewritten in modern C++20 && WTL 10.\r\n"
                                        L"Author: 0xntpower");
        return TRUE;
    }

    LRESULT OnLink(int, LPNMHDR p, BOOL&)
    {
        if (p->idFrom == IDC_ABOUT_LINK)
        {
            ::ShellExecuteW(m_hWnd, L"open", L"https://www.sysinternals.com", nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }
};

// ---- Connect -------------------------------------------------------------
class CConnectDlg : public CDialogImpl<CConnectDlg>
{
  public:
    enum
    {
        IDD = IDD_CONNECT
    };

    std::wstring m_server;
    std::wstring m_user;
    std::wstring m_password;

    BEGIN_MSG_MAP(CConnectDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnOK)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    COMMAND_ID_HANDLER(IDC_CONN_SAVE, OnToggleSave)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        CheckDlgButton(IDC_CONN_RADIO_AD, BST_CHECKED);
        SetDlgItemTextW(IDC_CONN_SERVER, m_server.c_str());
        SetDlgItemTextW(IDC_CONN_USER, m_user.c_str());
        ::SetFocus(GetDlgItem(IDC_CONN_SERVER));
        return FALSE;
    }

    LRESULT OnToggleSave(WORD, WORD, HWND, BOOL&)
    {
        const BOOL on = IsDlgButtonChecked(IDC_CONN_SAVE) == BST_CHECKED;
        ::EnableWindow(GetDlgItem(IDC_CONN_NAME), on);
        return 0;
    }

    LRESULT OnOK(WORD, WORD, HWND, BOOL&)
    {
        if (IsDlgButtonChecked(IDC_CONN_RADIO_SNAP) == BST_CHECKED)
        {
            ::MessageBoxW(m_hWnd,
                          L"Loading a saved snapshot is out of scope in this recreation.\r\n"
                          L"Select the Active Directory option to connect to a live directory.",
                          L"Active Directory Explorer", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        m_server = GetText(IDC_CONN_SERVER);
        m_user = GetText(IDC_CONN_USER);
        m_password = GetText(IDC_CONN_PASSWORD);
        EndDialog(IDOK);
        return 0;
    }

    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }

  private:
    std::wstring GetText(int id)
    {
        wchar_t buf[512] = {};
        GetDlgItemTextW(id, buf, static_cast<int>(std::size(buf)));
        return buf;
    }
};

// ---- Attribute Properties (read-only) ------------------------------------
class CAttrPropsDlg : public CDialogImpl<CAttrPropsDlg>
{
  public:
    enum
    {
        IDD = IDD_ATTR_PROPS
    };

    std::wstring m_object;
    ad::Attribute m_attr;

    BEGIN_MSG_MAP(CAttrPropsDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnClose)
    COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetDlgItemTextW(IDC_AP_ATTR, m_attr.name.c_str());
        SetDlgItemTextW(IDC_AP_OBJECT, m_object.c_str());
        SetDlgItemTextW(IDC_AP_SYNTAX, m_attr.syntax.c_str());
        wchar_t count[16] = {};
        _snwprintf_s(count, _TRUNCATE, L"%d", m_attr.count);
        SetDlgItemTextW(IDC_AP_SCHEMA, count);
        std::wstring values = m_attr.value;
        Replace(values, L"; ", L"\r\n");
        SetDlgItemTextW(IDC_AP_VALUES, values.c_str());
        return TRUE;
    }

    LRESULT OnClose(WORD, WORD id, HWND, BOOL&)
    {
        EndDialog(id);
        return 0;
    }

  private:
    static void Replace(std::wstring& s, const std::wstring& from, const std::wstring& to)
    {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::wstring::npos)
        {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
};

// ---- Search Container ----------------------------------------------------
class CSearchDlg : public CDialogImpl<CSearchDlg>
{
  public:
    enum
    {
        IDD = IDD_SEARCH
    };

    ad::Client* m_client = nullptr;
    std::wstring m_baseDn;
    std::wstring m_selectedDn; // the DN to navigate to, set on OK-navigate

    CListViewCtrl m_list;
    CComboBox m_scope;
    std::vector<ad::Child> m_results;

    BEGIN_MSG_MAP(CSearchDlg)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDOK, OnSearch)
    COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
    NOTIFY_HANDLER(IDC_SEARCH_LIST, NM_DBLCLK, OnDblClk)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        m_list.Attach(GetDlgItem(IDC_SEARCH_LIST));
        m_list.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
        m_list.InsertColumn(0, L"Distinguished Name", LVCFMT_LEFT, 360);
        m_list.InsertColumn(1, L"Class", LVCFMT_LEFT, 110);

        m_scope.Attach(GetDlgItem(IDC_SEARCH_SCOPE));
        m_scope.AddString(L"Subtree");
        m_scope.AddString(L"One Level");
        m_scope.AddString(L"Base");
        m_scope.SetCurSel(0);

        SetDlgItemTextW(IDC_SEARCH_BASE, m_baseDn.c_str());
        SetDlgItemTextW(IDC_SEARCH_FILTER, L"(objectClass=*)");
        return TRUE;
    }

    LRESULT OnSearch(WORD, WORD, HWND, BOOL&)
    {
        if (m_client == nullptr)
        {
            return 0;
        }
        const std::wstring base = GetText(IDC_SEARCH_BASE);
        const std::wstring filter = GetText(IDC_SEARCH_FILTER);
        ADS_SCOPEENUM scope = ADS_SCOPE_SUBTREE;
        switch (m_scope.GetCurSel())
        {
        case 1:
            scope = ADS_SCOPE_ONELEVEL;
            break;
        case 2:
            scope = ADS_SCOPE_BASE;
            break;
        default:
            scope = ADS_SCOPE_SUBTREE;
            break;
        }

        HCURSOR prev = ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));
        m_results = m_client->Search(base, filter, scope);
        ::SetCursor(prev);

        m_list.SetRedraw(FALSE);
        m_list.DeleteAllItems();
        for (int i = 0; i < static_cast<int>(m_results.size()); ++i)
        {
            m_list.InsertItem(i, m_results[i].dn.c_str());
            m_list.SetItemText(i, 1, m_results[i].objectClass.c_str());
        }
        m_list.SetRedraw(TRUE);

        wchar_t count[64] = {};
        _snwprintf_s(count, _TRUNCATE, L"%zu object(s) found", m_results.size());
        SetDlgItemTextW(IDC_SEARCH_COUNT, count);
        return 0;
    }

    LRESULT OnDblClk(int, LPNMHDR, BOOL&)
    {
        const int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
        if (sel >= 0 && sel < static_cast<int>(m_results.size()))
        {
            m_selectedDn = m_results[sel].dn;
            EndDialog(IDOK);
        }
        return 0;
    }

    LRESULT OnCancel(WORD, WORD, HWND, BOOL&)
    {
        EndDialog(IDCANCEL);
        return 0;
    }

  private:
    std::wstring GetText(int id)
    {
        wchar_t buf[1024] = {};
        GetDlgItemTextW(id, buf, static_cast<int>(std::size(buf)));
        return buf;
    }
};
