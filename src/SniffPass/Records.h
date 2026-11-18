// Records.h -- one captured password row, with its display cells precomputed. The
// columns are Index, Protocol, Local Address, Remote Address, Local Port, Remote Port,
// User, Password and Capture Time. Sorting uses numeric keys for the index and the two
// port columns, text for the rest.
#pragma once
#include <windows.h>
#include <string>
#include <array>
#include <cstdint>
#include "Columns.h"
#include "CredParser.h"

struct PasswordRecord
{
    std::array<std::wstring, kColumnCount> cells;
    int      index = 0;
    int      protocol = 0;
    uint32_t localIP = 0, remoteIP = 0;
    uint16_t localPort = 0, remotePort = 0;
    std::wstring user, pass;

    const std::wstring& column(int c) const
    {
        static const std::wstring empty;
        return (c >= 0 && c < kColumnCount) ? cells[c] : empty;
    }

    long long sortKey(int c, bool& numeric) const
    {
        numeric = true;
        switch (c)
        {
        case 0: return index;
        case 4: return localPort;
        case 5: return remotePort;
        default: numeric = false; return 0;
        }
    }

    // A record is a duplicate of another when protocol, both endpoints and both
    // credentials match, matching the Exclude Duplicates option in the original.
    bool sameAs(const PasswordRecord& o) const
    {
        return protocol == o.protocol && localIP == o.localIP && remoteIP == o.remoteIP &&
               localPort == o.localPort && remotePort == o.remotePort &&
               user == o.user && pass == o.pass;
    }
};

inline std::wstring FormatIP(uint32_t ip)
{
    wchar_t b[20];
    swprintf(b, 20, L"%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return b;
}

inline std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = ::MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Build a display record from a parser credential, at the given index and time.
inline PasswordRecord MakeRecord(const Credential& c, int index, const SYSTEMTIME& st)
{
    PasswordRecord r;
    r.index = index;
    r.protocol = c.protocol;
    r.localIP = c.localIP; r.remoteIP = c.remoteIP;
    r.localPort = c.localPort; r.remotePort = c.remotePort;
    r.user = Widen(c.user);
    r.pass = Widen(c.pass);

    wchar_t idx[16]; swprintf(idx, 16, L"%d", index);
    wchar_t lp[8]; swprintf(lp, 8, L"%u", c.localPort);
    wchar_t rp[8]; swprintf(rp, 8, L"%u", c.remotePort);
    wchar_t ts[40];
    swprintf(ts, 40, L"%02d/%02d/%04d %02d:%02d:%02d",
             st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);

    r.cells[0] = idx;
    r.cells[1] = ProtocolName(c.protocol);
    r.cells[2] = FormatIP(c.localIP);
    r.cells[3] = FormatIP(c.remoteIP);
    r.cells[4] = lp;
    r.cells[5] = rp;
    r.cells[6] = r.user;
    r.cells[7] = r.pass;
    r.cells[8] = ts;
    return r;
}
