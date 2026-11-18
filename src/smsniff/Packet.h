// Packet.h -- the SmartSniff data model. One SsPacket is a record from an .ssp file
// (a captured IP packet with its separate MAC addresses and timestamp). BuildConnections
// aggregates packets into one Connection per TCP/UDP/ICMP conversation, exactly the way
// the original groups them, with the column cells computed on demand.
//
// The field layout and the local/remote direction rule are reconstructed from the
// original's packet reader and its IP parser. See analysis/smsniff.md for the offsets.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include "Columns.h"

enum Proto
{
    PROTO_ICMP = 1,
    PROTO_TCP = 6,
    PROTO_UDP = 17
};

struct DisplayOptions
{
    bool showOutgoingIncoming = false; // Options -> Display Outgoing/Incoming Data
};

// One packet as stored in an .ssp file. The MAC addresses are kept separately from the
// IP bytes, the way the file format stores them, because a raw-socket capture has no
// Ethernet frame.
struct SsPacket
{
    FILETIME time{};
    uint8_t srcMac[6]{};
    uint8_t dstMac[6]{};
    std::vector<uint8_t> ip; // the IP packet, starting at the IP header
};

// The fields pulled out of one IPv4 packet.
struct ParsedPacket
{
    bool valid = false;
    uint8_t protocol = 0;
    uint32_t srcIp = 0, dstIp = 0;     // raw network-order dwords, as stored in the header
    uint16_t srcPort = 0, dstPort = 0; // host order
    uint16_t ipTotalLen = 0;
    uint16_t headerLen = 0; // IP header plus transport header
    uint32_t seq = 0;       // TCP sequence number, host order
};

// Parse one IPv4 packet. Returns false for anything that is not IPv4 TCP/UDP/ICMP.
bool ParseIp(const uint8_t* ip, size_t len, ParsedPacket& out);

// One client-server conversation.
struct Connection
{
    uint8_t protocol = 0;
    uint32_t localIp = 0, remoteIp = 0;
    uint16_t localPort = 0, remotePort = 0;
    uint8_t localMac[6]{};
    uint8_t remoteMac[6]{};

    int packets = 0, packetsOut = 0;
    long long totalSize = 0, totalSizeOut = 0; // sum of IP total length
    long long dataSize = 0, dataSizeOut = 0;   // sum of transport payload bytes

    FILETIME captureTime{}; // first packet time
    FILETIME lastPacketTime{};

    std::vector<uint8_t> payload; // concatenated transport payloads, for the lower pane
    int index = 0;                // 1-based display index

    std::wstring column(int c, const DisplayOptions& d) const;
    long long sortKey(int c, bool& numeric) const;
};

// Group packets into connections using the capture machine IP for the direction rule.
std::vector<Connection> BuildConnections(const std::vector<SsPacket>& packets, uint32_t captureIp);

// Read an .ssp file (SMSNF100 / SMSNF200). Returns false if the file is not an .ssp.
bool LoadSspFile(const std::wstring& path, std::vector<SsPacket>& out, uint32_t& captureIp);

// Serialize packets to .ssp bytes (SMSNF200). Used by the self-test to build known input.
std::vector<uint8_t> BuildSspBytes(const std::vector<SsPacket>& packets, uint32_t captureIp);

// Run the parser self-check against hand-crafted packet vectors. Writes a plain-text
// report to reportPath (empty means skip the file) and returns 0 on pass, 1 on fail.
int RunSelfTest(const std::wstring& reportPath);

// ---- small formatting helpers shared with the export and the UI ----------
std::wstring FormatIpv4(uint32_t netOrderDword);
std::wstring FormatMac(const uint8_t mac[6]);
std::wstring FormatFileTimeLocal(const FILETIME& ft);
std::wstring ServiceNameForPort(uint16_t port, uint8_t protocol);
