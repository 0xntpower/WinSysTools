// Des.cpp -- standard DES (ECB) used to deobfuscate VNC passwords.
#include "Des.h"
#include <cstring>

namespace des
{
namespace
{
    // All tables are 1-indexed from the most-significant bit, the textbook layout.
    const int IP[64] = {
        58,50,42,34,26,18,10,2, 60,52,44,36,28,20,12,4,
        62,54,46,38,30,22,14,6, 64,56,48,40,32,24,16,8,
        57,49,41,33,25,17, 9,1, 59,51,43,35,27,19,11,3,
        61,53,45,37,29,21,13,5, 63,55,47,39,31,23,15,7
    };
    const int FP[64] = {
        40,8,48,16,56,24,64,32, 39,7,47,15,55,23,63,31,
        38,6,46,14,54,22,62,30, 37,5,45,13,53,21,61,29,
        36,4,44,12,52,20,60,28, 35,3,43,11,51,19,59,27,
        34,2,42,10,50,18,58,26, 33,1,41, 9,49,17,57,25
    };
    const int E[48] = {
        32,1,2,3,4,5, 4,5,6,7,8,9, 8,9,10,11,12,13, 12,13,14,15,16,17,
        16,17,18,19,20,21, 20,21,22,23,24,25, 24,25,26,27,28,29, 28,29,30,31,32,1
    };
    const int P[32] = {
        16,7,20,21,29,12,28,17, 1,15,23,26,5,18,31,10,
        2,8,24,14,32,27,3,9, 19,13,30,6,22,11,4,25
    };
    const int PC1[56] = {
        57,49,41,33,25,17, 9, 1,58,50,42,34,26,18,
        10, 2,59,51,43,35,27, 19,11, 3,60,52,44,36,
        63,55,47,39,31,23,15, 7,62,54,46,38,30,22,
        14, 6,61,53,45,37,29, 21,13, 5,28,20,12, 4
    };
    const int PC2[48] = {
        14,17,11,24, 1, 5, 3,28,15, 6,21,10,
        23,19,12, 4,26, 8, 16, 7,27,20,13, 2,
        41,52,31,37,47,55, 30,40,51,45,33,48,
        44,49,39,56,34,53, 46,42,50,36,29,32
    };
    const int SHIFT[16] = { 1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1 };

    const uint8_t SBOX[8][64] = {
        {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7, 0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
          4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0, 15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
        {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10, 3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
          0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15, 13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
        {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8, 13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
          13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7, 1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
        {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15, 13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
          10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4, 3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
        {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9, 14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
          4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14, 11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
        {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11, 10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
          9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6, 4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
        {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1, 13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
          1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2, 6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
        {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7, 1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
          7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8, 2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}
    };

    uint64_t permute(uint64_t src, const int* table, int n, int srcBits)
    {
        uint64_t dst = 0;
        for (int i = 0; i < n; ++i)
        {
            dst <<= 1;
            dst |= (src >> (srcBits - table[i])) & 1ULL;
        }
        return dst;
    }

    uint64_t pack(const uint8_t b[8])
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | b[i];
        return v;
    }

    void unpack(uint64_t v, uint8_t b[8])
    {
        for (int i = 7; i >= 0; --i) { b[i] = static_cast<uint8_t>(v & 0xFF); v >>= 8; }
    }

    void keySchedule(const uint8_t key[8], uint64_t sub[16])
    {
        uint64_t perm = permute(pack(key), PC1, 56, 64);
        uint32_t c = static_cast<uint32_t>((perm >> 28) & 0x0FFFFFFF);
        uint32_t d = static_cast<uint32_t>(perm & 0x0FFFFFFF);
        for (int i = 0; i < 16; ++i)
        {
            int s = SHIFT[i];
            c = ((c << s) | (c >> (28 - s))) & 0x0FFFFFFF;
            d = ((d << s) | (d >> (28 - s))) & 0x0FFFFFFF;
            uint64_t cd = (static_cast<uint64_t>(c) << 28) | d;
            sub[i] = permute(cd, PC2, 48, 56);
        }
    }

    uint32_t feistel(uint32_t r, uint64_t k)
    {
        uint64_t x = permute(r, E, 48, 32) ^ k;
        uint32_t out = 0;
        for (int box = 0; box < 8; ++box)
        {
            uint32_t six = static_cast<uint32_t>((x >> (42 - 6 * box)) & 0x3F);
            int row = ((six & 0x20) >> 4) | (six & 0x01);
            int col = (six >> 1) & 0x0F;
            out = (out << 4) | SBOX[box][row * 16 + col];
        }
        return static_cast<uint32_t>(permute(out, P, 32, 32));
    }

    void crypt(const uint8_t key[8], const uint8_t in[8], uint8_t out[8], bool decrypt)
    {
        uint64_t sub[16];
        keySchedule(key, sub);

        uint64_t ip = permute(pack(in), IP, 64, 64);
        uint32_t l = static_cast<uint32_t>((ip >> 32) & 0xFFFFFFFF);
        uint32_t r = static_cast<uint32_t>(ip & 0xFFFFFFFF);
        for (int round = 0; round < 16; ++round)
        {
            uint32_t prevR = r;
            r = l ^ feistel(r, sub[decrypt ? 15 - round : round]);
            l = prevR;
        }
        uint64_t pre = (static_cast<uint64_t>(r) << 32) | l;
        unpack(permute(pre, FP, 64, 64), out);
    }
}

void encryptBlock(const uint8_t key[8], const uint8_t in[8], uint8_t out[8])
{
    crypt(key, in, out, false);
}

void decryptBlock(const uint8_t key[8], const uint8_t in[8], uint8_t out[8])
{
    crypt(key, in, out, true);
}

std::string vncDecrypt(const uint8_t* cipher, size_t len)
{
    std::string out;
    for (size_t off = 0; off + 8 <= len; off += 8)
    {
        uint8_t plain[8];
        decryptBlock(kVncKey.data(), cipher + off, plain);
        for (uint8_t c : plain)
        {
            if (c == 0) return out;   // VNC passwords are NUL-terminated ASCII
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

bool selfTest()
{
    // 1) FIPS DES known-answer vector proves the core is a correct DES.
    const uint8_t fipsKey[8]   = {0x13,0x34,0x57,0x79,0x9B,0xBC,0xDF,0xF1};
    const uint8_t fipsPlain[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
    const uint8_t fipsCipher[8]= {0x85,0xE8,0x13,0x54,0x0F,0x0A,0xB4,0x05};
    uint8_t enc[8], dec[8];
    encryptBlock(fipsKey, fipsPlain, enc);
    if (std::memcmp(enc, fipsCipher, 8) != 0) return false;
    decryptBlock(fipsKey, fipsCipher, dec);
    if (std::memcmp(dec, fipsPlain, 8) != 0) return false;

    // 2) VNC round trip with the fixed key: encrypting a known password and then
    // decrypting it the way VNCPassView does must recover the plaintext.
    const char* pw = "test1234";               // 8-char VNC password
    uint8_t stored[8], recovered[8];
    encryptBlock(kVncKey.data(), reinterpret_cast<const uint8_t*>(pw), stored);
    decryptBlock(kVncKey.data(), stored, recovered);
    if (std::memcmp(recovered, pw, 8) != 0) return false;
    if (vncDecrypt(stored, 8) != "test1234") return false;

    return true;
}
}
