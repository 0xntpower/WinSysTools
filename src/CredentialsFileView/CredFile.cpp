// CredFile.cpp -- Credentials file parsing and DPAPI decode.
#include "CredFile.h"
#include "resource.h"
#include <windows.h>
#include <dpapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace
{
    // DPAPI provider GUID df9d8cd0-1501-11d1-8c7a-00c04fc297eb, little-endian bytes.
    const BYTE kDpapiProvider[16] = {
        0xD0, 0x8C, 0x9D, 0xDF, 0x01, 0x15, 0xD1, 0x11,
        0x8C, 0x7A, 0x00, 0xC0, 0x4F, 0xC2, 0x97, 0xEB
    };

    std::wstring loadStr(UINT id)
    {
        wchar_t buf[256]{};
        ::LoadStringW(::GetModuleHandleW(nullptr), id, buf, 256);
        return buf;
    }

    DWORD rd32(const BYTE* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD)p[3] << 24); }

    long long fileTimeToLL(FILETIME ft) { return ((long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime; }

    std::wstring formatFileTime(long long ll, bool gmt)
    {
        if (ll == 0) return L"";
        FILETIME ft{ (DWORD)(ll & 0xFFFFFFFF), (DWORD)((ll >> 32) & 0xFFFFFFFF) };
        FILETIME shown = ft;
        if (!gmt) ::FileTimeToLocalFileTime(&ft, &shown);
        SYSTEMTIME st{};
        if (!::FileTimeToSystemTime(&shown, &st)) return L"";
        wchar_t date[64]{}, time[64]{};
        ::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 64);
        ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 64);
        return std::wstring(date) + L" " + time;
    }

    // A length-prefixed unicode string -- DWORD byte length, then the bytes.
    std::wstring readLpwstr(const BYTE* base, size_t size, size_t& cursor)
    {
        if (cursor + 4 > size) { cursor = size + 1; return L""; }
        DWORD len = rd32(base + cursor);
        cursor += 4;
        if (len == 0 || cursor + len > size) { if (cursor + len > size) cursor = size + 1; return L""; }
        std::wstring s(reinterpret_cast<const wchar_t*>(base + cursor), len / sizeof(wchar_t));
        cursor += len;
        if (!s.empty() && s.back() == L'\0') s.pop_back();
        return s;
    }

    // The credential blob holds the password, usually UTF-16 but sometimes ASCII
    // (the v1.12 fix). Treat it as UTF-16 when a high byte is zero, else ANSI.
    std::wstring passwordFromBlob(const BYTE* p, size_t n)
    {
        if (n == 0) return L"";
        if (n >= 2 && (n % 2) == 0)
        {
            bool utf16 = false;
            for (size_t i = 1; i < n; i += 2)
                if (p[i] == 0) { utf16 = true; break; }
            if (utf16)
            {
                std::wstring s(reinterpret_cast<const wchar_t*>(p), n / 2);
                size_t z = s.find(L'\0');
                if (z != std::wstring::npos) s.resize(z);
                return s;
            }
        }
        int wn = ::MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(p), (int)n, nullptr, 0);
        std::wstring w(wn, L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(p), (int)n, w.data(), wn);
        size_t z = w.find(L'\0');
        if (z != std::wstring::npos) w.resize(z);
        return w;
    }

    std::wstring entryTypeName(DWORD type)
    {
        if (type >= 1 && type <= 6) return loadStr(IDS_TYPE_BASE + type);
        return std::to_wstring(type);
    }

    std::wstring persistName(DWORD persist)
    {
        if (persist >= 1 && persist <= 3) return loadStr(IDS_PERSIST_BASE + persist);
        return std::to_wstring(persist);
    }

    std::wstring fileNameOf(const std::wstring& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? path : path.substr(slash + 1);
    }

    bool readWholeFile(const std::wstring& path, std::vector<BYTE>& out, FILETIME& created, FILETIME& modified, long long& size)
    {
        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        ::GetFileTime(h, &created, nullptr, &modified);
        LARGE_INTEGER li{};
        ::GetFileSizeEx(h, &li);
        size = li.QuadPart;
        out.resize((size_t)li.QuadPart);
        DWORD got = 0;
        bool ok = out.empty() || (::ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size());
        ::CloseHandle(h);
        return ok;
    }
}

int LocateDpapiBlob(const BYTE* data, size_t size)
{
    for (int off = 0; off < 16; ++off)
    {
        if ((size_t)off + 4 + sizeof(kDpapiProvider) > size) break;
        if (memcmp(data + off + 4, kDpapiProvider, sizeof(kDpapiProvider)) == 0)
            return off;
    }
    return -1;
}

bool ParseVistaEntry(const BYTE* plain, size_t size, const DisplayOptions& disp, CredRecord& rec)
{
    if (size < 0x34) return false;

    DWORD type = rd32(plain + 0x0C);
    long long lastWritten = (long long)rd32(plain + 0x14) | ((long long)rd32(plain + 0x18) << 32);
    DWORD persist = rd32(plain + 0x20);

    size_t cursor = 0x30;
    std::wstring target = readLpwstr(plain, size, cursor);   // Entry Name
    readLpwstr(plain, size, cursor);                         // TargetAlias
    readLpwstr(plain, size, cursor);                         // Comment
    readLpwstr(plain, size, cursor);                         // UnkData
    std::wstring userName = readLpwstr(plain, size, cursor); // User Name

    std::wstring password;
    if (cursor + 4 <= size)
    {
        DWORD blobSize = rd32(plain + cursor);
        cursor += 4;
        if (cursor + blobSize <= size)
            password = passwordFromBlob(plain + cursor, blobSize);
    }

    rec.cells[3] = formatFileTime(lastWritten, disp.showTimeInGmt);  // Modified Time
    rec.cells[4] = entryTypeName(type);                              // Entry Type
    rec.cells[5] = persistName(persist);                             // Persist
    rec.cells[6] = target;                                           // Entry Name
    rec.cells[7] = userName;                                         // User Name
    rec.cells[8] = password;                                         // Password
    rec.modifiedKey = lastWritten;
    rec.decryptedSize = (long long)size;
    rec.cells[2] = std::to_wstring(size);                            // Decrypted Size
    return true;
}

namespace
{
    // Decrypt a DPAPI blob with the running system's key. Returns the plaintext.
    bool decryptBlob(const BYTE* blob, size_t size, std::vector<BYTE>& out)
    {
        DATA_BLOB in{ (DWORD)size, const_cast<BYTE*>(blob) };
        DATA_BLOB res{};
        if (!::CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &res))
            return false;
        out.assign(res.pbData, res.pbData + res.cbData);
        if (res.pbData) ::LocalFree(res.pbData);
        return true;
    }

    void fillFileColumns(CredRecord& rec, const std::wstring& path, long long fileSize,
                         FILETIME created, FILETIME modified, bool xp, bool gmt)
    {
        rec.cells[0] = fileNameOf(path);                             // Filename
        rec.cells[1] = loadStr(xp ? 551 : 552);                      // Version
        rec.cells[9] = path;                                         // Full Path
        rec.cells[10] = std::to_wstring(fileSize);                   // File Size
        rec.fileSize = fileSize;
        rec.fileCreatedKey = fileTimeToLL(created);
        rec.fileModifiedKey = fileTimeToLL(modified);
        rec.cells[11] = formatFileTime(rec.fileModifiedKey, gmt);    // File Modified Time
        rec.cells[12] = formatFileTime(rec.fileCreatedKey, gmt);     // File Created Time
    }
}

int DecodeFile(const std::wstring& path, const DecodeRequest& req, std::vector<CredRecord>& out)
{
    std::vector<BYTE> raw;
    FILETIME created{}, modified{};
    long long fileSize = 0;
    if (!readWholeFile(path, raw, created, modified, fileSize) || raw.size() <= 0x50)
        return 0;

    int blobOff = LocateDpapiBlob(raw.data(), raw.size());
    if (blobOff < 0) return 0;

    std::vector<BYTE> plain;
    if (!decryptBlob(raw.data() + blobOff, raw.size() - blobOff, plain) || plain.size() <= 0x10)
        return 0;

    int added = 0;
    bool xp = plain.size() >= 4 && rd32(plain.data()) == 1;
    if (xp)
    {
        // XP -- several entries, each prefixed, advancing by the entry size plus 16.
        size_t cursor = 8;
        while (cursor + 4 < plain.size())
        {
            size_t entrySize = rd32(plain.data() + cursor);
            if (entrySize == 0 || cursor + entrySize > plain.size()) break;
            CredRecord rec;
            if (ParseVistaEntry(plain.data() + cursor, entrySize, req.display, rec))
            {
                rec.plain.assign(plain.data() + cursor, plain.data() + cursor + entrySize);
                fillFileColumns(rec, path, fileSize, created, modified, true, req.display.showTimeInGmt);
                out.push_back(std::move(rec));
                ++added;
            }
            cursor += entrySize + 16;
        }
    }
    else
    {
        CredRecord rec;
        if (ParseVistaEntry(plain.data(), plain.size(), req.display, rec))
        {
            rec.plain = plain;
            fillFileColumns(rec, path, fileSize, created, modified, false, req.display.showTimeInGmt);
            out.push_back(std::move(rec));
            ++added;
        }
    }
    return added;
}

int DecodeFolders(const DecodeRequest& req, std::vector<CredRecord>& out)
{
    int added = 0;
    for (const std::wstring& folder : req.credFolders)
    {
        if (folder.empty()) continue;
        std::wstring spec = folder;
        if (spec.back() != L'\\' && spec.back() != L'/') spec += L"\\";
        std::wstring pattern = spec + L"*";
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            added += DecodeFile(spec + fd.cFileName, req, out);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    return added;
}

bool CredSelfTest(std::wstring& detail)
{
    // Build a synthetic decrypted Vista CREDENTIAL structure with known,
    // non-secret values and confirm every decoded column. No real credential
    // is read, written or displayed.
    auto putW = [](std::vector<BYTE>& b, const std::wstring& s) {
        DWORD bytes = (DWORD)(s.size() * sizeof(wchar_t));
        b.push_back(bytes & 0xFF); b.push_back((bytes >> 8) & 0xFF);
        b.push_back((bytes >> 16) & 0xFF); b.push_back((bytes >> 24) & 0xFF);
        const BYTE* p = reinterpret_cast<const BYTE*>(s.data());
        b.insert(b.end(), p, p + bytes);
    };
    auto put32 = [](std::vector<BYTE>& b, DWORD v) {
        b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF);
        b.push_back((v >> 16) & 0xFF); b.push_back((v >> 24) & 0xFF);
    };

    const long long knownTime = 0x01D9A1B2C3D4E5F6LL;  // arbitrary fixed FILETIME
    std::vector<BYTE> blob;
    put32(blob, 0);                                    // 0x00 credFlags
    put32(blob, 0);                                    // 0x04 credSize
    put32(blob, 0);                                    // 0x08 credUnk0
    put32(blob, 1);                                    // 0x0C type = Generic
    put32(blob, 0);                                    // 0x10 flags
    put32(blob, (DWORD)(knownTime & 0xFFFFFFFF));      // 0x14 lastWritten low
    put32(blob, (DWORD)((knownTime >> 32) & 0xFFFFFFFF)); // 0x18 lastWritten high
    put32(blob, 0);                                    // 0x1C unk
    put32(blob, 2);                                    // 0x20 persist = Local Machine
    put32(blob, 0);                                    // 0x24 attributeCount
    put32(blob, 0);                                    // 0x28 unk0
    put32(blob, 0);                                    // 0x2C unk1
    putW(blob, L"SelfTestEntry");                      // Target -> Entry Name
    putW(blob, L"");                                   // TargetAlias
    putW(blob, L"");                                   // Comment
    putW(blob, L"");                                   // UnkData
    putW(blob, L"SelfTestUser");                       // UserName
    std::wstring pw = L"SyntheticPass123";             // synthetic, not a real secret
    put32(blob, (DWORD)(pw.size() * sizeof(wchar_t)));
    const BYTE* pwb = reinterpret_cast<const BYTE*>(pw.data());
    blob.insert(blob.end(), pwb, pwb + pw.size() * sizeof(wchar_t));

    DisplayOptions disp;
    CredRecord rec;
    bool ok = ParseVistaEntry(blob.data(), blob.size(), disp, rec);
    ok = ok
        && rec.cells[4] == L"Generic"
        && rec.cells[5] == L"Local Machine"
        && rec.cells[6] == L"SelfTestEntry"
        && rec.cells[7] == L"SelfTestUser"
        && rec.cells[8] == L"SyntheticPass123"
        && rec.modifiedKey == knownTime;

    // Also confirm the DPAPI blob locator finds a provider GUID past a header.
    std::vector<BYTE> fileBytes = { 1, 0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
    fileBytes.insert(fileBytes.end(), kDpapiProvider, kDpapiProvider + 16);
    bool locOk = LocateDpapiBlob(fileBytes.data(), fileBytes.size()) == 12;

    detail = (ok && locOk) ? L"self-check passed" : L"self-check FAILED";
    return ok && locOk;
}
