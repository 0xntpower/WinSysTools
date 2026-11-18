// CredParser.h -- the per-protocol credential extractor, the behavioural core of
// SniffPass. It reproduces sub_14000DCF0 from the original binary. The input is one
// reassembled TCP connection, modelled as an ordered list of segments (each segment is
// one captured packet payload, which may hold several CRLF lines). The output is one
// Credential per recovered login, for POP3, IMAP, SMTP, FTP and HTTP basic auth.
//
// The parser works only on supplied byte buffers. It never touches a socket or a driver.
// All capture is done elsewhere, and in this recreation no live capture runs at all, so
// the only data this ever sees is the synthetic vectors in the self test.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Connection
{
    // The two endpoints, as the original stores them (a = first seen, b = peer).
    uint32_t aIP = 0;        // network byte order
    uint32_t bIP = 0;
    uint16_t aPort = 0;      // host byte order
    uint16_t bPort = 0;
    std::vector<std::string> segments;   // capture-ordered packet payloads
};

struct Credential
{
    int         protocol = 0;   // Protocol enum from Columns.h
    uint32_t    localIP = 0;
    uint32_t    remoteIP = 0;
    uint16_t    localPort = 0;
    uint16_t    remotePort = 0;
    std::string user;
    std::string pass;
};

// Parse one connection and append every recovered credential to out.
void ParseConnection(const Connection& conn, std::vector<Credential>& out);

// Standard base64 decode, matching the original's table (A-Z a-z 0-9 + /), stopping at
// the first '=' pad. Exposed for the self test.
std::string Base64Decode(const std::string& in);

// Run the built in protocol vectors and return true when every one passes. Writes a
// short pass or fail line per protocol to the given sink.
bool RunParserSelfTest(std::string& report);
