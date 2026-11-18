// MimeType.h -- one row of the MIMEView list, reconstructed from the record that
// the original builds in sub_40391E / sub_40150B / sub_401614. Fields are named
// after the "Choose Columns" titles recovered from the binary.
#pragma once
#include <string>
#include <vector>

struct MimeType
{
    std::wstring contentType;    // col 0  subkey under HKCR\MIME\Database\Content Type
    std::wstring extension;      // col 1  "Extension" value
    std::wstring pluginName;     // col 2  default value of HKCR\CLSID\<clsid>
    std::wstring pluginVersion;  // col 3  HKCR\CLSID\<clsid>\Version
    std::wstring pluginProgId;   // col 4  HKCR\CLSID\<clsid>\ProgID
    std::wstring pluginFile;     // col 5  HKCR\CLSID\<clsid>\InProcServer32
    std::wstring clsid;          // col 6  "CLSID" value
    bool         fileDialog = true; // col 7  !(ProgID EditFlags & FTA_OpenIsSafe 0x10000)
    std::wstring openCommand;    // col 8  ext -> ProgID  shell\open\command
    std::wstring defaultIcon;    // (row icon only) ext -> ProgID  DefaultIcon

    // Display text for a visible column index, mirroring sub_4016AC's switch.
    std::wstring column(int col) const
    {
        switch (col)
        {
        case 0:  return contentType;
        case 1:  return extension;
        case 2:  return pluginName;
        case 3:  return pluginVersion;
        case 4:  return pluginProgId;
        case 5:  return pluginFile;
        case 6:  return clsid;
        case 7:  return fileDialog ? L"Yes" : L"No";
        case 8:  return openCommand;
        default: return {};
        }
    }
};

[[nodiscard]] std::vector<MimeType> EnumerateMimeTypes();
