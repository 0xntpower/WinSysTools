// IconSave.cpp -- see IconSave.h. The GRPICONDIR -> ICONDIR conversion below is a
// clean rewrite of sub_401807, confirmed line-for-line by IDA Hex-Rays and by
// rea/Ghidra. Layouts (fields comma-separated, byte sizes in parentheses):
//   GRPICONDIR = reserved(2), type(2), count(2), then GRPICONDIRENTRY[count]
//   GRPICONDIRENTRY (14) = w(1), h(1), colorCount(1), reserved(1), planes(2),
//                          bitCount(2), bytesInRes(4), id(2)
//   ICONDIR = reserved(2), type(2), count(2), then ICONDIRENTRY[count]
//   ICONDIRENTRY (16) = w(1), h(1), colorCount(1), reserved(1), planes(2),
//                       bitCount(2), bytesInRes(4), imageOffset(4)
#include "IconSave.h"
#include <cstring>

namespace
{
    // A cursor's RT_CURSOR member begins with a 4-byte hotspot (two WORDs) that is
    // not part of the written .cur image data.
    constexpr DWORD kHotspotBytes = 4;

    const BYTE* lockResource(HMODULE hModule, const wchar_t* name, const wchar_t* type, DWORD* size)
    {
        HRSRC hRes = FindResourceW(hModule, name, type);
        if (!hRes) return nullptr;
        if (size) *size = SizeofResource(hModule, hRes);
        HGLOBAL hData = LoadResource(hModule, hRes);
        if (!hData) return nullptr;
        return static_cast<const BYTE*>(LockResource(hData));
    }

    template <class T> T read(const BYTE* p) { T v; memcpy(&v, p, sizeof(T)); return v; }
    template <class T> void write(BYTE* p, T v) { memcpy(p, &v, sizeof(T)); }
}

bool WriteIconToFile(HANDLE hFile, HMODULE hModule, const wchar_t* resName,
                     bool srcIsCursor, int outType)
{
    const wchar_t* grpType = srcIsCursor ? RT_GROUP_CURSOR : RT_GROUP_ICON;
    const wchar_t* memType = srcIsCursor ? RT_CURSOR : RT_ICON;

    const BYTE* grp = lockResource(hModule, resName, grpType, nullptr);
    if (!grp) return false;

    const WORD count = read<WORD>(grp + 4);
    if (count == 0) return false;

    const DWORD headerSize = 6u + 16u * count;        // ICONDIR + ICONDIRENTRY[count]
    std::vector<BYTE> dir(headerSize, 0);
    write<WORD>(&dir[0], 0);                           // reserved
    write<WORD>(&dir[2], static_cast<WORD>(outType));  // 1 = icon, 2 = cursor
    write<WORD>(&dir[4], count);

    DWORD running = 0;
    const BYTE* srcEntry = grp + 6;                    // first GRPICONDIRENTRY
    for (WORD i = 0; i < count; ++i)
    {
        BYTE* e = &dir[6 + 16u * i];
        memcpy(e, srcEntry, 12);                       // through bytesInRes

        if (srcIsCursor)
        {
            // GRPCURSORDIRENTRY stores WORD width / WORD height. The .cur entry
            // keeps the low byte of width and half the low byte of height, and the
            // image is 4 bytes shorter (the hotspot is stripped from the member).
            e[0] = srcEntry[0];
            e[1] = static_cast<BYTE>(srcEntry[2] >> 1);
            e[2] = 0;                                  // colorCount
            e[3] = 0;                                  // reserved
            write<DWORD>(e + 8, read<DWORD>(e + 8) - kHotspotBytes);

            if (outType == 2)                          // saving as .cur: carry the hotspot
            {
                WORD memId = read<WORD>(srcEntry + 12);
                if (const BYTE* mem = lockResource(hModule, MAKEINTRESOURCEW(memId), RT_CURSOR, nullptr))
                {
                    write<WORD>(e + 4, read<WORD>(mem));       // hotspot X -> planes field
                    write<WORD>(e + 6, read<WORD>(mem + 2));   // hotspot Y -> bitCount field
                }
            }
        }

        write<DWORD>(e + 12, running + headerSize);    // imageOffset
        running += read<DWORD>(e + 8);
        if (srcIsCursor) running -= kHotspotBytes;     // mirrors the original's accumulator
        srcEntry += 14;
    }

    // dir holds [ICONDIR header (6)][ICONDIRENTRY[count]] contiguously, which is the
    // exact on-disk directory. (The original split this into two writes because its
    // scratch buffer kept a 2-byte alignment gap that is never emitted.)
    DWORD written = 0;
    if (!WriteFile(hFile, dir.data(), headerSize, &written, nullptr)) return false;

    // Append each member image (RT_ICON / RT_CURSOR), stripping the cursor hotspot.
    srcEntry = grp + 6;
    for (WORD i = 0; i < count; ++i)
    {
        WORD memId = read<WORD>(srcEntry + 12);
        DWORD size = 0;
        if (const BYTE* mem = lockResource(hModule, MAKEINTRESOURCEW(memId), memType, &size))
        {
            if (srcIsCursor)
                WriteFile(hFile, mem + kHotspotBytes, size - kHotspotBytes, &written, nullptr);
            else
                WriteFile(hFile, mem, size, &written, nullptr);
        }
        srcEntry += 14;
    }
    return true;
}

bool SaveIconToPath(const std::wstring& path, HMODULE hModule, const wchar_t* resName,
                    bool srcIsCursor, int outType)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    bool ok = WriteIconToFile(hFile, hModule, resName, srcIsCursor, outType);
    CloseHandle(hFile);
    return ok;
}

std::wstring BuildSavePath(const std::wstring& folder, const IconEntry& e, bool asCur)
{
    // Basename with everything from the first '.' dropped (sub_402815 '.' -> NUL).
    std::wstring base = IconEntry::baseName(e.filename);
    if (auto dot = base.find(L'.'); dot != std::wstring::npos) base.erase(dot);

    std::wstring id = e.resName.empty() ? std::to_wstring(e.resId) : e.resName;
    const wchar_t* ext = asCur ? L".cur" : L".ico";

    std::wstring dir = folder;
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir.push_back(L'\\');

    for (int n = 0; n < 1000; ++n)
    {
        std::wstring name = n == 0
            ? base + L"_" + id + ext
            : base + L"_" + std::to_wstring(n) + L"_" + id + ext;
        std::wstring full = dir + name;
        if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES)
            return full;
    }
    return {};
}

SaveStats SaveEntries(const std::wstring& folder, const std::vector<IconEntry>& entries,
                      bool saveCursorsAsIco)
{
    SaveStats stats;
    HMODULE hModule = nullptr;
    std::wstring loaded;

    for (const auto& e : entries)
    {
        const bool asCur = e.isCursor && !saveCursorsAsIco;
        const int outType = asCur ? 2 : 1;

        if (e.filename != loaded)
        {
            if (hModule) FreeLibrary(hModule);
            hModule = LoadLibraryExW(e.filename.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
            loaded = e.filename;
        }
        if (!hModule) { ++stats.failed; continue; }

        std::wstring path = BuildSavePath(folder, e, asCur);
        if (path.empty() || !SaveIconToPath(path, hModule, e.resourceId(), e.isCursor, outType))
            ++stats.failed;
        else
            ++stats.saved;
    }

    if (hModule) FreeLibrary(hModule);
    return stats;
}
