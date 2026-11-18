// PsInfo sub-reports: the hotfix sources (-h), the installed-software scan (-s),
// and the disk-volume walk (-d) with their size formatting. See analysis/PsInfo.md
// for the sub_ map.
#pragma once

#include "PsInfoUtil.h"

namespace psinfo
{
    inline bool HotfixKnown(const std::vector<Hotfix>& list, const std::wstring& name)
    {
        for (const auto& h : list)
        {
            if (h.name.find(name) != std::wstring::npos)
            {
                return true;
            }
        }
        return false;
    }

    // Recurse Software\Microsoft\Updates: a key with a "Description" value is one
    // hotfix named by that description, a key without it is a container to recurse
    // into. Mirrors sub_140007730.
    inline void CollectUpdates(HKEY root, const std::wstring& path, std::vector<Hotfix>& out)
    {
        RegKey key;
        if (!key.Open(root, path.c_str(), kRead))
        {
            return;
        }
        wchar_t name[264] = {};
        for (DWORD i = 0; ::RegEnumKeyW(key.key, i, name, 0x208) == ERROR_SUCCESS; ++i)
        {
            RegKey sub;
            if (!sub.Open(key.key, name, kRead))
            {
                continue;
            }
            DWORD cb = 0;
            if (::RegQueryValueExW(sub.key, L"Description", nullptr, nullptr, nullptr, &cb) != ERROR_SUCCESS)
            {
                CollectUpdates(root, path + L"\\" + name, out);
                continue;
            }
            Hotfix h;
            h.name = ReadString(sub.key, L"Description");
            ::RegQueryInfoKeyW(sub.key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, &h.written);
            out.push_back(std::move(h));
        }
    }

    // Enumerate the Hotfix key subkeys, skipping any name already present as a
    // substring of a collected entry. Mirrors the Hotfix branch of sub_140004E70.
    inline void CollectHotfixKeys(HKEY root, std::vector<Hotfix>& out)
    {
        RegKey key;
        if (!key.Open(root, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Hotfix", kRead))
        {
            return;
        }
        wchar_t name[264] = {};
        for (DWORD i = 0; ::RegEnumKeyW(key.key, i, name, 0x208) == ERROR_SUCCESS; ++i)
        {
            if (HotfixKnown(out, name))
            {
                continue;
            }
            RegKey sub;
            if (!sub.Open(key.key, name, kRead))
            {
                continue;
            }
            Hotfix h;
            h.name = name;
            ::RegQueryInfoKeyW(sub.key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, &h.written);
            out.push_back(std::move(h));
        }
    }

    // Split Internet Settings MinorVersion into semicolon separated tokens and add
    // one "Internet Explorer - token" entry per token. Mirrors sub_140007520.
    inline void CollectIeVersions(HKEY root, std::vector<Hotfix>& out)
    {
        RegKey key;
        if (!key.Open(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", kRead))
        {
            return;
        }
        const std::wstring value = ReadString(key.key, L"MinorVersion");
        size_t start = 0;
        while (start < value.size())
        {
            const size_t semi = value.find(L';', start);
            const std::wstring token =
                value.substr(start, semi == std::wstring::npos ? std::wstring::npos : semi - start);
            if (!token.empty())
            {
                Hotfix h;
                h.name = L"Internet Explorer - " + token;
                out.push_back(std::move(h));
            }
            if (semi == std::wstring::npos)
            {
                break;
            }
            start = semi + 1;
        }
    }

    // Enumerate the Uninstall key and keep the applications sorted ascending by
    // display name with an ordinal compare, inserting before the first element
    // that is not less than the new one, as the original linked list does.
    inline void CollectSoftware(HKEY root, std::vector<Application>& out)
    {
        RegKey key;
        if (!key.Open(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", kRead))
        {
            return;
        }
        wchar_t name[264] = {};
        for (DWORD i = 0; ::RegEnumKeyW(key.key, i, name, 0x208) == ERROR_SUCCESS; ++i)
        {
            RegKey sub;
            if (::RegOpenKeyW(key.key, name, &sub.key) != ERROR_SUCCESS)
            {
                continue;
            }
            DWORD cb = 0;
            if (::RegQueryValueExW(sub.key, L"DisplayName", nullptr, nullptr, nullptr, &cb) != ERROR_SUCCESS)
            {
                continue;
            }
            Application app;
            app.name = ReadString(sub.key, L"DisplayName");
            app.version = ReadString(sub.key, L"DisplayVersion");
            const auto pos = std::lower_bound(out.begin(), out.end(), app,
                                              [](const Application& a, const Application& b)
                                              {
                                                  return ::wcscmp(a.name.c_str(), b.name.c_str()) < 0;
                                              });
            out.insert(pos, std::move(app));
        }
    }

    inline const wchar_t* DriveTypeName(UINT type)
    {
        switch (type)
        {
        case DRIVE_NO_ROOT_DIR:
            return L"No Root";
        case DRIVE_REMOVABLE:
            return L"Removable";
        case DRIVE_FIXED:
            return L"Fixed";
        case DRIVE_REMOTE:
            return L"Remote";
        case DRIVE_CDROM:
            return L"CD-ROM";
        case DRIVE_RAMDISK:
            return L"RAM Disk";
        default:
            return L"Unknown";
        }
    }

    inline void CollectVolumes(std::vector<Volume>& out)
    {
        const UINT previous = ::SetErrorMode(SEM_FAILCRITICALERRORS);
        const DWORD drives = ::GetLogicalDrives();
        wchar_t root[4] = L"A:\\";
        for (int i = 0; i < 26; ++i)
        {
            if ((drives & (1u << i)) == 0)
            {
                continue;
            }
            root[0] = static_cast<wchar_t>(L'A' + i);
            Volume v;
            v.letter = root[0];
            wchar_t label[MAX_PATH] = {};
            wchar_t fs[32] = {};
            ::GetVolumeInformationW(root, label, MAX_PATH, nullptr, nullptr, nullptr, fs, 32);
            v.label = label;
            v.format = fs;
            v.type = ::GetDriveTypeW(root);
            ULARGE_INTEGER freeToCaller = {};
            ULARGE_INTEGER total = {};
            ULARGE_INTEGER totalFree = {};
            ::GetDiskFreeSpaceExW(root, &freeToCaller, &total, &totalFree);
            v.total = total.QuadPart;
            v.free = totalFree.QuadPart;
            out.push_back(std::move(v));
        }
        ::SetErrorMode(previous);
    }

    [[nodiscard]] inline std::wstring SizeString(unsigned long long bytes)
    {
        wchar_t buf[32] = {};
        if (bytes > 0x40000000ULL)
        {
            ::swprintf(buf, std::size(buf), L"%2.2f GB", static_cast<double>(bytes) / 1073741824.0);
        }
        else if (bytes > 0x100000ULL)
        {
            ::swprintf(buf, std::size(buf), L"%3.2f MB", static_cast<double>(bytes) / 1048576.0);
        }
        else if (bytes != 0)
        {
            ::swprintf(buf, std::size(buf), L"%3.2f KB", static_cast<double>(bytes) / 1024.0);
        }
        return buf;
    }
} // namespace psinfo
