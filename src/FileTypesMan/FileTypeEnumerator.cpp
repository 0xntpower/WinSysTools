// FileTypeEnumerator.cpp -- reconstructs FileTypesMan's HKEY_CLASSES_ROOT scan.
//
// Mirrors, in order:
//   sub_14000291C   driver: extensions, SystemFileAssociations, Applications
//   sub_1400083FC   .ext keys + folder/directory/drive/* + Directory\Background + URL protocols
//   sub_140008170   SystemFileAssociations subkeys
//   sub_140007FC8   Applications subkeys
//   sub_140007BCC   per-type reader (extension -> ProgID -> class, CurVer + UserChoice)
//   sub_140007A80   class-key reader (Description, DefaultIcon, EditFlags, ...)
//   sub_140008A28   resolve the effective class key (UserChoice / Applications / SFA)
//   sub_140009578 / sub_140009230   the action (shell verb) scan
//   sub_14000902C   command-line -> process filename
//   sub_14001886C   registry key owner (LookupAccountSid -> "domain\name")
#include "FileType.h"
#include "FileAction.h"
#include "RegKey.h"
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "advapi32.lib")

namespace
{
    bool osIsVistaOrLater()
    {
        OSVERSIONINFOW vi{sizeof(vi)};
#pragma warning(push)
#pragma warning(disable : 4996)
        ::GetVersionExW(&vi);
#pragma warning(pop)
        return vi.dwMajorVersion > 5;
    }

    // sub_14001886C: the owner account of a registry key, "domain\name".
    std::wstring keyOwner(HKEY key)
    {
        static std::unordered_map<std::wstring, std::wstring> cache;
        BYTE sd[1060];
        DWORD cb = sizeof(sd);
        if (::RegGetKeySecurity(key, OWNER_SECURITY_INFORMATION, sd, &cb) != ERROR_SUCCESS)
        {
            return {};
        }
        PSID owner = nullptr;
        BOOL defaulted = FALSE;
        if (!::GetSecurityDescriptorOwner(sd, &owner, &defaulted) || !owner || !::IsValidSid(owner))
        {
            return {};
        }

        wchar_t sidStr[64] = {};
        if (LPWSTR s = nullptr; ::ConvertSidToStringSidW(owner, &s))
        {
            lstrcpynW(sidStr, s, 64);
            ::LocalFree(s);
        }
        if (sidStr[0])
        {
            auto it = cache.find(sidStr);
            if (it != cache.end())
            {
                return it->second;
            }
        }

        wchar_t name[128] = {}, domain[128] = {};
        DWORD cchName = 128, cchDomain = 128;
        SID_NAME_USE use;
        std::wstring result;
        if (::LookupAccountSidW(nullptr, owner, name, &cchName, domain, &cchDomain, &use))
        {
            result = std::wstring(domain) + L"\\" + name;
        }
        if (sidStr[0])
        {
            cache[sidStr] = result;
        }
        return result;
    }

    // Strip a ",<index>" suffix, expand environment variables -> a loadable file path.
    std::wstring iconToPath(std::wstring s)
    {
        if (s.empty())
        {
            return {};
        }
        if (s.front() == L'"')
        {
            size_t end = s.find(L'"', 1);
            s = (end == std::wstring::npos) ? s.substr(1) : s.substr(1, end - 1);
        }
        else
        {
            size_t comma = s.find_last_of(L',');
            if (comma != std::wstring::npos)
            {
                std::wstring tail = s.substr(comma + 1);
                bool numeric = !tail.empty();
                for (wchar_t c : tail)
                {
                    if (c != L'-' && c != L' ' && !iswdigit(c))
                    {
                        numeric = false;
                        break;
                    }
                }
                if (numeric)
                {
                    s.resize(comma);
                }
            }
        }
        wchar_t expanded[1024];
        if (::ExpandEnvironmentStringsW(s.c_str(), expanded, 1024))
        {
            return expanded;
        }
        return s;
    }

    std::wstring verString(const unsigned char* block, const std::wstring& langcp, const wchar_t* field)
    {
        std::wstring sub = L"\\StringFileInfo\\" + langcp + L"\\" + field;
        LPVOID p = nullptr;
        UINT len = 0;
        if (::VerQueryValueW(block, sub.c_str(), &p, &len) && p && len)
        {
            return std::wstring(static_cast<wchar_t*>(p), len > 0 ? len - 1 : 0);
        }
        return {};
    }

    // Pull Product Name / FileVersion / FileDescription / CompanyName from a file
    // (columns 15-18). The icon/command file supplies the version info (readme v1.50).
    void readVersionInfo(const std::wstring& iconOrCommand, std::wstring& productName, std::wstring& productVersion,
                         std::wstring& productDescription, std::wstring& companyName)
    {
        std::wstring path = iconToPath(iconOrCommand);
        if (path.empty())
        {
            return;
        }
        // Resolve a bare filename such as "cryptui.dll" against the system directory, then
        // the Windows directory, as the original does.
        if (path.find_first_of(L"\\/") == std::wstring::npos)
        {
            static std::wstring sysDir = []
            {
                wchar_t d[MAX_PATH];
                return std::wstring(d, ::GetSystemDirectoryW(d, MAX_PATH));
            }();
            static std::wstring winDir = []
            {
                wchar_t d[MAX_PATH];
                return std::wstring(d, ::GetWindowsDirectoryW(d, MAX_PATH));
            }();
            std::wstring inSys = sysDir + L"\\" + path;
            std::wstring inWin = winDir + L"\\" + path;
            if (::GetFileAttributesW(inSys.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                path = inSys;
            }
            else if (::GetFileAttributesW(inWin.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                path = inWin;
            }
        }
        if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            return;
        }
        DWORD handle = 0, size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
        if (!size)
        {
            return;
        }
        std::vector<unsigned char> block(size);
        if (!::GetFileVersionInfoW(path.c_str(), 0, size, block.data()))
        {
            return;
        }

        std::wstring langcp = L"040904B0";
        struct
        {
            WORD lang, cp;
        }* tr = nullptr;
        UINT trLen = 0;
        if (::VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&tr), &trLen) &&
            tr && trLen >= 4)
        {
            wchar_t b[16];
            wsprintfW(b, L"%04X%04X", tr->lang, tr->cp);
            langcp = b;
        }
        productName = verString(block.data(), langcp, L"ProductName");
        productVersion = verString(block.data(), langcp, L"FileVersion");
        productDescription = verString(block.data(), langcp, L"FileDescription");
        companyName = verString(block.data(), langcp, L"CompanyName");
    }

    // sub_140007A80: read the class-key fields that feed the type columns. String values
    // are kept unchanged when the source value is missing, so a UserChoice override only
    // replaces what it actually defines (matching RegQueryValueExW leaving the buffer).
    void readClassFields(const RegKey& cls, FileType& ft)
    {
        if (auto v = cls.queryFlags(L"EditFlags"))
        {
            ft.editFlags = *v;
        }
        if (auto v = cls.queryFlags(L"BrowserFlags"))
        {
            ft.browserFlags = *v;
        }
        ft.alwaysShowExt = cls.hasValue(L"AlwaysShowExt");
        ft.excluded = (ft.editFlags & 0x1) != 0; // FTA_Exclude
        if (auto d = cls.queryString(L""))
        {
            ft.description = *d;
        }
        RegKey icon;
        if (icon.open(cls.get(), L"DefaultIcon"))
        {
            if (auto v = icon.queryString(L""))
            {
                ft.defaultIcon = *v;
            }
        }
        RegKey ih;
        if (ih.open(cls.get(), L"ShellEx\\IconHandler"))
        {
            if (auto v = ih.queryString(L""))
            {
                ft.iconHandlerClsid = *v;
            }
        }
    }

    // sub_140007BCC: fill a record for one extension / pseudo type key.
    void readExtensionType(const std::wstring& name, FileType& ft, bool loadVersion, bool loadOwner)
    {
        RegKey key;
        if (!key.open(HKEY_CLASSES_ROOT, name))
        {
            return;
        }

        ft.key = name;
        ft.regSubKey = name;
        ft.mimeType = key.queryStringOr(L"Content Type");
        ft.perceivedType = key.queryStringOr(L"PerceivedType");
        if (auto t = key.lastWriteTime())
        {
            ft.modifiedTime = *t;
            ft.hasModified = true;
        }
        if (loadOwner)
        {
            ft.regKeyOwner = keyOwner(key.get());
        }
        // "In New Menu" is set only when ShellNew carries a NullFile value (sub_140007BCC).
        RegKey shellNew;
        ft.inNewMenu = shellNew.open(key.get(), L"ShellNew") && shellNew.hasValue(L"NullFile");

        bool isExt = !name.empty() && name.front() == L'.';
        if (isExt)
        {
            ft.progId = key.queryDefault();
            if (osIsVistaOrLater())
            {
                RegKey uc;
                if (uc.open(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\" +
                                                   name + L"\\UserChoice"))
                {
                    ft.userChoice = uc.queryStringOr(L"Progid");
                }
            }
        }
        ft.typeKey = isExt ? ft.progId : name;

        // CurVer redirect on the ProgID (the Type Name shows the resolved class).
        if (isExt && !ft.progId.empty())
        {
            RegKey pk;
            if (pk.open(HKEY_CLASSES_ROOT, ft.progId))
            {
                std::wstring cv = pk.subValue(L"CurVer", L"");
                if (!cv.empty() && _wcsicmp(cv.c_str(), ft.progId.c_str()) != 0)
                {
                    ft.progId = cv;
                    ft.typeKey = cv;
                }
            }
        }

        // Class fields come from the ProgID (extension), or from the key itself when the
        // extension has no ProgID or it is a pseudo type. A UserChoice overrides them only
        // when its class key actually opens, matching the original fall-back behaviour.
        RegKey baseCls;
        std::wstring baseSub = (isExt && !ft.typeKey.empty()) ? ft.typeKey : name;
        if (!baseSub.empty() && baseCls.open(HKEY_CLASSES_ROOT, baseSub))
        {
            readClassFields(baseCls, ft);
        }
        if (isExt && !ft.userChoice.empty())
        {
            RegKey uc;
            if (uc.open(HKEY_CLASSES_ROOT, ft.userChoice))
            {
                readClassFields(uc, ft);
            }
        }

        if (loadVersion && !ft.defaultIcon.empty())
        {
            readVersionInfo(ft.defaultIcon, ft.productName, ft.productVersion, ft.productDescription, ft.companyName);
        }
    }
} // namespace

std::wstring FileType::formatTime(const FILETIME& ft)
{
    FILETIME local{};
    SYSTEMTIME st{};
    if (!::FileTimeToLocalFileTime(&ft, &local) || !::FileTimeToSystemTime(&local, &st))
    {
        return {};
    }
    if (st.wYear <= 1900)
    {
        return {};
    }
    wchar_t date[64] = {}, time[64] = {};
    ::GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 64);
    ::GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, nullptr, time, 64);
    return std::wstring(date) + L" " + time;
}

std::vector<FileType> EnumerateFileTypes(const EnumOptions& opt)
{
    std::vector<FileType> out;
    RegKey hcr;
    if (!hcr.open(HKEY_CLASSES_ROOT, L""))
    {
        return out;
    }

    auto isPseudo = [](const std::wstring& n)
    {
        return _wcsicmp(n.c_str(), L"folder") == 0 || _wcsicmp(n.c_str(), L"directory") == 0 ||
               _wcsicmp(n.c_str(), L"drive") == 0 || _wcsicmp(n.c_str(), L"*") == 0;
    };

    // --- sub_1400083FC: extensions, pseudo types, Directory\Background, URL protocols ---
    for (const std::wstring& name : hcr.subKeys())
    {
        bool dot = !name.empty() && name.front() == L'.';
        if (dot || isPseudo(name))
        {
            RegKey k;
            if (k.open(HKEY_CLASSES_ROOT, name))
            {
                FileType ft;
                readExtensionType(name, ft, opt.loadVersionInfo, opt.loadOwner);
                out.push_back(std::move(ft));
                if (_wcsicmp(name.c_str(), L"directory") == 0 &&
                    RegKey().open(HKEY_CLASSES_ROOT, L"Directory\\Background"))
                {
                    FileType bg;
                    readExtensionType(L"Directory\\Background", bg, opt.loadVersionInfo, opt.loadOwner);
                    out.push_back(std::move(bg));
                }
            }
        }
        if (name.find(L'.') == std::wstring::npos && !isPseudo(name))
        {
            RegKey k;
            if (k.open(HKEY_CLASSES_ROOT, name) && k.hasValue(L"URL Protocol"))
            {
                FileType ft;
                readExtensionType(name, ft, opt.loadVersionInfo, opt.loadOwner);
                ft.group = TypeGroup::UrlProtocol;
                if (opt.showUrlProtocols)
                {
                    out.push_back(std::move(ft));
                }
            }
        }
    }

    // --- sub_140008170: SystemFileAssociations ---
    // Only the pure perceived-type groupings are shown (subkeys with no dot at all, such
    // as audio / image / text). They carry the name in the Type Name column with an empty
    // Extension. The ".ext" subkeys are skipped, since the HKCR extension row covers them.
    // These rows read no owner (the original does not query key security here).
    if (RegKey sfa; sfa.open(HKEY_CLASSES_ROOT, L"SystemFileAssociations"))
    {
        for (const std::wstring& name : sfa.subKeys())
        {
            if (name.find(L'.') != std::wstring::npos)
            {
                continue;
            }
            RegKey k;
            if (!k.open(sfa.get(), name))
            {
                continue;
            }
            FileType ft;
            ft.systemFileAssoc = true;
            ft.progId = name;
            ft.group = TypeGroup::PerceivedType;
            ft.regSubKey = L"SystemFileAssociations\\" + name;
            ft.typeKey = ft.regSubKey;
            if (auto t = k.lastWriteTime())
            {
                ft.modifiedTime = *t;
                ft.hasModified = true;
            }
            readClassFields(k, ft);
            if (opt.loadVersionInfo && !ft.defaultIcon.empty())
            {
                readVersionInfo(ft.defaultIcon, ft.productName, ft.productVersion, ft.productDescription,
                                ft.companyName);
            }
            out.push_back(std::move(ft));
        }
    }

    // --- sub_140007FC8: Applications ---
    if (opt.showApplications)
    {
        if (RegKey apps; apps.open(HKEY_CLASSES_ROOT, L"Applications"))
        {
            for (const std::wstring& name : apps.subKeys())
            {
                RegKey appKey;
                if (!appKey.open(apps.get(), name))
                {
                    continue;
                }
                if (!RegKey().open(appKey.get(), L"Shell"))
                {
                    continue; // only apps with a Shell key
                }
                FileType ft;
                ft.progId = name; // Type Name
                ft.group = TypeGroup::Application;
                ft.regSubKey = L"Applications\\" + name;
                ft.typeKey = ft.regSubKey;
                if (auto t = appKey.lastWriteTime())
                {
                    ft.modifiedTime = *t;
                    ft.hasModified = true;
                }
                readClassFields(appKey, ft); // Description = app key default value, no owner read
                if (opt.loadVersionInfo && !ft.defaultIcon.empty())
                {
                    readVersionInfo(ft.defaultIcon, ft.productName, ft.productVersion, ft.productDescription,
                                    ft.companyName);
                }
                out.push_back(std::move(ft));
            }
        }
    }

    return out;
}

// ----------------------------------------------------------------- actions ------
namespace
{
    int findSub(const std::wstring& hay, const std::wstring& needle)
    {
        auto p = hay.find(needle);
        return p == std::wstring::npos ? -1 : static_cast<int>(p);
    }

    // sub_14000902C: command-line -> the executable / dll filename.
    std::wstring commandToFilename(std::wstring cmd)
    {
        int r = findSub(cmd, L"rundll32.exe ");
        if (r >= 0)
        {
            return commandToFilename(cmd.substr(r + 13));
        }

        std::wstring file;
        if (!cmd.empty() && cmd.front() == L'"')
        {
            size_t end = cmd.find(L'"', 1);
            if (end != std::wstring::npos)
            {
                file = cmd.substr(1, end - 1);
            }
        }
        else
        {
            int dot = findSub(cmd, L".exe");
            if (dot == -1)
            {
                dot = findSub(cmd, L".dll");
            }
            if (dot >= 0)
            {
                file = cmd.substr(0, dot + 4);
            }
            else
            {
                int comma = findSub(cmd, L",");
                if (comma >= 0)
                {
                    file = cmd.substr(0, comma);
                    if (file.find(L'.') == std::wstring::npos)
                    {
                        file += L".dll";
                    }
                }
                else
                {
                    file = cmd;
                }
            }
        }
        if (file.empty())
        {
            return {};
        }
        wchar_t expanded[1024];
        if (::ExpandEnvironmentStringsW(file.c_str(), expanded, 1024))
        {
            file = expanded;
        }
        // A bare filename is resolved against the system directory (sub_14000902C's
        // trailing step), even when the file does not actually live there.
        if (file.find_first_of(L"\\/") == std::wstring::npos)
        {
            wchar_t sys[MAX_PATH];
            if (UINT n = ::GetSystemDirectoryW(sys, MAX_PATH))
            {
                file = std::wstring(sys, n) + L"\\" + file;
            }
        }
        return file;
    }

    // Resolve an "@file,-id" caption from its resource module (ExtractResourceCaption).
    std::wstring extractResourceCaption(const std::wstring& caption)
    {
        if (caption.empty() || caption.front() != L'@')
        {
            return caption;
        }
        size_t comma = caption.find_last_of(L',');
        if (comma == std::wstring::npos)
        {
            return caption;
        }
        int id = _wtoi(caption.c_str() + comma + 1);
        std::wstring file = caption.substr(1, comma - 1);
        wchar_t path[1024];
        if (::ExpandEnvironmentStringsW(file.c_str(), path, 1024))
        {
            file = path;
        }
        HMODULE mod = ::GetModuleHandleW(file.c_str());
        bool loaded = false;
        if (!mod)
        {
            mod = ::LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
            loaded = true;
        }
        std::wstring result = caption;
        if (mod)
        {
            wchar_t buf[256] = {};
            if (::LoadStringW(mod, static_cast<UINT>(-id), buf, 256) > 0)
            {
                result = buf;
            }
            if (loaded)
            {
                ::FreeLibrary(mod);
            }
        }
        return result;
    }

    // sub_140009230: read one action record from shell\<verb>.
    void readAction(const RegKey& shell, const std::wstring& defaultVerb, const std::wstring& verb,
                    const std::wstring& shellKeyPath, FileAction& a, bool extractCaptions, bool loadVersion,
                    bool loadOwner)
    {
        RegKey vk;
        if (!vk.open(shell.get(), verb))
        {
            return;
        }
        a.name = verb;
        a.isDefault = _wcsicmp(verb.c_str(), defaultVerb.c_str()) == 0;
        if (loadOwner)
        {
            a.regKeyOwner = keyOwner(vk.get());
        }
        a.disabled = vk.hasValue(L"LegacyDisable");
        a.extended = vk.hasValue(L"Extended");
        a.caption = vk.queryDefault();
        a.icon = vk.queryStringOr(L"Icon");
        if (a.caption.empty())
        {
            a.caption = verb;
        }
        else if (extractCaptions)
        {
            a.caption = extractResourceCaption(a.caption);
        }
        a.registryKey = shellKeyPath + L"\\" + verb;
        if (auto t = vk.lastWriteTime())
        {
            a.modifiedTime = *t;
            a.hasModified = true;
        }

        if (RegKey dde; dde.open(vk.get(), L"ddeexec"))
        {
            a.ddeMessage = dde.queryDefault();
            a.ddeApplication = dde.subValue(L"application", L"");
            a.ddeTopic = dde.subValue(L"topic", L"");
            a.ddeIfexec = dde.subValue(L"ifexec", L"");
        }
        if (RegKey cmd; cmd.open(vk.get(), L"command"))
        {
            if (auto t = cmd.lastWriteTime())
            {
                if (!a.hasModified || ::CompareFileTime(&*t, &a.modifiedTime) == 1)
                {
                    a.modifiedTime = *t;
                    a.hasModified = true;
                }
            }
            a.commandLine = cmd.queryDefault();
            a.processFilename = commandToFilename(a.commandLine);
        }
        if (loadVersion && !a.processFilename.empty())
        {
            readVersionInfo(a.processFilename, a.productName, a.productVersion, a.productDescription, a.companyName);
        }
    }

    // sub_140009578: enumerate the verbs under one shell key into records.
    void scanShell(std::vector<FileAction>& out, const std::wstring& classSub, bool systemFileAssoc,
                   bool extractCaptions, bool loadVersion)
    {
        RegKey shell;
        if (!shell.open(HKEY_CLASSES_ROOT, classSub + L"\\shell"))
        {
            return;
        }
        std::wstring defaultVerb = shell.queryDefault();
        std::wstring shellPath = L"HKEY_CLASSES_ROOT\\" + classSub + L"\\shell";
        for (const std::wstring& verb : shell.subKeys())
        {
            FileAction a;
            a.systemFileAssoc = systemFileAssoc;
            a.regSubKey = classSub + L"\\shell\\" + verb;
            readAction(shell, defaultVerb, verb, shellPath, a, extractCaptions, loadVersion, true);
            // The Registry Key column points at the verb's Command key ("%s\\Shell\\%s\\Command").
            a.registryKey = L"HKEY_CLASSES_ROOT\\" + classSub + L"\\Shell\\" + verb + L"\\Command";
            out.push_back(std::move(a));
        }
    }
} // namespace

std::vector<FileAction> EnumerateActions(const FileType& type, bool extractCaptions, bool loadVersionInfo)
{
    std::vector<FileAction> out;

    // Effective class key the shell verbs live under (UserChoice / Applications / SFA / ProgID),
    // with the same fall-back to the ProgID when a UserChoice target is missing.
    std::wstring classSub;
    if (type.systemFileAssoc)
    {
        classSub = type.typeKey; // "SystemFileAssociations\\name"
    }
    else if (type.group == TypeGroup::Application)
    {
        classSub = type.typeKey; // "Applications\\name"
    }
    else if (!type.userChoice.empty() && RegKey().open(HKEY_CLASSES_ROOT, type.userChoice))
    {
        classSub = type.userChoice;
    }
    else if (!type.typeKey.empty() && !type.key.empty() && type.key.front() == L'.')
    {
        classSub = type.typeKey;
    }
    else
    {
        classSub = type.key;
    }

    scanShell(out, classSub, type.systemFileAssoc, extractCaptions, loadVersionInfo);

    // Actions also defined under SystemFileAssociations\<ext> for a normal extension.
    if (!type.systemFileAssoc && !type.key.empty() && type.key.front() == L'.')
    {
        scanShell(out, L"SystemFileAssociations\\" + type.key, true, extractCaptions, loadVersionInfo);
    }

    return out;
}
