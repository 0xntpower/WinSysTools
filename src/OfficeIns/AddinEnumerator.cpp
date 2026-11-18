// AddinEnumerator.cpp -- reconstructs OfficeIns's add-in scan.
//
// Mirrors, in order:
//   sub_140002FAC  the driver: 8 Office apps x 6 registry layouts, HKCU then HKLM
//   sub_140002F1C  per (app, layout) it formats the path and scans HKCU then HKLM
//   sub_140002C8C  per Addins key: enumerate ProgID subkeys, read Description /
//                  FriendlyName and LoadBehavior
//   sub_140002874  resolve the ProgID's CLSID -> InprocServer32 / LocalServer32 file,
//                  detect managed (mscoree.dll) add-ins and their RuntimeVersion
//   sub_1400026B8  read the server file's version info and creation time
//   sub_140002580  the LoadBehavior read-modify-write used by Start Mode / Connected
#include "Addin.h"
#include "RegKey.h"
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "version.lib")

namespace
{
    // The six registry layouts scanned per application (sub_140002FAC). %s = app name.
    const wchar_t* const kLayouts[] = {
        L"Software\\Microsoft\\office\\%s\\Addins",
        L"Software\\Microsoft\\office\\8.0\\%s\\Addins",
        L"Software\\Microsoft\\office\\9.0\\%s\\Addins",
        L"Software\\Microsoft\\office\\10.0\\%s\\Addins",
        L"Software\\Microsoft\\office\\11.0\\%s\\Addins",
        L"Software\\Microsoft\\%s\\Addins",
    };

    std::wstring format1(const wchar_t* fmt, const std::wstring& arg)
    {
        wchar_t buf[512];
        _snwprintf_s(buf, _TRUNCATE, fmt, arg.c_str());
        return buf;
    }

    std::wstring baseName(const std::wstring& path)
    {
        size_t p = path.find_last_of(L"\\/");
        return (p == std::wstring::npos) ? path : path.substr(p + 1);
    }

    // Open the predefined hive on a remote machine, or return the local predefined
    // handle when no remote computer is set (sub_140002FAC's RegConnectRegistryA path).
    HKEY connectHive(HKEY predefined, const std::wstring& remote)
    {
        if (remote.empty()) return predefined;
        HKEY h = nullptr;
        if (::RegConnectRegistryW(remote.c_str(), predefined, &h) == ERROR_SUCCESS) return h;
        return nullptr;
    }

    // Translate a local server path into the remote admin-share form the original
    // builds for version-info queries (sub_1400026B8: "\\comp\C$\rest").
    std::wstring remoteFilePath(const std::wstring& local, const std::wstring& remote)
    {
        if (remote.empty() || local.size() < 2 || local[1] != L':') return local;
        return L"\\\\" + remote + L"\\" + local.substr(0, 1) + L"$" + local.substr(2);
    }

    std::wstring verString(const unsigned char* block, const std::wstring& langcp, const wchar_t* field)
    {
        std::wstring sub = L"\\StringFileInfo\\" + langcp + L"\\" + field;
        LPVOID p = nullptr; UINT len = 0;
        if (::VerQueryValueW(block, sub.c_str(), &p, &len) && p && len)
            return std::wstring(static_cast<wchar_t*>(p), len > 0 ? len - 1 : 0);
        return {};
    }

    // sub_1400026B8 / sub_14000E068: pull ProductName, FileVersion, CompanyName and
    // FileDescription from the server file, plus its creation time (local).
    void readVersionInfo(Addin& a, const std::wstring& remote)
    {
        if (a.fileName.empty()) return;
        std::wstring path = remoteFilePath(a.fileName, remote);
        if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;

        DWORD handle = 0;
        DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
        if (size)
        {
            std::vector<unsigned char> block(size);
            if (::GetFileVersionInfoW(path.c_str(), 0, size, block.data()))
            {
                std::wstring langcp = L"040904E4";
                struct { WORD lang, cp; } *tr = nullptr; UINT trLen = 0;
                if (::VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation",
                                     reinterpret_cast<LPVOID*>(&tr), &trLen) && tr && trLen >= 4)
                {
                    wchar_t b[16];
                    _snwprintf_s(b, _TRUNCATE, L"%04X%04X", tr->lang, tr->cp);
                    langcp = b;
                }
                a.productName     = verString(block.data(), langcp, L"ProductName");
                a.fileVersion     = verString(block.data(), langcp, L"FileVersion");
                a.companyName     = verString(block.data(), langcp, L"CompanyName");
                a.fileDescription = verString(block.data(), langcp, L"FileDescription");
            }
        }

        HANDLE f = ::CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (f != INVALID_HANDLE_VALUE)
        {
            FILETIME created{};
            if (::GetFileTime(f, &created, nullptr, nullptr))
            {
                ::FileTimeToLocalFileTime(&created, &a.createdTime);
                a.hasFile = true;
            }
            ::CloseHandle(f);
        }
    }

    // sub_140002874: resolve the add-in's ProgID to its CLSID and server file.
    void resolveClsid(Addin& a, HKEY hcr, const std::wstring& remote)
    {
        if (!hcr) return;

        RegKey clsidKey;
        if (clsidKey.open(hcr, a.progId + L"\\CLSID"))
            a.clsid = clsidKey.queryDefault();
        if (a.clsid.empty()) return;   // minimal record, as the original returns here

        RegKey cls;
        if (!cls.open(hcr, L"CLSID\\" + a.clsid)) return;

        bool localServer = false;
        RegKey server;
        if (!server.open(cls.get(), L"InprocServer32"))
        {
            if (!server.open(cls.get(), L"LocalServer32")) { readVersionInfo(a, remote); return; }
            localServer = true;
        }

        std::wstring path = server.queryDefault();
        if (!path.empty() && path.front() == L'"')
        {
            size_t end = path.find(L'"', 1);
            path = (end == std::wstring::npos) ? path.substr(1) : path.substr(1, end - 1);
        }
        else if (localServer)
        {
            size_t dot = path.find_last_of(L'.');
            if (dot != std::wstring::npos)
            {
                size_t sp = path.find(L' ', dot);
                if (sp != std::wstring::npos) path.resize(sp);
            }
        }
        a.fileName = path;

        if (_wcsicmp(baseName(a.fileName).c_str(), L"mscoree.dll") == 0)
        {
            a.isDotNet = true;
            std::wstring codeBase = server.queryStringOr(L"CodeBase");
            if (_wcsnicmp(codeBase.c_str(), L"file:///", 8) == 0)
            {
                std::wstring f = codeBase.substr(8);
                std::replace(f.begin(), f.end(), L'/', L'\\');
                a.fileName = f;
            }
            auto subs = server.subKeys();   // first version subkey holds RuntimeVersion
            if (!subs.empty())
            {
                RegKey ver;
                if (ver.open(server.get(), subs.front()))
                    a.dotNetVersion = ver.queryStringOr(L"RuntimeVersion");
            }
        }

        readVersionInfo(a, remote);
    }

    // sub_140002C8C: enumerate one Addins key's ProgID subkeys into records.
    void scanAddinsKey(std::vector<Addin>& out, HKEY hive, HKEY hcr,
                       const std::wstring& subPath, HKEY rootConst, int appIndex,
                       const std::wstring& remote)
    {
        if (!hive) return;
        RegKey addins;
        if (!addins.open(hive, subPath)) return;

        for (const std::wstring& progId : addins.subKeys())
        {
            Addin a;
            a.progId = progId;
            a.appIndex = appIndex;
            a.rootKey = rootConst;
            a.regSubPath = subPath + L"\\" + progId;

            RegKey item;
            if (item.open(addins.get(), progId))
            {
                a.description = item.queryStringOr(L"Description");
                if (a.description.empty()) a.description = item.queryStringOr(L"FriendlyName");
                if (auto lb = item.queryDword(L"LoadBehavior")) a.loadBehavior = *lb;
            }

            resolveClsid(a, hcr, remote);
            out.push_back(std::move(a));
        }
    }
}

std::wstring Addin::createdTimeText() const
{
    // sub_140004F00: LOCALE_SYSTEM_DEFAULT (0x0800), short date + default time,
    // guarded by year > 1985. Anything else renders "N/A".
    SYSTEMTIME st{};
    if (!hasFile || !::FileTimeToSystemTime(&createdTime, &st) || st.wYear <= 1985) return L"N/A";
    wchar_t date[128] = {}, time[128] = {};
    ::GetDateFormatW(LOCALE_SYSTEM_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, static_cast<int>(std::size(date)));
    ::GetTimeFormatW(LOCALE_SYSTEM_DEFAULT, 0, &st, nullptr, time, static_cast<int>(std::size(time)));
    return std::wstring(date) + L" " + time;
}

std::vector<Addin> EnumerateAddins(const std::wstring& remote)
{
    std::vector<Addin> out;

    HKEY hkcu = connectHive(HKEY_CURRENT_USER, remote);
    HKEY hklm = connectHive(HKEY_LOCAL_MACHINE, remote);
    HKEY hcr  = connectHive(HKEY_CLASSES_ROOT, remote);

    for (int appIndex = 1; appIndex <= 8; ++appIndex)
    {
        const std::wstring app = kScanApps[appIndex - 1];
        for (const wchar_t* layout : kLayouts)
        {
            std::wstring path = format1(layout, app);
            scanAddinsKey(out, hkcu, hcr, path, HKEY_CURRENT_USER, appIndex, remote);
            scanAddinsKey(out, hklm, hcr, path, HKEY_LOCAL_MACHINE, appIndex, remote);
        }
    }

    if (!remote.empty())
    {
        if (hkcu) ::RegCloseKey(hkcu);
        if (hklm) ::RegCloseKey(hklm);
        if (hcr)  ::RegCloseKey(hcr);
    }
    return out;
}

bool SetLoadBehavior(Addin& a, DWORD orValue, DWORD andMask, const std::wstring& remote)
{
    HKEY hive = connectHive(a.rootKey, remote);
    if (!hive) return false;

    RegKey k;
    bool ok = false;
    if (k.open(hive, a.regSubPath, KEY_READ | KEY_WRITE))
    {
        DWORD data = 0;
        if (auto lb = k.queryDword(L"LoadBehavior")) data = *lb;
        data = orValue | (data & ~andMask);
        if (k.setDword(L"LoadBehavior", data)) { a.loadBehavior = data; ok = true; }
    }
    if (!remote.empty() && hive) ::RegCloseKey(hive);
    return ok;
}

bool SetStartMode(Addin& a, int mode, const std::wstring& remote)
{
    // sub_14000A644 mask table: Disabled 0/254, Startup 2/254, Demand 8/254, First 16/254.
    static const DWORD orv[] = { 0, 2, 8, 16 };
    if (mode < 0 || mode > 3) return false;
    return SetLoadBehavior(a, orv[mode], 254, remote);
}

bool SetConnected(Addin& a, int yes, const std::wstring& remote)
{
    // sub_14000A644: No 0/1 (clear bit 0), Yes 1/1 (set bit 0).
    if (yes != 0 && yes != 1) return false;
    return SetLoadBehavior(a, static_cast<DWORD>(yes), 1, remote);
}

bool AddinMatchesName(const Addin& a, const std::wstring& name)
{
    // sub_140001B04: match ProgID, CLSID, full file path, or the file's basename.
    return _wcsicmp(name.c_str(), a.progId.c_str()) == 0
        || _wcsicmp(name.c_str(), a.clsid.c_str()) == 0
        || _wcsicmp(name.c_str(), a.fileName.c_str()) == 0
        || _wcsicmp(name.c_str(), baseName(a.fileName).c_str()) == 0;
}
