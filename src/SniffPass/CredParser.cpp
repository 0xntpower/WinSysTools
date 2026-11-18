// CredParser.cpp -- reproduction of the original credential state machine
// (sub_14000DCF0). It scans a connection's segments, splits each into CRLF lines, and
// runs the protocol detectors. Segment index drives the POP3, FTP and SMTP timing, the
// same way the original keys off the packet index. Only the five plaintext auth schemes
// the original handles are recovered here.
#include "CredParser.h"
#include "Columns.h"
#include <cctype>

namespace
{
    // Case insensitive first occurrence index, or -1. Matches sub_140004DFC.
    int findCI(const std::string& hay, const char* needle)
    {
        size_t nlen = 0;
        while (needle[nlen]) ++nlen;
        if (nlen == 0 || hay.size() < nlen) return -1;
        for (size_t i = 0; i + nlen <= hay.size(); ++i)
        {
            size_t k = 0;
            for (; k < nlen; ++k)
                if (std::tolower((unsigned char)hay[i + k]) != std::tolower((unsigned char)needle[k])) break;
            if (k == nlen) return (int)i;
        }
        return -1;
    }

    bool startsWithCI(const std::string& s, const char* prefix)
    {
        size_t i = 0;
        for (; prefix[i]; ++i)
        {
            if (i >= s.size()) return false;
            if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i])) return false;
        }
        return true;
    }

    bool ieq(const std::string& a, const char* b)
    {
        size_t i = 0;
        for (; i < a.size() && b[i]; ++i)
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        return i == a.size() && b[i] == 0;
    }

    // Split on single spaces, dropping empty tokens. Matches sub_14000DBB8.
    std::vector<std::string> splitSpace(const std::string& s)
    {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < s.size())
        {
            while (i < s.size() && s[i] == ' ') ++i;
            size_t start = i;
            while (i < s.size() && s[i] != ' ') ++i;
            if (i > start) out.emplace_back(s.substr(start, i - start));
        }
        return out;
    }

    // Quote aware space split used by the IMAP LOGIN path. A quoted run becomes one
    // token with the quotes removed, and trailing spaces inside a token are trimmed.
    std::vector<std::string> splitQuoted(const std::string& s)
    {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < s.size())
        {
            bool inQuote = false;
            size_t start = i;
            int qOpen = -1, qClose = -1;
            while (i < s.size())
            {
                char c = s[i];
                if (c == '"')
                {
                    inQuote = !inQuote;
                    if (inQuote) { if (qOpen < 0) qOpen = (int)(i - start); }
                    else qClose = (int)(i - start);
                }
                if (c == 0 || (c == ' ' && !inQuote)) break;
                ++i;
            }
            size_t tokStart = start, tokLen = i - start;
            if (qOpen >= 0 && qClose >= 0) { tokStart = start + qOpen + 1; tokLen = qClose - qOpen - 1; }
            std::string tok = s.substr(tokStart, tokLen);
            while (!tok.empty() && tok.back() == ' ') tok.pop_back();
            out.emplace_back(tok);
            if (i < s.size()) ++i;  // step past the delimiter
        }
        return out;
    }

    // Split a segment into lines on CR, LF or CRLF, stopping at a NUL.
    std::vector<std::string> splitLines(const std::string& seg)
    {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < seg.size() && seg[i] != 0)
        {
            size_t start = i;
            while (i < seg.size() && seg[i] != '\r' && seg[i] != '\n' && seg[i] != 0) ++i;
            out.emplace_back(seg.substr(start, i - start));
            if (i < seg.size() && seg[i] == '\r' && i + 1 < seg.size() && seg[i + 1] == '\n') ++i;
            if (i < seg.size() && seg[i] != 0) ++i;
        }
        return out;
    }

    // Drop leading spaces and tabs, matching the original's skip before each detector.
    std::string lstripWs(const std::string& line)
    {
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        return line.substr(i);
    }
}

std::string Base64Decode(const std::string& in)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0;
    };
    std::string out;
    size_t i = 0;
    while (i + 3 < in.size() || (i + 1 < in.size() && in.size() - i >= 2))
    {
        if (i + 1 >= in.size()) break;
        int a = val(in[i]);
        int b = val(in[i + 1]);
        int c = (i + 2 < in.size()) ? val(in[i + 2]) : 0;
        int d = (i + 3 < in.size()) ? val(in[i + 3]) : 0;
        out.push_back((char)((a << 2) | (b >> 4)));
        if (i + 2 < in.size() && in[i + 2] == '=') break;
        out.push_back((char)((b << 4) | (c >> 2)));
        if (i + 3 < in.size() && in[i + 3] == '=') break;
        out.push_back((char)((c << 6) | d));
        i += 4;
        if (i + 3 >= in.size() && (i >= in.size() || in[i] == 0)) break;
    }
    return out;
}

void ParseConnection(const Connection& conn, std::vector<Credential>& out)
{
    std::string user, pass, imapTag;
    int waitState = 0;        // v88: 1 SMTP after AUTH LOGIN, 2 POP3 marker, 3 IMAP after LOGIN
    int userSeg = -1;         // v58
    int authSeg = -1;         // v62
    int smtpPrompts = 0;      // v47

    auto emit = [&](int proto, bool swap) {
        Credential c;
        c.protocol = proto;
        if (swap) { c.localIP = conn.bIP; c.localPort = conn.bPort; c.remoteIP = conn.aIP; c.remotePort = conn.aPort; }
        else      { c.localIP = conn.aIP; c.localPort = conn.aPort; c.remoteIP = conn.bIP; c.remotePort = conn.bPort; }
        c.user = user;
        c.pass = pass;
        out.push_back(std::move(c));
    };

    for (int seg = 0; seg < (int)conn.segments.size(); ++seg)
    {
        bool httpFlag = false;    // v48, reset per segment
        int lineIdx = 0;          // v57

        for (const std::string& raw : splitLines(conn.segments[seg]))
        {
            std::string i = lstripWs(raw);

            int userPos = findCI(i, "user ");
            int passPos = findCI(i, "pass ");
            int getPos  = findCI(i, "get ");
            int loginPos = findCI(i, "login ");

            // HTTP basic auth: an Authorization header on a GET connection.
            if (httpFlag && findCI(i, "authorization") == 0)
            {
                auto toks = splitSpace(i);
                if (toks.size() >= 3 && ieq(toks[toks.size() - 2], "basic"))
                {
                    std::string dec = Base64Decode(toks.back());
                    size_t colon = dec.find(':');
                    if (colon != std::string::npos)
                    {
                        user = dec.substr(0, colon);
                        pass = dec.substr(colon + 1);
                        emit(PROTO_HTTP, false);
                        user.clear(); pass.clear();
                        break;
                    }
                }
            }
            if (getPos == 0 && lineIdx == 0) httpFlag = true;

            int proto26 = waitState;   // v26 mirrors v88 each line

            // IMAP tagged response after a LOGIN closes the record.
            if (waitState == 3 && !imapTag.empty() && startsWithCI(i, imapTag.c_str()))
            {
                emit(PROTO_IMAP, true);
                user.clear(); pass.clear(); waitState = 0;
                break;
            }

            // IMAP LOGIN, as "<tag> LOGIN <user> <pass>".
            if (loginPos > 0)
            {
                auto toks = splitQuoted(i);
                if (toks.size() == 4 && ieq(toks[1], "login"))
                {
                    proto26 = PROTO_IMAP; waitState = 3;
                    user = toks[2]; pass = toks[3]; imapTag = toks[0];
                }
            }

            // POP3 / FTP username line.
            int userSegLocal = userSeg;
            if (userPos == 0 && user.empty())
            {
                user = i.substr(5);
                userSeg = seg; userSegLocal = seg;
            }

            // POP3 / FTP password line closes the record.
            bool skipToSmtp = false;
            if (passPos == 0 && pass.empty())
            {
                if (user.empty()) skipToSmtp = true;
                else
                {
                    pass = i.substr(5);
                    emit(proto26, true);
                    user.clear(); pass.clear(); waitState = 0; proto26 = 0;
                }
            }

            // POP3 vs FTP: look at the server reply right after the username segment.
            if (!skipToSmtp && !user.empty() && userSegLocal + 1 == seg)
            {
                if (!i.empty() && i[0] == '+') { waitState = 2; }
                else
                {
                    if (!i.empty() && std::isdigit((unsigned char)i[0])) proto26 = PROTO_FTP;
                    waitState = proto26;
                }
            }

            // SMTP AUTH LOGIN base64 handshake, gated on the SMTP state.
            if (proto26 == PROTO_SMTP && authSeg >= 0)
            {
                int d = seg - authSeg;
                if (d == 1 || d == 3)
                {
                    if (startsWithCI(i, "334 ")) ++smtpPrompts;
                }
                if (d == 2 || d == 4)
                {
                    auto toks = splitSpace(i);
                    if (toks.size() == 1)
                    {
                        std::string dec = Base64Decode(toks[0]);
                        if (d == 2) user = dec;
                        if (d == 4)
                        {
                            pass = dec;
                            if (smtpPrompts == 2)
                            {
                                emit(PROTO_SMTP, true);
                                user.clear(); pass.clear(); waitState = 0;
                            }
                        }
                    }
                }
            }

            // AUTH LOGIN command opens the SMTP state.
            if (startsWithCI(i, "auth "))
            {
                auto toks = splitSpace(i);
                if (toks.size() == 2 && ieq(toks[0], "auth") && ieq(toks[1], "login"))
                {
                    waitState = 1; authSeg = seg; smtpPrompts = 0;
                }
            }

            ++lineIdx;
        }
    }
}

// ---------------------------------------------------------------------------
// Self test. Each vector is a hand built byte stream with invented values. No real
// capture and no real credential is ever involved.
namespace
{
    struct Vector
    {
        const char* name;
        Connection  conn;
        int         proto;
        const char* user;
        const char* pass;
    };

    Connection makeConn(std::vector<std::string> segs)
    {
        Connection c;
        c.aIP = 0x0100007F;   // 127.0.0.1
        c.bIP = 0x0101A8C0;   // 192.168.1.1
        c.aPort = 1050;
        c.bPort = 110;
        c.segments = std::move(segs);
        return c;
    }
}

bool RunParserSelfTest(std::string& report)
{
    std::vector<Vector> vectors = {
        // POP3: USER then a server +OK then PASS.
        { "POP3", makeConn({ "USER alice\r\n", "+OK\r\n", "PASS wonderland\r\n" }),
          PROTO_POP3, "alice", "wonderland" },
        // FTP: USER then a 3 digit server reply then PASS.
        { "FTP", makeConn({ "USER bob\r\n", "331 Password required\r\n", "PASS buildr\r\n" }),
          PROTO_FTP, "bob", "buildr" },
        // IMAP: a tagged LOGIN then the tagged OK response.
        { "IMAP", makeConn({ "a1 LOGIN carol p@ssw0rd\r\n", "a1 OK LOGIN completed\r\n" }),
          PROTO_IMAP, "carol", "p@ssw0rd" },
        // SMTP: AUTH LOGIN then the two base64 334 prompts and the two base64 replies.
        { "SMTP", makeConn({ "AUTH LOGIN\r\n", "334 VXNlcm5hbWU6\r\n", "ZGVtbw==\r\n",
                             "334 UGFzc3dvcmQ6\r\n", "czNjcjN0\r\n" }),
          PROTO_SMTP, "demo", "s3cr3t" },
        // HTTP basic: a GET request with an Authorization header, one segment.
        { "HTTP", makeConn({ "GET /secure HTTP/1.1\r\nHost: example.test\r\n"
                             "Authorization: Basic d2VidXNlcjp3M2JwNHNz\r\n\r\n" }),
          PROTO_HTTP, "webuser", "w3bp4ss" },
    };

    bool allPass = true;
    for (const Vector& v : vectors)
    {
        std::vector<Credential> got;
        ParseConnection(v.conn, got);
        bool ok = got.size() == 1 && got[0].protocol == v.proto &&
                  got[0].user == v.user && got[0].pass == v.pass;
        if (!ok) allPass = false;
        report += v.name;
        report += ok ? ": PASS" : ": FAIL";
        if (!ok)
        {
            report += " (got ";
            report += std::to_string(got.size());
            report += " rows";
            if (!got.empty())
            {
                report += ", proto=" + std::to_string(got[0].protocol);
                report += " user=" + got[0].user + " pass=" + got[0].pass;
            }
            report += ")";
        }
        report += "\n";
    }
    return allPass;
}
