// RdpEnumerator.cpp -- reconstructs RemoteDesktopPassView's .rdp parsing and scan.
// Mirrors sub_408161 (read the file, split each line on two colons), sub_407FCE
// (match "full address" / "username" / "domain" / "password" and DPAPI-recover the
// password hex), and sub_4085F8 / sub_4019F4 (scan My Documents for *.rdp).
#include "RdpPassword.h"
#include "Dpapi.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>

namespace
{
    // The original caps a stored field at 126 wide chars (sub_407F9F). Keep full
    // fidelity here instead -- all recovered data is ASCII/UTF-16 either way.
    std::wstring readFileAsWide(const std::wstring& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return {};
        std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (raw.size() >= 0x4000) raw.resize(0x3FFF);   // original ignores files >= 0x4000
        if (raw.empty()) return {};

        const auto* b = reinterpret_cast<const unsigned char*>(raw.data());

        // UTF-16LE with BOM.
        if (raw.size() >= 2 && b[0] == 0xFF && b[1] == 0xFE)
            return std::wstring(reinterpret_cast<const wchar_t*>(raw.data() + 2),
                                (raw.size() - 2) / sizeof(wchar_t));

        // UTF-16LE without BOM (as mstsc writes .rdp, and as the original assumes).
        if (raw.size() >= 2 && b[1] == 0x00 && b[0] != 0x00)
            return std::wstring(reinterpret_cast<const wchar_t*>(raw.data()),
                                raw.size() / sizeof(wchar_t));

        // ANSI / UTF-8 fallback (an improvement over the original's wide-only read).
        UINT cp = (raw.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? CP_UTF8 : CP_ACP;
        const char* start = (cp == CP_UTF8) ? raw.data() + 3 : raw.data();
        int len = (cp == CP_UTF8) ? static_cast<int>(raw.size() - 3) : static_cast<int>(raw.size());
        int n = ::MultiByteToWideChar(cp, 0, start, len, nullptr, 0);
        std::wstring w(n, L'\0');
        ::MultiByteToWideChar(cp, 0, start, len, w.data(), n);
        return w;
    }

    // sub_4085F8: the default scan folder is CSIDL_PERSONAL (My Documents).
    std::wstring myDocumentsPath()
    {
        wchar_t path[MAX_PATH] = {};
        if (SUCCEEDED(::SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, path)))
            return path;
        return {};
    }
}

bool ParseRdpFile(const std::wstring& path, RdpPassword& out)
{
    std::wstring text = readFileAsWide(path);
    if (text.empty()) return false;

    out = RdpPassword{};
    out.fileName = path;
    bool matched = false;

    size_t pos = 0;
    while (pos < text.size())
    {
        size_t eol = text.find_first_of(L"\r\n", pos);
        std::wstring line = text.substr(pos, eol == std::wstring::npos ? std::wstring::npos : eol - pos);
        pos = (eol == std::wstring::npos) ? text.size() : eol + 1;

        // sub_408161: split on the first colon (key) then the next colon (type),
        // leaving the value. Lines without two colons are skipped.
        size_t c1 = line.find(L':');
        if (c1 == std::wstring::npos) continue;
        size_t c2 = line.find(L':', c1 + 1);
        if (c2 == std::wstring::npos) continue;

        std::wstring key = line.substr(0, c1);
        std::wstring value = line.substr(c2 + 1);

        if (_wcsicmp(key.c_str(), L"full address") == 0) { out.computer = value; matched = true; }
        else if (_wcsicmp(key.c_str(), L"username") == 0) { out.userName = value; matched = true; }
        else if (_wcsicmp(key.c_str(), L"domain") == 0) { out.domain = value; matched = true; }
        else if (_wcsnicmp(key.c_str(), L"password", 8) == 0)
        {
            // "password 51:b:<hex>" -- decode the hex and DPAPI-recover it.
            std::wstring pw = RecoverRdpPasswordHex(value);
            if (!pw.empty()) { out.password = pw; matched = true; }
        }
    }

    return matched;
}

std::vector<RdpPassword> EnumerateRdpPasswords()
{
    std::vector<RdpPassword> out;

    std::wstring docs = myDocumentsPath();
    if (docs.empty()) return out;
    if (docs.back() != L'\\') docs += L'\\';

    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW((docs + L"*.rdp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        RdpPassword rec;
        if (ParseRdpFile(docs + fd.cFileName, rec))
            out.push_back(std::move(rec));
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);

    return out;
}
