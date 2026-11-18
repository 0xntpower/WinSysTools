// Dialogs.h -- Search For Icons (105), Save Selected Icons (107), Properties (108)
// and About (112), bound to the original extracted dialog templates for visual
// parity. Logic is clean C++ over the recovered models.
#pragma once
#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlcrack.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include "resource.h"
#include "IconEntry.h"
#include "ResourceScan.h"

// ------------------------------------------------------- Search For Icons (105)
class CSearchDlg : public CDialogImpl<CSearchDlg>
{
public:
    enum { IDD = IDD_SEARCH };

    std::wstring& m_wildcard;
    ScanOptions&  m_opt;
    int&          m_scanMode;     // 1 = files, 2 = process
    DWORD&        m_pid;
    std::vector<DWORD> m_pids;

    CSearchDlg(std::wstring& wildcard, ScanOptions& opt, int& scanMode, DWORD& pid)
        : m_wildcard(wildcard), m_opt(opt), m_scanMode(scanMode), m_pid(pid) {}

    BEGIN_MSG_MAP(CSearchDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnOk)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
        COMMAND_ID_HANDLER(IDC_SRCH_BROWSEFILE, OnBrowseFile)
        COMMAND_ID_HANDLER(IDC_SRCH_BROWSEFOLD, OnBrowseFolder)
    END_MSG_MAP()

    void FillSizeCombo(int id, int value)
    {
        CComboBox c(GetDlgItem(id));
        for (int s : { 16, 20, 24, 32, 40, 48, 64, 96, 128, 256 })
            c.AddString(std::to_wstring(s).c_str());
        c.SetWindowTextW(std::to_wstring(value).c_str());
    }

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"Search For Icons");
        CComboBox wild(GetDlgItem(IDC_SRCH_WILDCARD));
        wild.AddString(m_wildcard.c_str());
        wild.SetWindowTextW(m_wildcard.c_str());

        CheckDlgButton(IDC_SRCH_ICONS, m_opt.icons);
        CheckDlgButton(IDC_SRCH_CURSORS, m_opt.cursors);
        CheckDlgButton(IDC_SRCH_SUBDIRS, m_opt.subdirs);
        CheckDlgButton(IDC_SRCH_SIZEFILTER, m_opt.filterSize);
        CheckDlgButton(IDC_SRCH_COLORFILTER, m_opt.filterColor);
        FillSizeCombo(IDC_SRCH_WIDTH, m_opt.width);
        FillSizeCombo(IDC_SRCH_HEIGHT, m_opt.height);

        CComboBox colors(GetDlgItem(IDC_SRCH_COLORS));
        for (auto* s : { L"16 colors", L"256 colors", L"24-bit color", L"32-bit color" })
            colors.AddString(s);
        colors.SetCurSel(ColorIndexFromBits(m_opt.colorBits));

        CheckRadioButton(IDC_SRCH_RADIO_FILES, IDC_SRCH_RADIO_PROC,
                         m_scanMode == 2 ? IDC_SRCH_RADIO_PROC : IDC_SRCH_RADIO_FILES);
        FillProcessList();
        CenterWindow(GetParent());
        return TRUE;
    }

    static int ColorIndexFromBits(int bits)
    {
        switch (bits) { case 4: return 0; case 8: return 1; case 24: return 2; case 32: return 3; }
        return 1;
    }
    static int BitsFromColorIndex(int i)
    {
        static const int bits[] = { 4, 8, 24, 32 };
        return (i >= 0 && i < 4) ? bits[i] : 8;
    }

    void FillProcessList()
    {
        CListBox lb(GetDlgItem(IDC_SRCH_PROCLIST));
        m_pids.clear();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe))
            do
            {
                wchar_t line[320];
                wsprintfW(line, L"%u  -  %s", pe.th32ProcessID, pe.szExeFile);
                lb.AddString(line);
                m_pids.push_back(pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        CloseHandle(snap);
    }

    LRESULT OnBrowseFile(WORD, WORD, HWND, BOOL&)
    {
        CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
            L"Executable Files\0*.exe;*.dll;*.ocx;*.cpl\0All Files\0*.*\0", m_hWnd);
        dlg.m_ofn.lpstrTitle = L"Select a file for icons extraction";
        if (dlg.DoModal() == IDOK)
            ::SetDlgItemTextW(m_hWnd, IDC_SRCH_WILDCARD, dlg.m_szFileName);
        return 0;
    }

    LRESULT OnBrowseFolder(WORD, WORD, HWND, BOOL&)
    {
        wchar_t display[MAX_PATH];
        BROWSEINFOW bi{};
        bi.hwndOwner = m_hWnd;
        bi.pszDisplayName = display;
        bi.lpszTitle = L"Select a folder for icons scanning";
        bi.ulFlags = BIF_RETURNONLYFSDIRS;
        if (LPITEMIDLIST pidl = SHBrowseForFolderW(&bi))
        {
            wchar_t folder[MAX_PATH];
            if (SHGetPathFromIDListW(pidl, folder))
            {
                std::wstring w = folder;
                if (!w.empty() && w.back() != L'\\') w += L"\\";
                w += L"*.*";
                ::SetDlgItemTextW(m_hWnd, IDC_SRCH_WILDCARD, w.c_str());
            }
            CoTaskMemFree(pidl);
        }
        return 0;
    }

    static int ComboInt(HWND h, int def)
    {
        wchar_t buf[32]{};
        ::GetWindowTextW(h, buf, 32);
        int v = _wtoi(buf);
        return v > 0 ? v : def;
    }

    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        wchar_t buf[1024]{};
        ::GetDlgItemTextW(m_hWnd, IDC_SRCH_WILDCARD, buf, 1024);
        m_wildcard = buf;

        m_opt.icons = IsDlgButtonChecked(IDC_SRCH_ICONS) != 0;
        m_opt.cursors = IsDlgButtonChecked(IDC_SRCH_CURSORS) != 0;
        m_opt.subdirs = IsDlgButtonChecked(IDC_SRCH_SUBDIRS) != 0;
        m_opt.filterSize = IsDlgButtonChecked(IDC_SRCH_SIZEFILTER) != 0;
        m_opt.filterColor = IsDlgButtonChecked(IDC_SRCH_COLORFILTER) != 0;
        m_opt.width = ComboInt(GetDlgItem(IDC_SRCH_WIDTH), 32);
        m_opt.height = ComboInt(GetDlgItem(IDC_SRCH_HEIGHT), 32);
        m_opt.colorBits = BitsFromColorIndex(CComboBox(GetDlgItem(IDC_SRCH_COLORS)).GetCurSel());

        m_scanMode = IsDlgButtonChecked(IDC_SRCH_RADIO_PROC) ? 2 : 1;
        if (m_scanMode == 2)
        {
            int sel = CListBox(GetDlgItem(IDC_SRCH_PROCLIST)).GetCurSel();
            m_pid = (sel >= 0 && sel < (int)m_pids.size()) ? m_pids[sel] : 0;
        }
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ------------------------------------------------------ Save Selected Icons (107)
class CSaveDlg : public CDialogImpl<CSaveDlg>
{
public:
    enum { IDD = IDD_SAVE };

    std::wstring& m_folder;
    bool&         m_asIco;

    CSaveDlg(std::wstring& folder, bool& asIco) : m_folder(folder), m_asIco(asIco) {}

    BEGIN_MSG_MAP(CSaveDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnOk)
        COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
        COMMAND_ID_HANDLER(IDC_SAVE_BROWSE, OnBrowse)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"Save Selected Icons");
        SetDlgItemTextW(IDC_SAVE_FOLDER, m_folder.c_str());
        CheckDlgButton(IDC_SAVE_ASICO, m_asIco);
        CenterWindow(GetParent());
        return TRUE;
    }

    LRESULT OnBrowse(WORD, WORD, HWND, BOOL&)
    {
        wchar_t display[MAX_PATH];
        BROWSEINFOW bi{};
        bi.hwndOwner = m_hWnd;
        bi.pszDisplayName = display;
        bi.lpszTitle = L"Select a folder for saving all selected icons:";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        if (LPITEMIDLIST pidl = SHBrowseForFolderW(&bi))
        {
            wchar_t folder[MAX_PATH];
            if (SHGetPathFromIDListW(pidl, folder)) SetDlgItemTextW(IDC_SAVE_FOLDER, folder);
            CoTaskMemFree(pidl);
        }
        return 0;
    }

    LRESULT OnOk(WORD, WORD, HWND, BOOL&)
    {
        wchar_t buf[MAX_PATH]{};
        GetDlgItemTextW(IDC_SAVE_FOLDER, buf, MAX_PATH);
        m_folder = buf;
        m_asIco = IsDlgButtonChecked(IDC_SAVE_ASICO) != 0;
        EndDialog(IDOK);
        return 0;
    }
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&) { EndDialog(IDCANCEL); return 0; }
};

// ------------------------------------------------------------- Properties (108)
// Lists every image in the selected icon/cursor group with its dimension, colour
// depth and size, mirroring the original's per-image detail list.
class CPropertiesDlg : public CDialogImpl<CPropertiesDlg>
{
public:
    enum { IDD = IDD_PROPERTIES };

    IconEntry  m_item;
    CListViewCtrl m_images;
    CStatic    m_preview;
    HICON      m_previewIcon = nullptr;

    explicit CPropertiesDlg(const IconEntry& item) : m_item(item) {}

    BEGIN_MSG_MAP(CPropertiesDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_DESTROY(OnDestroy)
        COMMAND_ID_HANDLER(IDOK, OnClose)
        COMMAND_ID_HANDLER(IDCANCEL, OnClose)
        COMMAND_ID_HANDLER(IDC_PROP_COPYIMAGE, OnCopyImage)
    END_MSG_MAP()

    template <class T> static T rd(const BYTE* p) { T v; memcpy(&v, p, sizeof(T)); return v; }

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW((m_item.resourceField() + L" Properties").c_str());
        SetDlgItemTextW(IDC_PROP_FILENAME, m_item.filename.c_str());
        SetDlgItemTextW(IDC_PROP_ICONID,
            m_item.resName.empty() ? std::to_wstring(m_item.resId).c_str() : m_item.resName.c_str());
        SetDlgItemTextW(IDC_PROP_SIZE, (IconEntry::groupedNumber(m_item.totalSize) + L" bytes").c_str());
        SetDlgItemTextW(IDC_PROP_TYPE, m_item.typeField().c_str());

        m_images.Attach(GetDlgItem(IDC_PROP_IMAGES));
        m_images.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        m_images.InsertColumn(0, L"ID", LVCFMT_LEFT, 50);
        m_images.InsertColumn(1, L"Dimension", LVCFMT_LEFT, 80);
        m_images.InsertColumn(2, L"Colors", LVCFMT_LEFT, 70);
        m_images.InsertColumn(3, L"Size (Bytes)", LVCFMT_LEFT, 80);
        FillImages();

        // Show a 32x32 preview inside the "Selected Image" group box (1011).
        m_previewIcon = ExtractSelectedIcon();
        if (m_previewIcon)
        {
            RECT rc{};
            ::GetWindowRect(GetDlgItem(IDC_PROP_PREVIEW), &rc);
            ::MapWindowPoints(nullptr, m_hWnd, reinterpret_cast<LPPOINT>(&rc), 2);
            m_preview.Create(m_hWnd, CRect(rc.left + 40, rc.top + 30, rc.left + 72, rc.top + 62),
                             nullptr, WS_CHILD | WS_VISIBLE | SS_ICON);
            m_preview.SetIcon(m_previewIcon);
        }
        CenterWindow(GetParent());
        return TRUE;
    }

    void FillImages()
    {
        HMODULE hModule = LoadLibraryExW(m_item.filename.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (!hModule) return;
        const wchar_t* grpType = m_item.isCursor ? RT_GROUP_CURSOR : RT_GROUP_ICON;
        if (HRSRC hGrp = FindResourceW(hModule, m_item.resourceId(), grpType))
        {
            const BYTE* grp = static_cast<const BYTE*>(LockResource(LoadResource(hModule, hGrp)));
            if (grp)
            {
                WORD count = rd<WORD>(grp + 4);
                const BYTE* e = grp + 6;
                for (WORD i = 0; i < count; ++i, e += 14)
                {
                    int w = e[0] ? e[0] : 256;
                    int h = m_item.isCursor ? (rd<WORD>(e + 2)) : (e[1] ? e[1] : 256);
                    int bits = rd<WORD>(e + 6);
                    DWORD size = rd<DWORD>(e + 8);
                    WORD id = rd<WORD>(e + 12);
                    int row = m_images.InsertItem(i, std::to_wstring(id).c_str());
                    wchar_t dim[32]; wsprintfW(dim, L"%d X %d", w, h);
                    m_images.SetItemText(row, 1, dim);
                    wchar_t col[32]; wsprintfW(col, L"%d Bits", bits);
                    m_images.SetItemText(row, 2, col);
                    m_images.SetItemText(row, 3, IconEntry::groupedNumber(size).c_str());
                }
                if (count) m_images.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
        }
        FreeLibrary(hModule);
    }

    LRESULT OnCopyImage(WORD, WORD, HWND, BOOL&)
    {
        // Copy the selected image as an icon-backed bitmap onto the clipboard.
        HICON hIcon = ExtractSelectedIcon();
        if (!hIcon) return 0;
        ICONINFO ii{};
        if (::GetIconInfo(hIcon, &ii) && ::OpenClipboard(m_hWnd))
        {
            ::EmptyClipboard();
            ::SetClipboardData(CF_BITMAP, ii.hbmColor);
            ::CloseClipboard();
            if (ii.hbmMask) ::DeleteObject(ii.hbmMask);
        }
        ::DestroyIcon(hIcon);
        return 0;
    }

    HICON ExtractSelectedIcon()
    {
        HMODULE hModule = LoadLibraryExW(m_item.filename.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (!hModule) return nullptr;
        HICON hIcon = ExtractGroupIcon(hModule, m_item.resourceId(), m_item.isCursor, 32, 32);
        FreeLibrary(hModule);
        return hIcon;
    }

    void OnDestroy() { if (m_previewIcon) DestroyIcon(m_previewIcon); }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};

// ----------------------------------------------------------------- About (112)
class CAboutDlg : public CDialogImpl<CAboutDlg>
{
public:
    enum { IDD = IDD_ABOUT };

    BEGIN_MSG_MAP(CAboutDlg)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_ID_HANDLER(IDOK, OnClose)
        COMMAND_ID_HANDLER(IDCANCEL, OnClose)
    END_MSG_MAP()

    BOOL OnInitDialog(HWND, LPARAM)
    {
        SetWindowTextW(L"About IconsExtract");
        SetDlgItemTextW(IDC_ABOUT_TITLE, L"IconsExtract v1.47");
        SetDlgItemTextW(IDC_ABOUT_COPYRIGHT, L"Copyright (c) 2003 - 2010 Nir Sofer");
        CenterWindow(GetParent());
        return TRUE;
    }
    LRESULT OnClose(WORD, WORD id, HWND, BOOL&) { EndDialog(id); return 0; }
};
