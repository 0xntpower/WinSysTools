// IconEntry.h -- one found icon- or cursor-group, reconstructed from the 296-byte
// record the original builds in the EnumResourceNames callback (sub_404A01) and
// formats per column in sub_40423B. Fields are named after the recovered columns.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct IconEntry
{
    bool         isCursor = false;   // record+276: 1 = Icon, 2 = Cursor
    int          resId = 0;          // record+0  : numeric resource id (0 when named)
    std::wstring resName;            // record+284: resource name (string names only)
    int          images = 0;         // record+4  : image count (GRPICONDIR idCount)
    unsigned     totalSize = 0;      // record+292: sum of the group's dwBytesInRes
    std::wstring filename;           // record+8  : full path of the source file
    int          largeImage = -1;    // 32x32 image-list index
    int          smallImage = -1;    // 16x16 image-list index

    // The MAKEINTRESOURCE-style name used for FindResource during a save/extract.
    // A named resource keeps its string, a numeric one uses MAKEINTRESOURCE(resId).
    const wchar_t* resourceId() const
    {
        return resName.empty() ? MAKEINTRESOURCEW(resId) : resName.c_str();
    }

    // Column 0 "Resource": "basename(id)" or "basename(name)" (sub_4013E4).
    std::wstring resourceField() const
    {
        std::wstring base = baseName(filename);
        if (resName.empty())
            return base + L"(" + std::to_wstring(resId) + L")";
        return base + L"(" + resName + L")";
    }

    std::wstring typeField() const { return isCursor ? L"Cursor" : L"Icon"; }

    static std::wstring baseName(const std::wstring& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        return slash == std::wstring::npos ? path : path.substr(slash + 1);
    }

    // Decimal grouped size, e.g. 12345 -> "12,345" (sub_402C7E / GetNumberFormat).
    static std::wstring groupedNumber(unsigned value)
    {
        std::wstring digits = std::to_wstring(value), out;
        int c = 0;
        for (auto it = digits.rbegin(); it != digits.rend(); ++it)
        {
            if (c && c % 3 == 0) out.push_back(L',');
            out.push_back(*it);
            ++c;
        }
        return std::wstring(out.rbegin(), out.rend());
    }

    std::wstring column(int col) const
    {
        switch (col)
        {
        case 0:  return resourceField();
        case 1:  return std::to_wstring(images);
        case 2:  return baseName(filename);
        case 3:  return typeField();
        case 4:  return groupedNumber(totalSize);
        case 5:  return filename;
        default: return {};
        }
    }
};
