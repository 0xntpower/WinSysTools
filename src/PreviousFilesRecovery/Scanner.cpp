// Scanner.cpp -- recreation of the scan core (sub_140011168 plus sub_140010E10).
// For each live folder the files are gathered once, then every shadow copy's copy
// of that folder is enumerated and each entry classified against the live files.
// The live tree is then recursed into subfolders up to the requested depth.
#include "Scanner.h"
#include "ShadowCopy.h"
#include <algorithm>
#include <map>
#include <unordered_map>

namespace
{
    unsigned long long sizeOf(const WIN32_FIND_DATAW& f)
    {
        return ((unsigned long long)f.nFileSizeHigh << 32) | f.nFileSizeLow;
    }
    bool timesEqual(const FILETIME& a, const FILETIME& b)
    {
        return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
    }
    bool isDots(const wchar_t* name)
    {
        return name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0));
    }

    struct LiveFile { unsigned long long size; FILETIME modified; DWORD attrs; bool dir; };

    struct Scan
    {
        const ScanOptions& opt;
        const std::vector<ShadowCopy>& shadows;
        const ScanProgress& progress;
        std::vector<PrevFileRecord>& out;
        bool cancelled = false;

        void report()
        {
            if (progress && !progress((int)out.size())) cancelled = true;
        }

        // folder is the full live path, e.g. C:\Users\Me\Documents.
        void walk(const std::wstring& folder, int level)
        {
            if (cancelled) return;

            // Gather the live folder's entries that match the wildcard.
            std::unordered_map<std::wstring, LiveFile> live;
            {
                WIN32_FIND_DATAW fd{};
                HANDLE h = ::FindFirstFileW((folder + L"\\" + opt.wildcard).c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (isDots(fd.cFileName)) continue;
                        std::wstring key = fd.cFileName;
                        std::transform(key.begin(), key.end(), key.begin(), towlower);
                        live[key] = { sizeOf(fd), fd.ftLastWriteTime, fd.dwFileAttributes,
                                      (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 };
                    } while (::FindNextFileW(h, &fd));
                    ::FindClose(h);
                }
            }

            std::wstring rel = folder.size() >= 2 ? folder.substr(2) : std::wstring();  // strip "X:"

            for (const auto& sc : shadows)
            {
                if (cancelled) return;
                if (opt.mode == 4 && _wcsicmp(sc.deviceObject.c_str(), opt.selectedShadow.c_str()) != 0)
                    continue;
                compareFolder(folder, rel, live, sc);
            }

            // Recurse into live subfolders, honouring the depth limit.
            if (!cancelled && (opt.depth == 0 || opt.depth > level + 1))
            {
                WIN32_FIND_DATAW fd{};
                HANDLE h = ::FindFirstFileW((folder + L"\\*").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        if (cancelled) break;
                        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !isDots(fd.cFileName))
                            walk(folder + L"\\" + fd.cFileName, level + 1);
                    } while (::FindNextFileW(h, &fd));
                    ::FindClose(h);
                }
            }
        }

        void compareFolder(const std::wstring& folder, const std::wstring& rel,
                            const std::unordered_map<std::wstring, LiveFile>& live, const ShadowCopy& sc)
        {
            std::wstring snapFolder = sc.deviceObject + rel;
            WIN32_FIND_DATAW fd{};
            HANDLE h = ::FindFirstFileW((snapFolder + L"\\" + opt.wildcard).c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;

            do
            {
                if (cancelled) break;
                if (isDots(fd.cFileName)) continue;
                bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

                std::wstring key = fd.cFileName;
                std::transform(key.begin(), key.end(), key.begin(), towlower);
                auto it = live.find(key);

                if (it != live.end())
                {
                    // Exists on the live drive. Only files become an older-version row.
                    if (!dir && opt.findPrevVersions)
                    {
                        const LiveFile& cur = it->second;
                        bool different = sizeOf(fd) != cur.size || !timesEqual(fd.ftLastWriteTime, cur.modified);
                        bool include = (opt.mode == 1) ? true : different;
                        if (include)
                        {
                            PrevFileRecord r;
                            r.filename = fd.cFileName;
                            r.folder = folder;
                            r.type = ItemType::OlderFileVersion;
                            r.hasCurrent = true;
                            r.currentModified = cur.modified;
                            r.currentSize = cur.size;
                            r.currentAttrs = cur.attrs;
                            r.previousModified = fd.ftLastWriteTime;
                            r.previousSize = sizeOf(fd);
                            r.previousAttrs = fd.dwFileAttributes;
                            r.shadowName = sc.deviceObject;
                            r.shadowTime = sc.creationTime;
                            out.push_back(std::move(r));
                            report();
                        }
                    }
                }
                else
                {
                    // Not on the live drive: a deleted file or folder.
                    if ((!dir && opt.findDeletedFiles) || (dir && opt.findDeletedFolders))
                    {
                        PrevFileRecord r;
                        r.filename = fd.cFileName;
                        r.folder = folder;
                        r.type = dir ? ItemType::DeletedFolder : ItemType::DeletedFile;
                        r.hasCurrent = false;
                        r.previousModified = fd.ftLastWriteTime;
                        r.previousSize = sizeOf(fd);
                        r.previousAttrs = fd.dwFileAttributes;
                        r.shadowName = sc.deviceObject;
                        r.shadowTime = sc.creationTime;
                        out.push_back(std::move(r));
                        report();
                    }
                }
            } while (::FindNextFileW(h, &fd));
            ::FindClose(h);
        }
    };

    // Mode 3 keeps only the newest previous version of each existing file.
    void keepLatestOnly(std::vector<PrevFileRecord>& rows)
    {
        std::map<std::wstring, size_t> best;   // folder\filename -> index of newest kept
        std::vector<PrevFileRecord> kept;
        for (auto& r : rows)
        {
            if (r.type != ItemType::OlderFileVersion) { kept.push_back(std::move(r)); continue; }
            std::wstring key = r.folder + L"\\" + r.filename;
            auto it = best.find(key);
            auto newer = [](const FILETIME& a, const FILETIME& b) {
                return ((unsigned long long)a.dwHighDateTime << 32 | a.dwLowDateTime) >
                       ((unsigned long long)b.dwHighDateTime << 32 | b.dwLowDateTime);
            };
            if (it == best.end()) { best[key] = kept.size(); kept.push_back(std::move(r)); }
            else if (newer(r.previousModified, kept[it->second].previousModified)) kept[it->second] = std::move(r);
        }
        rows.swap(kept);
    }
}

std::vector<PrevFileRecord> ScanPreviousFiles(const ScanOptions& opt, const ScanProgress& progress)
{
    std::vector<PrevFileRecord> out;

    // The base folder must be a local drive path (X:\...), as the original requires.
    if (opt.baseFolder.size() < 2 || opt.baseFolder[1] != L':') return out;

    std::vector<ShadowCopy> shadows = EnumerateShadowCopies(opt.baseFolder[0]);
    if (shadows.empty()) return out;

    std::wstring base = opt.baseFolder;
    while (base.size() > 3 && base.back() == L'\\') base.pop_back();  // keep "X:\" but trim trailing slashes

    Scan scan{ opt, shadows, progress, out };
    scan.walk(base, 0);

    if (opt.mode == 3) keepLatestOnly(out);
    return out;
}
