// VssEnumerator.cpp -- shadow copy enumeration and file browsing. Mirrors the
// original: VssApi.dll is resolved at run time, CreateVssBackupComponents builds
// the backup object, InitializeForBackup and SetContext(VSS_CTX_ALL) prime it, and
// Query returns every snapshot. Each snapshot's device object path is then a normal
// path that FindFirstFile walks for the file list and CopyFile reads for copy-out.
#include "VssEnumerator.h"
#include <windows.h>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <string>
#include <algorithm>

namespace
{
    using PfnCreateVss = HRESULT(STDAPICALLTYPE*)(IVssBackupComponents**);
    using PfnVssFree   = void(APIENTRY*)(VSS_SNAPSHOT_PROP*);

    const wchar_t kGlobalRootPrefix[] = L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolumeShadowCopy";

    long long ftToLL(const FILETIME& ft)
    {
        return (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }
    FILETIME llToFt(long long v)
    {
        FILETIME ft;
        ft.dwLowDateTime = static_cast<DWORD>(v & 0xFFFFFFFF);
        ft.dwHighDateTime = static_cast<DWORD>(static_cast<unsigned long long>(v) >> 32);
        return ft;
    }

    std::wstring fromPwsz(VSS_PWSZ p) { return p ? std::wstring(p) : std::wstring(); }

    std::wstring formatGuid(const GUID& g)
    {
        wchar_t buf[64];
        swprintf(buf, 64, L"{%8.8X-%4.4X-%4.4X-%2.2X%2.2X-%2.2X%2.2X%2.2X%2.2X%2.2X%2.2X}",
                 g.Data1, g.Data2, g.Data3,
                 g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
                 g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
        return buf;
    }

    // VSS_VOLUME_SNAPSHOT_ATTRIBUTES bits in the order the original lists them,
    // joined with a comma. Names match STRINGTABLE 701..718.
    std::wstring decodeSnapAttrs(LONG a)
    {
        struct { unsigned bit; const wchar_t* name; } kBits[] = {
            { 0x00000001, L"Persistent" },
            { 0x00000002, L"No Auto-Recovery" },
            { 0x00000004, L"Client-Accessible" },
            { 0x00000008, L"No Auto-Release" },
            { 0x00000010, L"No Writers" },
            { 0x00000020, L"Transportable" },
            { 0x00000040, L"Not Exposed" },
            { 0x00000080, L"Not Transacted" },
            { 0x00010000, L"Hardware Assisted" },
            { 0x00020000, L"Differential" },
            { 0x00040000, L"PLEX" },
            { 0x00080000, L"Imported" },
            { 0x00100000, L"Exposed Locally" },
            { 0x00200000, L"Exposed Remotely" },
            { 0x00400000, L"Auto Recover" },
            { 0x00800000, L"Rollback Recovery" },
            { 0x01000000, L"Delayed PostSnapshot" },
            { 0x02000000, L"TxF recovery" },
        };
        std::wstring out;
        for (auto& b : kBits)
            if (static_cast<unsigned>(a) & b.bit)
            {
                if (!out.empty()) out += L", ";
                out += b.name;
            }
        return out;
    }

    // Compact attribute letters for a file entry. The I, E, X and V letters match the
    // readme (Not Content Indexed, Encrypted, No Scrub File, Integrity).
    std::wstring decodeFileAttrs(DWORD a)
    {
        struct { DWORD bit; wchar_t ch; } kFlags[] = {
            { FILE_ATTRIBUTE_READONLY,            L'R' },
            { FILE_ATTRIBUTE_HIDDEN,              L'H' },
            { FILE_ATTRIBUTE_SYSTEM,              L'S' },
            { FILE_ATTRIBUTE_DIRECTORY,           L'D' },
            { FILE_ATTRIBUTE_ARCHIVE,             L'A' },
            { FILE_ATTRIBUTE_TEMPORARY,           L'T' },
            { FILE_ATTRIBUTE_SPARSE_FILE,         L'P' },
            { FILE_ATTRIBUTE_REPARSE_POINT,       L'L' },
            { FILE_ATTRIBUTE_COMPRESSED,          L'C' },
            { FILE_ATTRIBUTE_OFFLINE,             L'O' },
            { FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, L'I' },
            { FILE_ATTRIBUTE_ENCRYPTED,           L'E' },
            { 0x00008000 /*INTEGRITY_STREAM*/,    L'V' },
            { 0x00020000 /*NO_SCRUB_DATA*/,       L'X' },
        };
        std::wstring out;
        for (auto& f : kFlags)
            if (a & f.bit) out += f.ch;
        return out;
    }

    std::wstring groupNumber(unsigned long long n)
    {
        wchar_t raw[32];
        swprintf(raw, 32, L"%llu", n);
        NUMBERFMTW nf{};
        wchar_t dec[] = L".";
        wchar_t thou[] = L",";
        nf.NumDigits = 0;
        nf.LeadingZero = 0;
        nf.Grouping = 3;
        nf.lpDecimalSep = dec;
        nf.lpThousandSep = thou;
        nf.NegativeOrder = 1;
        wchar_t out[64];
        if (::GetNumberFormatW(LOCALE_USER_DEFAULT, 0, raw, &nf, out, 64) > 0) return out;
        return raw;
    }

    std::wstring extensionOf(const std::wstring& name)
    {
        size_t dot = name.rfind(L'.');
        if (dot == std::wstring::npos || dot + 1 >= name.size()) return std::wstring();
        return name.substr(dot + 1);
    }

    SnapshotRecord buildSnapshot(const VSS_SNAPSHOT_PROP& s, const DisplayOptions& opt)
    {
        SnapshotRecord r;
        r.deviceObject = fromPwsz(s.m_pwszSnapshotDeviceObject);
        r.cells[0] = r.deviceObject;

        std::wstring volName = fromPwsz(s.m_pwszOriginalVolumeName);
        r.cells[3] = volName;

        wchar_t paths[1024] = L"";
        DWORD retLen = 0;
        if (!volName.empty())
            ::GetVolumePathNamesForVolumeNameW(volName.c_str(), paths, 1024, &retLen);
        r.volumePath = paths;   // first mount point in the double-null list
        r.cells[2] = r.volumePath;

        r.cells[4] = fromPwsz(s.m_pwszOriginatingMachine);
        r.cells[5] = fromPwsz(s.m_pwszServiceMachine);

        r.creationKey = s.m_tsCreationTimestamp;
        FILETIME ft = llToFt(s.m_tsCreationTimestamp);
        r.cells[6] = FormatTimeCell(ft, opt.showTimeInGmt);

        r.cells[7] = decodeSnapAttrs(s.m_lSnapshotAttributes);
        r.cells[8] = fromPwsz(s.m_pwszExposedName);
        r.cells[9] = fromPwsz(s.m_pwszExposedPath);
        r.cells[10] = formatGuid(s.m_SnapshotId);
        r.cells[11] = formatGuid(s.m_SnapshotSetId);
        r.cells[12] = formatGuid(s.m_ProviderId);

        // Explorer path only when the volume resolved to a drive letter mount point.
        if (r.volumePath.size() >= 2 && r.volumePath[1] == L':')
        {
            SYSTEMTIME st{};
            ::FileTimeToSystemTime(&ft, &st);   // always UTC, the way @GMT names work
            wchar_t ep[128];
            swprintf(ep, 128, L"\\\\localhost\\%c$\\@GMT-%4.4d.%2.2d.%2.2d-%2.2d.%2.2d.%2.2d",
                     r.volumePath[0], st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            r.explorerPath = ep;
            r.cells[1] = ep;
        }
        return r;
    }

    // Set a copied folder's timestamps and attributes from the source find data.
    void applyFolderInfo(const std::wstring& dir, const WIN32_FIND_DATAW& fd)
    {
        ::SetFileAttributesW(dir.c_str(), fd.dwFileAttributes);
        HANDLE h = ::CreateFileW(dir.c_str(), FILE_WRITE_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            ::SetFileTime(h, &fd.ftCreationTime, &fd.ftLastAccessTime, &fd.ftLastWriteTime);
            ::CloseHandle(h);
        }
    }

    bool exists(const std::wstring& path)
    {
        return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // Build a destination path that does not collide, inserting a 4-digit number
    // before the extension, matching the original's copy-under-another-name behaviour.
    std::wstring uniqueDest(const std::wstring& destDir, const std::wstring& name)
    {
        std::wstring base = destDir + L"\\" + name;
        if (!exists(base)) return base;
        size_t dot = name.rfind(L'.');
        std::wstring stem = (dot == std::wstring::npos) ? name : name.substr(0, dot);
        std::wstring ext = (dot == std::wstring::npos) ? std::wstring() : name.substr(dot);
        for (int i = 1; i < 10000; ++i)
        {
            wchar_t num[8];
            swprintf(num, 8, L"%4.4d", i);
            std::wstring cand = destDir + L"\\" + stem + num + ext;
            if (!exists(cand)) return cand;
        }
        return base;
    }

    int copyTree(const std::wstring& src, const std::wstring& dst)
    {
        DWORD attr = ::GetFileAttributesW(src.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) return 0;
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
        {
            ::CreateDirectoryW(dst.c_str(), nullptr);
            int count = 0;
            WIN32_FIND_DATAW fd{};
            HANDLE h = ::FindFirstFileW((src + L"\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                    count += copyTree(src + L"\\" + fd.cFileName, dst + L"\\" + fd.cFileName);
                } while (::FindNextFileW(h, &fd));
                ::FindClose(h);
            }
            WIN32_FIND_DATAW self{};
            HANDLE sh = ::FindFirstFileW(src.c_str(), &self);
            if (sh != INVALID_HANDLE_VALUE) { applyFolderInfo(dst, self); ::FindClose(sh); }
            return count;
        }
        ::CopyFileW(src.c_str(), dst.c_str(), FALSE);
        return 1;
    }
}

std::wstring FormatTimeCell(const FILETIME& ft, bool gmt)
{
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) return std::wstring();
    FILETIME use = ft;
    if (!gmt)
    {
        FILETIME local{};
        if (::FileTimeToLocalFileTime(&ft, &local)) use = local;
    }
    SYSTEMTIME st{};
    if (!::FileTimeToSystemTime(&use, &st) || st.wYear <= 1000) return std::wstring();
    wchar_t date[128] = L"";
    wchar_t time[128] = L"";
    ::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 128);
    ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 128);
    return std::wstring(date) + L" " + time;
}

std::vector<SnapshotRecord> EnumerateSnapshots(const DisplayOptions& opt, HRESULT* hrOut)
{
    std::vector<SnapshotRecord> out;
    HRESULT hr = S_OK;

    HMODULE dll = ::LoadLibraryW(L"VssApi.dll");
    if (!dll)
    {
        if (hrOut) *hrOut = HRESULT_FROM_WIN32(::GetLastError());
        return out;
    }

    auto create = reinterpret_cast<PfnCreateVss>(
        ::GetProcAddress(dll, "?CreateVssBackupComponents@@YAJPEAPEAVIVssBackupComponents@@@Z"));
    if (!create)
        create = reinterpret_cast<PfnCreateVss>(
            ::GetProcAddress(dll, "?CreateVssBackupComponents@@YGJPAPAVIVssBackupComponents@@@Z"));
    auto freeProp = reinterpret_cast<PfnVssFree>(::GetProcAddress(dll, "VssFreeSnapshotProperties"));

    if (create)
    {
        IVssBackupComponents* bc = nullptr;
        hr = create(&bc);
        if (SUCCEEDED(hr) && bc)
        {
            bc->InitializeForBackup();
            bc->SetContext(VSS_CTX_ALL);
            IVssEnumObject* pEnum = nullptr;
            GUID nullId{};
            hr = bc->Query(nullId, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &pEnum);
            if (SUCCEEDED(hr) && pEnum)
            {
                VSS_OBJECT_PROP prop{};
                ULONG fetched = 0;
                while (pEnum->Next(1, &prop, &fetched) == S_OK && fetched == 1)
                {
                    VSS_SNAPSHOT_PROP& snap = prop.Obj.Snap;
                    out.push_back(buildSnapshot(snap, opt));
                    if (freeProp) freeProp(&snap);
                }
                pEnum->Release();
            }
            bc->Release();
        }
    }
    else
    {
        hr = E_FAIL;
    }

    ::FreeLibrary(dll);
    if (hrOut) *hrOut = hr;
    return out;
}

std::vector<FileRecord> EnumerateFiles(const SnapshotRecord& snap,
                                       const std::wstring& folder,
                                       const DisplayOptions& opt)
{
    std::vector<FileRecord> out;
    std::wstring base = snap.deviceObject + folder;
    std::wstring pattern = base + L"\\*";

    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;

    do
    {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        FileRecord r;
        r.name = fd.cFileName;
        r.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        r.cells[0] = r.name;

        r.modifiedKey = ftToLL(fd.ftLastWriteTime);
        r.cells[1] = FormatTimeCell(fd.ftLastWriteTime, opt.showTimeInGmt);
        r.createdKey = ftToLL(fd.ftCreationTime);
        r.cells[2] = FormatTimeCell(fd.ftCreationTime, opt.showTimeInGmt);

        if (!r.isDir)
        {
            ULARGE_INTEGER sz;
            sz.LowPart = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            r.sizeKey = static_cast<long long>(sz.QuadPart);
            r.cells[3] = groupNumber(sz.QuadPart);
            r.cells[5] = extensionOf(r.name);
        }

        r.cells[4] = decodeFileAttrs(fd.dwFileAttributes);

        // Entry Modified Time is the MFT change time, from FILE_BASIC_INFO.
        FILETIME change{};
        std::wstring full = base + L"\\" + r.name;
        HANDLE fh = ::CreateFileW(full.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (fh != INVALID_HANDLE_VALUE)
        {
            FILE_BASIC_INFO bi{};
            if (::GetFileInformationByHandleEx(fh, FileBasicInfo, &bi, sizeof(bi)))
            {
                change.dwLowDateTime = static_cast<DWORD>(bi.ChangeTime.QuadPart & 0xFFFFFFFF);
                change.dwHighDateTime = static_cast<DWORD>(static_cast<unsigned long long>(bi.ChangeTime.QuadPart) >> 32);
            }
            ::CloseHandle(fh);
        }
        r.entryModifiedKey = ftToLL(change);
        r.cells[6] = FormatTimeCell(change, opt.showTimeInGmt);

        out.push_back(std::move(r));
    } while (::FindNextFileW(h, &fd));

    ::FindClose(h);
    return out;
}

int CopyItemsFromSnapshot(const SnapshotRecord& snap,
                          const std::wstring& folder,
                          const std::vector<std::wstring>& names,
                          const std::wstring& destDir)
{
    ::CreateDirectoryW(destDir.c_str(), nullptr);
    std::wstring base = snap.deviceObject + folder;
    int count = 0;
    for (const auto& name : names)
    {
        std::wstring src = base + L"\\" + name;
        std::wstring dst = uniqueDest(destDir, name);
        count += copyTree(src, dst);
    }
    return count;
}

bool CopyFileFromShadow(const std::wstring& shadowId,
                        const std::wstring& sourcePath,
                        const std::wstring& destFile)
{
    std::vector<SnapshotRecord> snaps = EnumerateSnapshots(DisplayOptions{}, nullptr);
    if (snaps.empty()) return false;

    const SnapshotRecord* match = nullptr;

    // exact device object, or a device object path prefix
    for (const auto& s : snaps)
        if (_wcsicmp(s.deviceObject.c_str(), shadowId.c_str()) == 0) { match = &s; break; }

    // a bare number, for example "3"
    if (!match && !shadowId.empty() &&
        std::all_of(shadowId.begin(), shadowId.end(), [](wchar_t c) { return iswdigit(c); }))
    {
        std::wstring dev = std::wstring(kGlobalRootPrefix) + shadowId;
        for (const auto& s : snaps)
            if (_wcsicmp(s.deviceObject.c_str(), dev.c_str()) == 0) { match = &s; break; }
    }

    // a volume such as "C:" or "C:\\": newest snapshot of that volume wins
    if (!match && shadowId.size() >= 2 && shadowId.size() <= 3 && shadowId[1] == L':')
    {
        const SnapshotRecord* best = nullptr;
        for (const auto& s : snaps)
            if (_wcsnicmp(s.volumePath.c_str(), shadowId.c_str(), 2) == 0)
                if (!best || s.creationKey > best->creationKey) best = &s;
        match = best;
    }

    if (!match) return false;

    std::wstring sp = sourcePath;
    if (!sp.empty() && sp[0] == L'\\') sp.erase(0, 1);
    std::wstring full = match->deviceObject + L"\\" + sp;
    return ::CopyFileW(full.c_str(), destFile.c_str(), FALSE) != 0;
}
