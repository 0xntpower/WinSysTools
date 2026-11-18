// Dialogs.h -- the About box and the object Properties sheet.
//
// About (dialog 100) reuses the original template, enlarged to carry the
// recreation credit. Properties builds a property sheet with two pages that
// match the original: a Details page from dialog 107 filled through
// NtQueryObject, and a standard Security page from ACLUI's CreateSecurityPage.
#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <prsht.h>
#include <string>
#include <cstdio>
#include "resource.h"
#include "NtObjMgr.h"
#include "Security.h"

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
    NOTIFY_CODE_HANDLER(NM_CLICK, OnLinkClick)
    NOTIFY_CODE_HANDLER(NM_RETURN, OnLinkClick)
    END_MSG_MAP()

    BOOL OnInitDialog(CWindow, LPARAM)
    {
        SetWindowTextW(L"About WinObj");
        SetDlgItemTextW(IDC_ABOUT_NAME, L"WinObj v3.14");
        // A lone & is an accelerator prefix in a static control, so the ampersand
        // is written doubled and renders as a single &.
        SetDlgItemTextW(IDC_ABOUT_DESC, L"Independent source-accurate recreation of WinObj by Sysinternals, "
                                        L"through reverse engineering and rewritten in modern C++20 && WTL 10.\r\n"
                                        L"Author: 0xntpower");
        return TRUE;
    }

    LRESULT OnLinkClick(int, LPNMHDR p, BOOL&)
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

// ---- Properties ----------------------------------------------------------
namespace detail
{
    struct DetailsCtx
    {
        std::wstring path;
        std::wstring type;
        nt::ObjDetails det;
    };

    inline void setNum(HWND dlg, int id, bool have, unsigned long v)
    {
        wchar_t buf[32] = L"";
        if (have)
        {
            swprintf(buf, 32, L"%lu", v);
        }
        ::SetDlgItemTextW(dlg, id, buf);
    }

    inline INT_PTR CALLBACK DetailsProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_INITDIALOG)
        {
            auto* psp = reinterpret_cast<PROPSHEETPAGE*>(lParam);
            auto* ctx = reinterpret_cast<DetailsCtx*>(psp->lParam);
            ::SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

            ::SetDlgItemTextW(dlg, IDC_DET_NAME, ctx->path.c_str());
            ::SetDlgItemTextW(dlg, IDC_DET_TYPE, ctx->type.c_str());
            ::CheckDlgButton(dlg, IDC_DET_PERMANENT, ctx->det.permanent ? BST_CHECKED : BST_UNCHECKED);

            setNum(dlg, IDC_DET_REFS, ctx->det.haveBasic, ctx->det.pointerCount);
            setNum(dlg, IDC_DET_HANDLES, ctx->det.haveBasic, ctx->det.handleCount);
            setNum(dlg, IDC_DET_PAGED, ctx->det.haveBasic, ctx->det.pagedQuota);
            setNum(dlg, IDC_DET_NONPAGED, ctx->det.haveBasic, ctx->det.nonPagedQuota);

            if (ctx->det.isSymlink)
            {
                ::SetDlgItemTextW(dlg, IDC_DET_EXTRAGROUP, L"Symbolic Link");
                ::SetDlgItemTextW(dlg, 102, L"Target:");
                ::SetDlgItemTextW(dlg, IDC_DET_X1VAL, ctx->det.symlinkTarget.c_str());
            }
            else
            {
                for (int id : {IDC_DET_EXTRAGROUP, 102, IDC_DET_X1VAL})
                {
                    if (HWND c = ::GetDlgItem(dlg, id))
                    {
                        ::ShowWindow(c, SW_HIDE);
                    }
                }
            }
            return TRUE;
        }
        return FALSE;
    }
} // namespace detail

// Open the two-page Properties sheet for one object. Returns immediately when
// the object has no path. The Security page is added only when a handle to the
// object could be opened, matching the original.
inline void ShowObjectProperties(HWND parent, const std::wstring& path, const std::wstring& type)
{
    if (path.empty())
    {
        return;
    }

    detail::DetailsCtx ctx;
    ctx.path = path;
    ctx.type = type;
    ctx.det = nt::QueryDetails(path, type);

    HPROPSHEETPAGE pages[2] = {};
    int count = 0;

    PROPSHEETPAGE psp{};
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_DEFAULT;
    psp.hInstance = ModuleHelper::GetResourceInstance();
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_DETAILS);
    psp.pfnDlgProc = detail::DetailsProc;
    psp.lParam = reinterpret_cast<LPARAM>(&ctx);
    pages[count] = ::CreatePropertySheetPageW(&psp);
    if (pages[count])
    {
        ++count;
    }

    CObjectSecurity* sec = new CObjectSecurity(path, type);
    if (sec->HasHandle())
    {
        if (HPROPSHEETPAGE sp = ::CreateSecurityPage(sec))
        {
            pages[count++] = sp;
        }
    }
    sec->Release(); // the security page holds its own reference

    std::wstring caption = path;
    size_t slash = caption.rfind(L'\\');
    if (slash != std::wstring::npos)
    {
        caption = caption.substr(slash + 1);
    }
    caption += L" Properties";

    PROPSHEETHEADERW psh{};
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_DEFAULT | PSH_NOAPPLYNOW;
    psh.hwndParent = parent;
    psh.hInstance = ModuleHelper::GetResourceInstance();
    psh.pszCaption = caption.c_str();
    psh.nPages = count;
    psh.phpage = pages;
    ::PropertySheetW(&psh);
}
