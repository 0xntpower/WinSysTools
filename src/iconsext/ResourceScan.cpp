// ResourceScan.cpp -- see ResourceScan.h.
#include "ResourceScan.h"
#include <tlhelp32.h>
#include <cstring>

namespace
{
    template <class T> T rd(const BYTE* p) { T v; memcpy(&v, p, sizeof(T)); return v; }

    // Context threaded through EnumResourceNames as its LONG_PTR parameter.
    struct EnumCtx
    {
        IconScanner* scanner;
        HMODULE      hModule;
        const std::wstring* file;
        bool         cursor;
    };

    // sub_401610: does the group contain an image matching (w, h, bits)? 0 = "any".
    bool groupMatches(const BYTE* grp, int w, int h, int bits)
    {
        WORD count = rd<WORD>(grp + 4);
        const BYTE* e = grp + 6;
        for (WORD i = 0; i < count; ++i, e += 14)
        {
            int ew = e[0], eh = e[1], eb = rd<WORD>(e + 6);
            if ((w == 0 || ew == w) && (h == 0 || eh == h) && (bits == 0 || eb == bits))
                return true;
        }
        return false;
    }

    BOOL CALLBACK enumProc(HMODULE, LPCWSTR type, LPWSTR name, LONG_PTR param)
    {
        auto* ctx = reinterpret_cast<EnumCtx*>(param);
        IconScanner& s = *ctx->scanner;

        const BYTE* grp = nullptr;
        if (HRSRC hRes = FindResourceW(ctx->hModule, name, type))
            if (HGLOBAL hData = LoadResource(ctx->hModule, hRes))
                grp = static_cast<const BYTE*>(LockResource(hData));
        if (!grp) return TRUE;

        const ScanOptions& o = s.options;
        if (o.filterSize || o.filterColor)
        {
            int w = o.filterSize ? o.width : 0;
            int h = o.filterSize ? o.height : 0;
            int b = o.filterColor ? o.colorBits : 0;
            if (!groupMatches(grp, w, h, b)) return TRUE;
        }

        IconEntry e;
        e.isCursor = ctx->cursor;
        e.filename = *ctx->file;
        if (IS_INTRESOURCE(name))
            e.resId = static_cast<int>(reinterpret_cast<ULONG_PTR>(name));
        else
            e.resName = name;

        WORD count = rd<WORD>(grp + 4);
        e.images = count;
        const BYTE* entry = grp + 6;
        for (WORD i = 0; i < count; ++i, entry += 14)
            e.totalSize += rd<DWORD>(entry + 8);

        if (s.visit) s.visit(ctx->hModule, e);
        s.results.push_back(std::move(e));
        return TRUE;
    }
}

void IconScanner::enumModule(HMODULE hModule, const std::wstring& file)
{
    EnumCtx ctx{ this, hModule, &file, false };
    if (options.icons)
        EnumResourceNamesW(hModule, RT_GROUP_ICON, enumProc, reinterpret_cast<LONG_PTR>(&ctx));
    ctx.cursor = true;
    if (options.cursors)
        EnumResourceNamesW(hModule, RT_GROUP_CURSOR, enumProc, reinterpret_cast<LONG_PTR>(&ctx));
}

void IconScanner::scanFile(const std::wstring& file)
{
    HMODULE hModule = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!hModule) return;
    enumModule(hModule, file);
    FreeLibrary(hModule);
}

void IconScanner::scanPath(const std::wstring& path)
{
    std::wstring p = path;
    // A '%' means the path still holds environment strings (sub_405015).
    if (p.find(L'%') != std::wstring::npos)
    {
        wchar_t buf[1024];
        if (ExpandEnvironmentStringsW(p.c_str(), buf, 1024)) p = buf;
    }
    // A bare directory is expanded to "\\*.*" (sub_4051F2 tail).
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        if (!p.empty() && p.back() != L'\\') p.push_back(L'\\');
        p += L"*.*";
    }
    scanWildcard(p);
}

void IconScanner::scanWildcard(const std::wstring& wildcard)
{
    if (cancelled()) return;

    size_t slash = wildcard.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? std::wstring() : wildcard.substr(0, slash + 1);
    std::wstring pattern = slash == std::wstring::npos ? wildcard : wildcard.substr(slash + 1);

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(wildcard.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (cancelled()) break;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                scanFile(dir + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    if (!options.subdirs || cancelled()) return;

    HANDLE hd = FindFirstFileW((dir + L"*.*").c_str(), &fd);
    if (hd == INVALID_HANDLE_VALUE) return;
    do
    {
        if (cancelled()) break;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.')
            scanWildcard(dir + fd.cFileName + L"\\" + pattern);
    } while (FindNextFileW(hd, &fd));
    FindClose(hd);
}

void IconScanner::scanProcess(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me{ sizeof(me) };
    if (Module32FirstW(snap, &me))
    {
        do
        {
            if (cancelled()) break;
            scanFile(me.szExePath);
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
}

HICON ExtractGroupIcon(HMODULE hModule, const wchar_t* name, bool cursor, int cx, int cy)
{
    const wchar_t* grpType = cursor ? RT_GROUP_CURSOR : RT_GROUP_ICON;
    const wchar_t* memType = cursor ? RT_CURSOR : RT_ICON;

    HRSRC hGrp = FindResourceW(hModule, name, grpType);
    if (!hGrp) return nullptr;
    const BYTE* grp = static_cast<const BYTE*>(LockResource(LoadResource(hModule, hGrp)));
    if (!grp) return nullptr;

    // fIcon is TRUE for the directory lookup even for cursors (sub_401599).
    int id = LookupIconIdFromDirectoryEx(const_cast<PBYTE>(grp), TRUE, cx, cy, LR_DEFAULTCOLOR);
    HRSRC hMem = FindResourceW(hModule, MAKEINTRESOURCEW(id), memType);
    if (!hMem) return nullptr;
    DWORD size = SizeofResource(hModule, hMem);
    const BYTE* mem = static_cast<const BYTE*>(LockResource(LoadResource(hModule, hMem)));
    if (!mem) return nullptr;

    return CreateIconFromResourceEx(const_cast<PBYTE>(mem), size, cursor ? FALSE : TRUE,
                                    0x00030000, cx, cy, LR_DEFAULTCOLOR);
}
