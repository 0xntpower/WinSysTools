// Packet.cpp -- the packet parser, the connection builder, the .ssp reader and writer,
// and the parser self-check. The offsets and the direction rule follow the original.
#include "Packet.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <array>

namespace
{
    inline uint16_t be16(const uint8_t* p)
    {
        return (uint16_t)((p[0] << 8) | p[1]);
    }
    inline uint32_t be32(const uint8_t* p)
    {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }
    inline uint32_t rd32(const uint8_t* p)
    {
        uint32_t v;
        memcpy(&v, p, 4);
        return v;
    } // raw dword, memory order

    long long filetimeToLL(const FILETIME& ft)
    {
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return (long long)u.QuadPart;
    }

    std::wstring groupNumber(long long v)
    {
        wchar_t raw[32];
        swprintf(raw, 32, L"%lld", v);
        std::wstring s = raw;
        bool neg = !s.empty() && s[0] == L'-';
        std::wstring digits = neg ? s.substr(1) : s;
        std::wstring out;
        int c = 0;
        for (int i = (int)digits.size() - 1; i >= 0; --i)
        {
            out.insert(out.begin(), digits[i]);
            if (++c % 3 == 0 && i != 0)
            {
                out.insert(out.begin(), L',');
            }
        }
        return neg ? L"-" + out : out;
    }

    // Format a pair of values in the outgoing-then-incoming form when the option is on,
    // or the combined value otherwise.
    std::wstring outIn(long long total, long long out, bool split)
    {
        if (!split)
        {
            return groupNumber(total);
        }
        return groupNumber(out) + L" ; " + groupNumber(total - out);
    }
} // namespace

bool ParseIp(const uint8_t* ip, size_t len, ParsedPacket& out)
{
    out = ParsedPacket{};
    if (!ip || len < 20)
    {
        return false;
    }
    if ((ip[0] & 0xF0) != 0x40)
    {
        return false; // IPv4 only
    }
    uint16_t ihl = (uint16_t)(4 * (ip[0] & 0x0F));
    if (ihl < 20 || ihl > len)
    {
        return false;
    }

    out.protocol = ip[9];
    out.ipTotalLen = be16(ip + 2);
    out.srcIp = rd32(ip + 12);
    out.dstIp = rd32(ip + 16);

    if (out.protocol == PROTO_TCP)
    {
        if ((size_t)ihl + 20 > len)
        {
            return false;
        }
        const uint8_t* tcp = ip + ihl;
        out.srcPort = be16(tcp + 0);
        out.dstPort = be16(tcp + 2);
        out.seq = be32(tcp + 4);
        out.headerLen = (uint16_t)(ihl + 4 * (tcp[12] >> 4));
    }
    else if (out.protocol == PROTO_UDP)
    {
        if ((size_t)ihl + 8 > len)
        {
            return false;
        }
        const uint8_t* udp = ip + ihl;
        out.srcPort = be16(udp + 0);
        out.dstPort = be16(udp + 2);
        out.headerLen = (uint16_t)(ihl + 8);
    }
    else if (out.protocol == PROTO_ICMP)
    {
        out.headerLen = (uint16_t)(ihl + 8);
    }
    else
    {
        return false;
    }

    // Guard against a total-length field smaller than the headers we counted.
    if (out.ipTotalLen < out.headerLen)
    {
        out.ipTotalLen = out.headerLen;
    }
    out.valid = true;
    return true;
}

std::vector<Connection> BuildConnections(const std::vector<SsPacket>& packets, uint32_t captureIp)
{
    struct Key
    {
        uint8_t proto;
        uint32_t lip, rip;
        uint16_t lport, rport;
        bool operator<(const Key& o) const
        {
            return std::tie(proto, lip, rip, lport, rport) < std::tie(o.proto, o.lip, o.rip, o.lport, o.rport);
        }
    };
    std::map<Key, size_t> index;
    std::vector<Connection> conns;

    for (const SsPacket& sp : packets)
    {
        ParsedPacket pp;
        if (!ParseIp(sp.ip.data(), sp.ip.size(), pp))
        {
            continue;
        }

        bool incoming = (pp.dstIp == captureIp);
        uint32_t lip = incoming ? pp.dstIp : pp.srcIp;
        uint32_t rip = incoming ? pp.srcIp : pp.dstIp;
        uint16_t lport = incoming ? pp.dstPort : pp.srcPort;
        uint16_t rport = incoming ? pp.srcPort : pp.dstPort;
        const uint8_t* lmac = incoming ? sp.dstMac : sp.srcMac;
        const uint8_t* rmac = incoming ? sp.srcMac : sp.dstMac;

        Key k{pp.protocol, lip, rip, lport, rport};
        auto it = index.find(k);
        if (it == index.end())
        {
            Connection c;
            c.protocol = pp.protocol;
            c.localIp = lip;
            c.remoteIp = rip;
            c.localPort = lport;
            c.remotePort = rport;
            memcpy(c.localMac, lmac, 6);
            memcpy(c.remoteMac, rmac, 6);
            c.captureTime = sp.time;
            index[k] = conns.size();
            conns.push_back(std::move(c));
            it = index.find(k);
        }
        Connection& c = conns[it->second];

        long long payloadBytes = (long long)pp.ipTotalLen - (long long)pp.headerLen;
        if (payloadBytes < 0)
        {
            payloadBytes = 0;
        }

        ++c.packets;
        c.totalSize += pp.ipTotalLen;
        c.dataSize += payloadBytes;
        if (!incoming)
        {
            ++c.packetsOut;
            c.totalSizeOut += pp.ipTotalLen;
            c.dataSizeOut += payloadBytes;
        }
        c.lastPacketTime = sp.time;

        // Keep the transport payload for the lower pane, bounded so a huge file stays light.
        size_t off = pp.headerLen;
        if (off < sp.ip.size() && c.payload.size() < 0x100000)
        {
            c.payload.insert(c.payload.end(), sp.ip.begin() + off, sp.ip.end());
        }
    }

    for (size_t i = 0; i < conns.size(); ++i)
    {
        conns[i].index = (int)i + 1;
    }
    return conns;
}

// ---- .ssp reader / writer -----------------------------------------------
bool LoadSspFile(const std::wstring& path, std::vector<SsPacket>& out, uint32_t& captureIp)
{
    out.clear();
    captureIp = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        return false;
    }

    char sig[8];
    if (!f.read(sig, 8))
    {
        return false;
    }
    if (memcmp(sig, "SMSNF100", 8) != 0 && memcmp(sig, "SMSNF200", 8) != 0)
    {
        return false;
    }

    uint16_t mainHdrSize = 0;
    if (!f.read(reinterpret_cast<char*>(&mainHdrSize), 2))
    {
        return false;
    }
    if (mainHdrSize == 0 || mainHdrSize > 255)
    {
        return false;
    }
    std::vector<uint8_t> mh(mainHdrSize);
    if (!f.read(reinterpret_cast<char*>(mh.data()), mainHdrSize))
    {
        return false;
    }
    if (mainHdrSize >= 4)
    {
        memcpy(&captureIp, mh.data(), 4);
    }

    while (true)
    {
        uint16_t phSize = 0;
        if (!f.read(reinterpret_cast<char*>(&phSize), 2))
        {
            break;
        }
        if (phSize == 0 || phSize > 255)
        {
            break;
        }
        std::vector<uint8_t> ph(phSize);
        if (!f.read(reinterpret_cast<char*>(ph.data()), phSize))
        {
            break;
        }
        if (phSize < 24)
        {
            break;
        }

        uint32_t dataLen = 0;
        memcpy(&dataLen, ph.data(), 4);
        if (dataLen > 0xFFE)
        {
            break;
        }

        SsPacket sp;
        memcpy(&sp.time, ph.data() + 4, 8);
        memcpy(sp.srcMac, ph.data() + 12, 6);
        memcpy(sp.dstMac, ph.data() + 18, 6);
        sp.ip.resize(dataLen);
        if (dataLen && !f.read(reinterpret_cast<char*>(sp.ip.data()), dataLen))
        {
            break;
        }
        out.push_back(std::move(sp));
    }
    return true;
}

std::vector<uint8_t> BuildSspBytes(const std::vector<SsPacket>& packets, uint32_t captureIp)
{
    std::vector<uint8_t> b;
    auto put = [&](const void* p, size_t n)
    {
        const uint8_t* s = (const uint8_t*)p;
        b.insert(b.end(), s, s + n);
    };

    put("SMSNF200", 8);
    uint16_t mainHdrSize = 4;
    put(&mainHdrSize, 2);
    put(&captureIp, 4);

    for (const SsPacket& sp : packets)
    {
        uint16_t phSize = 24;
        put(&phSize, 2);
        uint32_t dataLen = (uint32_t)sp.ip.size();
        put(&dataLen, 4);
        put(&sp.time, 8);
        put(sp.srcMac, 6);
        put(sp.dstMac, 6);
        put(sp.ip.data(), sp.ip.size());
    }
    return b;
}

// ---- formatting ---------------------------------------------------------
std::wstring FormatIpv4(uint32_t netOrderDword)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&netOrderDword);
    wchar_t s[20];
    swprintf(s, 20, L"%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return s;
}

std::wstring FormatMac(const uint8_t mac[6])
{
    bool zero = true;
    for (int i = 0; i < 6; ++i)
    {
        if (mac[i])
        {
            zero = false;
        }
    }
    if (zero)
    {
        return L"";
    }
    wchar_t s[20];
    swprintf(s, 20, L"%02X-%02X-%02X-%02X-%02X-%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return s;
}

std::wstring FormatFileTimeLocal(const FILETIME& ft)
{
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0)
    {
        return L"";
    }
    FILETIME local{};
    if (!FileTimeToLocalFileTime(&ft, &local))
    {
        local = ft;
    }
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&local, &st))
    {
        return L"";
    }
    wchar_t s[32];
    swprintf(s, 32, L"%02d/%02d/%04d %02d:%02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
    return s;
}

std::wstring ServiceNameForPort(uint16_t port, uint8_t protocol)
{
    // A small well-known-ports table stands in for the system services lookup the original
    // performs. It keeps the parser self-contained and deterministic for the self-check.
    struct Svc
    {
        uint16_t port;
        const wchar_t* tcp;
        const wchar_t* udp;
    };
    static const Svc table[] = {
        {20, L"ftp-data", L""},
        {21, L"ftp", L""},
        {22, L"ssh", L""},
        {23, L"telnet", L""},
        {25, L"smtp", L""},
        {53, L"domain", L"domain"},
        {67, L"", L"bootps"},
        {68, L"", L"bootpc"},
        {80, L"http", L""},
        {110, L"pop3", L""},
        {123, L"", L"ntp"},
        {135, L"epmap", L"epmap"},
        {137, L"", L"netbios-ns"},
        {138, L"", L"netbios-dgm"},
        {139, L"netbios-ssn", L""},
        {143, L"imap", L""},
        {161, L"", L"snmp"},
        {389, L"ldap", L""},
        {443, L"https", L""},
        {445, L"microsoft-ds", L"microsoft-ds"},
        {465, L"smtps", L""},
        {514, L"", L"syslog"},
        {587, L"submission", L""},
        {993, L"imaps", L""},
        {995, L"pop3s", L""},
        {1900, L"", L"ssdp"},
        {3389, L"ms-wbt-server", L""},
        {5353, L"", L"mdns"},
    };
    for (const Svc& s : table)
    {
        if (s.port == port)
        {
            const wchar_t* n = (protocol == PROTO_UDP) ? s.udp : s.tcp;
            if (n && n[0])
            {
                return n;
            }
        }
    }
    return L"";
}

std::wstring Connection::column(int c, const DisplayOptions& d) const
{
    switch (c)
    {
    case 0:
    {
        wchar_t b[16];
        swprintf(b, 16, L"%d", index);
        return b;
    }
    case 1:
        return protocol == PROTO_TCP ? L"TCP" : protocol == PROTO_UDP ? L"UDP" : protocol == PROTO_ICMP ? L"ICMP" : L"";
    case 2:
        return FormatIpv4(localIp);
    case 3:
        return FormatIpv4(remoteIp);
    case 4:
        return protocol == PROTO_ICMP ? L"" : groupNumber(localPort);
    case 5:
        return protocol == PROTO_ICMP ? L"" : groupNumber(remotePort);
    case 6:
        return outIn(packets, packetsOut, d.showOutgoingIncoming);
    case 7:
        return outIn(totalSize, totalSizeOut, d.showOutgoingIncoming);
    case 8:
        return FormatFileTimeLocal(captureTime);
    case 9:
        return outIn(dataSize, dataSizeOut, d.showOutgoingIncoming);
    case 10:
        return protocol == PROTO_ICMP ? L"" : ServiceNameForPort(remotePort, protocol);
    case 11:
        return L""; // Local Host, requires DNS resolution
    case 12:
        return L""; // Remote Host
    case 13:
        return L""; // ProcessID, live-capture only
    case 14:
        return L""; // Process Filename
    case 15:
        return FormatMac(localMac);
    case 16:
        return FormatMac(remoteMac);
    case 17:
        return L""; // Local IP Country, requires an IP location file
    case 18:
        return L""; // Remote IP Country
    case 19:
        return FormatFileTimeLocal(lastPacketTime);
    case 20:
    {
        long long span = filetimeToLL(lastPacketTime) - filetimeToLL(captureTime);
        double ms = span / 10000.0; // 100-ns units to milliseconds
        if (ms <= 0.0)
        {
            return L"";
        }
        double kbps = (double)dataSize * 1000.0 / (ms * 1024.0);
        wchar_t b[32];
        swprintf(b, 32, L"%.2f KB/Sec", kbps);
        return b;
    }
    case 21:
        return L""; // Process User
    case 22:
    {
        long long span = filetimeToLL(lastPacketTime) - filetimeToLL(captureTime);
        if (span < 0)
        {
            span = 0;
        }
        long long secs = span / 10000000;
        wchar_t b[32];
        swprintf(b, 32, L"%lld:%02lld:%02lld", secs / 3600, (secs / 60) % 60, secs % 60);
        return b;
    }
    default:
        return L"";
    }
}

long long Connection::sortKey(int c, bool& numeric) const
{
    numeric = true;
    switch (c)
    {
    case 0:
        return index;
    case 4:
        return localPort;
    case 5:
        return remotePort;
    case 6:
        return packets;
    case 7:
        return totalSize;
    case 8:
        return filetimeToLL(captureTime);
    case 9:
        return dataSize;
    case 19:
        return filetimeToLL(lastPacketTime);
    default:
        numeric = false;
        return 0;
    }
}

// ---- self-check ----------------------------------------------------------
namespace
{
    // Build a minimal IPv4 packet with the given protocol and endpoints. Payload bytes are
    // appended after the headers. The checksums are left zero, which the parser does not read.
    std::vector<uint8_t> makeIp(uint8_t proto, uint32_t srcNet, uint32_t dstNet, uint16_t sport, uint16_t dport,
                                const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> ip(20, 0);
        ip[0] = 0x45; // version 4, IHL 5
        ip[9] = proto;
        memcpy(&ip[12], &srcNet, 4);
        memcpy(&ip[16], &dstNet, 4);

        if (proto == PROTO_TCP)
        {
            std::vector<uint8_t> tcp(20, 0);
            tcp[0] = (uint8_t)(sport >> 8);
            tcp[1] = (uint8_t)sport;
            tcp[2] = (uint8_t)(dport >> 8);
            tcp[3] = (uint8_t)dport;
            tcp[12] = 0x50; // data offset 5 words, 20 bytes
            ip.insert(ip.end(), tcp.begin(), tcp.end());
        }
        else if (proto == PROTO_UDP)
        {
            std::vector<uint8_t> udp(8, 0);
            udp[0] = (uint8_t)(sport >> 8);
            udp[1] = (uint8_t)sport;
            udp[2] = (uint8_t)(dport >> 8);
            udp[3] = (uint8_t)dport;
            ip.insert(ip.end(), udp.begin(), udp.end());
        }
        else if (proto == PROTO_ICMP)
        {
            std::vector<uint8_t> icmp(8, 0);
            icmp[0] = 8; // echo request
            ip.insert(ip.end(), icmp.begin(), icmp.end());
        }

        ip.insert(ip.end(), payload.begin(), payload.end());
        uint16_t total = (uint16_t)ip.size();
        ip[2] = (uint8_t)(total >> 8);
        ip[3] = (uint8_t)total;
        return ip;
    }

    uint32_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    {
        uint8_t q[4] = {a, b, c, d};
        uint32_t v;
        memcpy(&v, q, 4);
        return v;
    }

    FILETIME mkTime(long long qw)
    {
        FILETIME ft;
        ULARGE_INTEGER u;
        u.QuadPart = qw;
        ft.dwLowDateTime = u.LowPart;
        ft.dwHighDateTime = u.HighPart;
        return ft;
    }
} // namespace

int RunSelfTest(const std::wstring& reportPath)
{
    std::wstring log;
    int fails = 0;
    auto check = [&](const wchar_t* name, bool ok)
    {
        log += ok ? L"PASS  " : L"FAIL  ";
        log += name;
        log += L"\r\n";
        if (!ok)
        {
            ++fails;
        }
    };

    const uint32_t me = ipv4(192, 168, 1, 10);
    const uint32_t web = ipv4(93, 184, 216, 34);
    const uint32_t dns = ipv4(8, 8, 8, 8);

    // 1. Direct parser checks on hand-crafted IP bytes.
    {
        std::vector<uint8_t> payload(10, 0x41); // ten 'A' bytes
        auto tcp = makeIp(PROTO_TCP, me, web, 49152, 80, payload);
        ParsedPacket p;
        bool ok = ParseIp(tcp.data(), tcp.size(), p);
        check(L"parse TCP valid", ok);
        check(L"parse TCP protocol", p.protocol == PROTO_TCP);
        check(L"parse TCP ports", p.srcPort == 49152 && p.dstPort == 80);
        check(L"parse TCP header len", p.headerLen == 40);
        check(L"parse TCP total len", p.ipTotalLen == 50);
        check(L"parse TCP src ip", p.srcIp == me && p.dstIp == web);

        auto udp = makeIp(PROTO_UDP, me, dns, 51000, 53, std::vector<uint8_t>(20, 0));
        ParsedPacket u;
        ParseIp(udp.data(), udp.size(), u);
        check(L"parse UDP ports", u.srcPort == 51000 && u.dstPort == 53);
        check(L"parse UDP header len", u.headerLen == 28);

        auto icmp = makeIp(PROTO_ICMP, me, dns, 0, 0, std::vector<uint8_t>(4, 0));
        ParsedPacket ic;
        ParseIp(icmp.data(), icmp.size(), ic);
        check(L"parse ICMP protocol", ic.protocol == PROTO_ICMP);
        check(L"parse ICMP header len", ic.headerLen == 28);

        uint8_t junk[8] = {0x60, 0, 0, 0, 0, 0, 0, 0}; // IPv6 version nibble
        ParsedPacket bad;
        check(L"reject non-IPv4", !ParseIp(junk, sizeof(junk), bad));
    }

    // 2. Build a synthetic .ssp, round-trip it, and group into connections.
    {
        std::vector<SsPacket> pkts;
        auto addPkt = [&](std::vector<uint8_t> ip, long long t)
        {
            SsPacket sp;
            sp.ip = std::move(ip);
            sp.time = mkTime(t);
            sp.srcMac[0] = 0x02;
            sp.srcMac[5] = 0x01;
            sp.dstMac[0] = 0x02;
            sp.dstMac[5] = 0x02;
            pkts.push_back(std::move(sp));
        };
        // Outgoing SYN, incoming reply, outgoing data on the same TCP conversation.
        addPkt(makeIp(PROTO_TCP, me, web, 49152, 80, std::vector<uint8_t>(0)), 130000000000000000LL);
        addPkt(makeIp(PROTO_TCP, web, me, 80, 49152, std::vector<uint8_t>(100, 0x42)), 130000000050000000LL);
        addPkt(makeIp(PROTO_TCP, me, web, 49152, 80, std::vector<uint8_t>(20, 0x43)), 130000000100000000LL);
        // A separate UDP DNS query and an ICMP echo.
        addPkt(makeIp(PROTO_UDP, me, dns, 51000, 53, std::vector<uint8_t>(30, 0)), 130000000060000000LL);
        addPkt(makeIp(PROTO_ICMP, me, dns, 0, 0, std::vector<uint8_t>(32, 0)), 130000000070000000LL);

        std::vector<uint8_t> ssp = BuildSspBytes(pkts, me);
        check(L"ssp signature", ssp.size() > 10 && memcmp(ssp.data(), "SMSNF200", 8) == 0);

        // Write and read it back to exercise the file path.
        std::vector<SsPacket> loaded;
        uint32_t capIp = 0;
        {
            wchar_t tmp[MAX_PATH], dir[MAX_PATH];
            GetTempPathW(MAX_PATH, dir);
            GetTempFileNameW(dir, L"sst", 0, tmp);
            std::ofstream of(tmp, std::ios::binary);
            of.write(reinterpret_cast<const char*>(ssp.data()), (std::streamsize)ssp.size());
            of.close();
            bool ok = LoadSspFile(tmp, loaded, capIp);
            DeleteFileW(tmp);
            check(L"ssp round-trip load", ok);
        }
        check(L"ssp capture ip", capIp == me);
        check(L"ssp packet count", loaded.size() == 5);

        std::vector<Connection> conns = BuildConnections(loaded, me);
        check(L"connection count", conns.size() == 3);

        const Connection* tcp = nullptr;
        for (const auto& c : conns)
        {
            if (c.protocol == PROTO_TCP)
            {
                tcp = &c;
            }
        }
        check(L"tcp conversation found", tcp != nullptr);
        if (tcp)
        {
            check(L"tcp local endpoint", tcp->localIp == me && tcp->localPort == 49152);
            check(L"tcp remote endpoint", tcp->remoteIp == web && tcp->remotePort == 80);
            check(L"tcp packet count", tcp->packets == 3);
            check(L"tcp outgoing count", tcp->packetsOut == 2);
            // total is 40 plus 140 plus 60, data is 0 plus 100 plus 20
            check(L"tcp total size", tcp->totalSize == 240);
            check(L"tcp data size", tcp->dataSize == 120);
            check(L"tcp service name", tcp->column(10, DisplayOptions{}) == L"http");
        }
    }

    log += fails == 0 ? L"\r\nRESULT: PASS\r\n" : L"\r\nRESULT: FAIL\r\n";

    if (!reportPath.empty())
    {
        std::ofstream rf(reportPath, std::ios::binary);
        int n = WideCharToMultiByte(CP_UTF8, 0, log.data(), (int)log.size(), nullptr, 0, nullptr, nullptr);
        std::string u8(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, log.data(), (int)log.size(), u8.data(), n, nullptr, nullptr);
        rf.write(u8.data(), (std::streamsize)u8.size());
    }

    // Also report to a parent console when one is attached, so a plain run prints results.
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        DWORD written = 0;
        int n = WideCharToMultiByte(CP_UTF8, 0, log.data(), (int)log.size(), nullptr, 0, nullptr, nullptr);
        std::string u8(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, log.data(), (int)log.size(), u8.data(), n, nullptr, nullptr);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), u8.data(), (DWORD)u8.size(), &written, nullptr);
        FreeConsole();
    }

    return fails == 0 ? 0 : 1;
}
