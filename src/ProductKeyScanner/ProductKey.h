// ProductKey.h -- one row of the ProductKeyScanner list.
// Field order and column mapping recovered from sub_14000FE18 (registry decode:
// Product Key at record+48, Product ID at +176, Product Name at +1328, plus the
// Registry Key / Registry Value / Modified Time it fills) and sub_140010AAC (WMI).
// The seven columns follow the STRINGTABLE title array (ids 1001..1007):
//   0 Product ID  1 Product Key  2 Product Name  3 Scan Type
//   4 Registry Key  5 Registry Value  6 Modified Time
#pragma once
#include <string>
#include <vector>

struct ProductKeyRecord
{
    std::wstring productId;      // col 0  Product ID     ("ProductId" value / blob+8)
    std::wstring productKey;     // col 1  Product Key    (decoded base-24 key)
    std::wstring productName;    // col 2  Product Name   (ProductName / ConvertToEdition)
    std::wstring scanType;       // col 3  Scan Type      ("Registry" / "BIOS" / "WMI")
    std::wstring registryKey;    // col 4  Registry Key   ("HKEY_LOCAL_MACHINE\\...")
    std::wstring registryValue;  // col 5  Registry Value ("DigitalProductId" / ...)
    std::wstring modifiedTime;   // col 6  Modified Time  (key last-write, local time)

    std::wstring column(int col) const
    {
        switch (col)
        {
        case 0:  return productId;
        case 1:  return productKey;
        case 2:  return productName;
        case 3:  return scanType;
        case 4:  return registryKey;
        case 5:  return registryValue;
        case 6:  return modifiedTime;
        default: return {};
        }
    }
};
