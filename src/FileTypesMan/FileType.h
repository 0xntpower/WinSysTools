// FileType.h -- one upper-pane record: a file extension or registered type read from
// HKEY_CLASSES_ROOT. Fields mirror the record the enumerator fills (sub_140007BCC +
// sub_140007A80). column() reproduces the original getter: the field index matches the
// kTypeColumns order.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum class TypeGroup { Standard = 0, PerceivedType = 1, UrlProtocol = 2, Application = 3 };

struct FileType
{
    std::wstring key;            // HKCR subkey name (".txt", "folder", ...)  -> col 0 Extension
    std::wstring progId;         // extension -> ProgID / class               -> col 1 Type Name
    std::wstring description;    // class key default value                   -> col 2 Description
    std::wstring mimeType;       // "Content Type"                            -> col 3
    std::wstring perceivedType;  // "PerceivedType"                           -> col 4
    bool  inNewMenu = false;     // ShellNew present                          -> col 5
    bool  excluded = false;      // EditFlags FTA_Exclude bit (0x1)           -> col 6
    bool  alwaysShowExt = false; // "AlwaysShowExt"                           -> col 7
    DWORD editFlags = 0;         // "EditFlags"                               -> col 8
    DWORD browserFlags = 0;      // "BrowserFlags"                            -> col 9
    std::wstring defaultIcon;    // "DefaultIcon"                             -> col 10
    FILETIME modifiedTime{};     // key last-write time                       -> col 11
    bool hasModified = false;
    TypeGroup group = TypeGroup::Standard;                                  // col 12
    std::wstring userChoice;     // UserChoice ProgID (Vista+)                -> col 13
    std::wstring iconHandlerClsid; // "ShellEx\IconHandler"                   -> col 14
    std::wstring productName, productVersion, productDescription, companyName; // cols 15-18
    std::wstring regKeyOwner;    // key security owner                        -> col 19

    // Registry location of the type (for RegEdit jump and writes).
    HKEY         root = HKEY_CLASSES_ROOT;
    std::wstring regSubKey;      // e.g. ".txt", "Applications\\x.exe", "SystemFileAssociations\\.y"
    std::wstring typeKey;        // the class key the actions live under (progId or key itself)
    bool         systemFileAssoc = false;

    static std::wstring formatTime(const FILETIME& ft);
    static const wchar_t* groupName(TypeGroup g)
    {
        switch (g)
        {
        case TypeGroup::PerceivedType: return L"Perceived Type";
        case TypeGroup::UrlProtocol:   return L"URL Protocol";
        case TypeGroup::Application:   return L"Application";
        default:                       return L"Standard";
        }
    }
    static std::wstring hex(DWORD v)
    {
        wchar_t b[16];
        wsprintfW(b, L"0x%08x", v);
        return b;
    }

    std::wstring registryKey() const { return L"HKEY_CLASSES_ROOT\\" + regSubKey; }

    std::wstring column(int col) const
    {
        switch (col)
        {
        case 0:  return key;
        case 1:  return progId;
        case 2:  return description;
        case 3:  return mimeType;
        case 4:  return perceivedType;
        case 5:  return inNewMenu ? L"*" : L"";
        case 6:  return excluded ? L"*" : L"";
        case 7:  return alwaysShowExt ? L"*" : L"";
        case 8:  return hex(editFlags);
        case 9:  return hex(browserFlags);
        case 10: return defaultIcon;
        case 11: return hasModified ? formatTime(modifiedTime) : std::wstring();
        case 12: return groupName(group);
        case 13: return userChoice;
        case 14: return iconHandlerClsid;
        case 15: return productName;
        case 16: return productVersion;
        case 17: return productDescription;
        case 18: return companyName;
        case 19: return regKeyOwner;
        default: return {};
        }
    }
};

// Enumerate every file type / extension under HKEY_CLASSES_ROOT, mirroring
// sub_14000291C: sub_1400083FC (extensions, folder/directory/drive/*, URL protocols),
// sub_140008170 (SystemFileAssociations) and sub_140007FC8 (Applications).
// showUrlProtocols/showApplications/extractCaptions gate the same categories the
// options do. loadVersionInfo pulls the Product* columns from the default-icon file.
struct EnumOptions
{
    bool showUrlProtocols = false;   // option default 0
    bool showApplications = true;    // option default 1
    bool loadVersionInfo = true;
    bool loadOwner = true;
};
std::vector<FileType> EnumerateFileTypes(const EnumOptions& opt);
