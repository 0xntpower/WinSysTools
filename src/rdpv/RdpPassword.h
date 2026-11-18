// RdpPassword.h -- one row of the RemoteDesktopPassView list, reconstructed from the
// record the original parser fills in sub_407FCE (four .rdp fields: "full address",
// "username", "domain", "password") plus the source .rdp filename (sub_402BAE).
#pragma once
#include <string>
#include <vector>

// Field order matches the parse-record offsets recovered from sub_407FCE:
//   +0x000 "full address"  -> Computer Name
//   +0x100 "domain"        -> Domain
//   +0x200 "username"      -> User Name
//   +0x300 "password"      -> Password (DPAPI-decrypted, UTF-16 plaintext)
// The display columns follow the STRINGTABLE (1001 Computer Name, 1002 Domain,
// 1003 User Name, 1004 Password, 1005 Filename).
struct RdpPassword
{
    std::wstring computer;    // col 0  Computer Name  ("full address")
    std::wstring domain;      // col 1  Domain
    std::wstring userName;    // col 2  User Name      ("username")
    std::wstring password;    // col 3  Password       (recovered plaintext)
    std::wstring fileName;    // col 4  Filename       (source .rdp path)

    // Display text per column index, matching the record getter order.
    std::wstring column(int col) const
    {
        switch (col)
        {
        case 0:  return computer;
        case 1:  return domain;
        case 2:  return userName;
        case 3:  return password;
        case 4:  return fileName;
        default: return {};
        }
    }
};

// Parse a single .rdp file. Returns true and fills `out` when the file held any of
// the recognized fields (the original adds a row whenever sub_407FCE matched a line).
[[nodiscard]] bool ParseRdpFile(const std::wstring& path, RdpPassword& out);

// Scan the default location (My Documents, CSIDL_PERSONAL) for *.rdp files.
[[nodiscard]] std::vector<RdpPassword> EnumerateRdpPasswords();
