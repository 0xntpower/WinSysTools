// OptionsBar.h -- the WMI options bar embedded at the top of the main window
// (the original's child dialog template 111): computer, namespace, class/query,
// synchronous/asynchronous mode and the Update button.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <string>
#include "resource.h"
#include "Config.h"

// The frame implements this so the bar can trigger a refresh.
struct IBarHost
{
    virtual void OnBarUpdate() = 0;
};

class COptionsBar : public CDialogImpl<COptionsBar>
{
  public:
    enum
    {
        IDD = IDD_WMIBAR
    };

    IBarHost* m_host = nullptr;

    BEGIN_MSG_MAP(COptionsBar)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_ID_HANDLER(IDC_BAR_UPDATE_BTN, OnUpdate)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        CComboBox computer = GetDlgItem(IDC_BAR_COMPUTER_COMBO);
        computer.AddString(L"This computer");
        computer.AddString(L"Connect another computer");

        CComboBox sync = GetDlgItem(IDC_BAR_SYNCMODE_COMBO);
        sync.AddString(L"Asynchronous Query");
        sync.AddString(L"Synchronous Query");
        return TRUE;
    }

    void FromConfig(const Config& c)
    {
        CComboBox(GetDlgItem(IDC_BAR_COMPUTER_COMBO)).SetCurSel(c.connectComputer == 2 ? 1 : 0);
        CComboBox(GetDlgItem(IDC_BAR_SYNCMODE_COMBO)).SetCurSel(c.syncMode == 2 ? 1 : 0);
        SetDlgItemTextW(IDC_BAR_NAMESPACE_COMBO, c.nameSpace.c_str());
        SetDlgItemTextW(IDC_BAR_CLASS_COMBO, c.classOrQuery.c_str());
        SetDlgItemTextW(IDC_BAR_COMPUTER_EDIT, c.computerName.c_str());
        SetDlgItemTextW(IDC_BAR_USERNAME_EDIT, c.userName.c_str());
        SetDlgItemInt(IDC_BAR_TIMEOUT_EDIT, c.timeout, FALSE);
    }

    void ToConfig(Config& c)
    {
        c.connectComputer = CComboBox(GetDlgItem(IDC_BAR_COMPUTER_COMBO)).GetCurSel() == 1 ? 2 : 1;
        c.syncMode = CComboBox(GetDlgItem(IDC_BAR_SYNCMODE_COMBO)).GetCurSel() == 1 ? 2 : 1;
        c.nameSpace = getText(IDC_BAR_NAMESPACE_COMBO);
        c.classOrQuery = getText(IDC_BAR_CLASS_COMBO);
        c.computerName = getText(IDC_BAR_COMPUTER_EDIT);
        c.userName = getText(IDC_BAR_USERNAME_EDIT);
        BOOL ok = FALSE;
        UINT t = GetDlgItemInt(IDC_BAR_TIMEOUT_EDIT, &ok, FALSE);
        if (ok)
        {
            c.timeout = (int)t;
        }
    }

    std::wstring getPassword()
    {
        return getText(IDC_BAR_PASSWORD_EDIT);
    }

    LRESULT OnUpdate(WORD, WORD, HWND, BOOL&)
    {
        if (m_host)
        {
            m_host->OnBarUpdate();
        }
        return 0;
    }

  private:
    std::wstring getText(int id)
    {
        wchar_t buf[4096]{};
        GetDlgItemTextW(id, buf, 4096);
        return buf;
    }
};
