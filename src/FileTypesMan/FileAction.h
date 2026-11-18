// FileAction.h -- one lower-pane record: a shell verb (action) of the selected type,
// read from <typekey>\shell\<verb> and its command / ddeexec subkeys. Mirrors the record
// filled by sub_140009230. column() matches the kActionColumns order.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "FileType.h"

struct FileAction
{
    std::wstring name;       // verb subkey name                 -> col 0
    std::wstring caption;    // verb default value (menu text)   -> col 1
    bool isDefault = false;  // verb == shell default            -> col 2
    bool disabled = false;   // "LegacyDisable" present          -> col 3
    FILETIME modifiedTime{}; // verb/command last-write time     -> col 4
    bool hasModified = false;
    std::wstring commandLine;     // shell\<verb>\command default     -> col 5
    std::wstring icon;            // "Icon" value                     -> col 6
    std::wstring ddeMessage;      // ddeexec default                  -> col 7
    std::wstring ddeApplication;  // ddeexec\application              -> col 8
    std::wstring ddeIfexec;       // ddeexec\ifexec                   -> col 9
    std::wstring ddeTopic;        // ddeexec\topic                    -> col 10
    std::wstring processFilename; // resolved exe/dll from command    -> col 11
    bool systemFileAssoc = false; // action under SystemFileAssociations -> col 12
    std::wstring registryKey;     // full key path                    -> col 13
    bool extended = false;        // "Extended" present               -> col 14
    std::wstring productName, productVersion, productDescription, companyName; // 15-18
    std::wstring regKeyOwner; // key security owner               -> col 19

    HKEY root = HKEY_CLASSES_ROOT;
    std::wstring regSubKey; // relative key path of the verb (for writes)

    std::wstring column(int col) const
    {
        switch (col)
        {
        case 0:
            return name;
        case 1:
            return caption;
        case 2:
            return isDefault ? L"Yes" : L"No";
        case 3:
            return disabled ? L"Yes" : L"No";
        case 4:
            return hasModified ? FileType::formatTime(modifiedTime) : std::wstring();
        case 5:
            return commandLine;
        case 6:
            return icon;
        case 7:
            return ddeMessage;
        case 8:
            return ddeApplication;
        case 9:
            return ddeIfexec;
        case 10:
            return ddeTopic;
        case 11:
            return processFilename;
        case 12:
            return systemFileAssoc ? L"Yes" : L"No";
        case 13:
            return registryKey;
        case 14:
            return extended ? L"Yes" : L"No";
        case 15:
            return productName;
        case 16:
            return productVersion;
        case 17:
            return productDescription;
        case 18:
            return companyName;
        case 19:
            return regKeyOwner;
        default:
            return {};
        }
    }
};

// Enumerate every action (shell verb) of a file type, mirroring sub_140009578:
// walk <typeKey>\shell\<verb>, plus SystemFileAssociations\<key>\shell when present.
std::vector<FileAction> EnumerateActions(const FileType& type, bool extractCaptions, bool loadVersionInfo);
