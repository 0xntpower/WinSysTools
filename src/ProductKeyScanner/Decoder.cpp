// Decoder.cpp -- base-24 DigitalProductId decode plus the Windows 8+ variant.
// Faithful port of sub_14000FB8C and the blob dispatch in sub_14000FE18.
#include "Decoder.h"
#include <cstring>

namespace
{
    // The 24-symbol alphabet strcpy'd on the stack in sub_14000FB8C.
    constexpr char kAlphabet[] = "BCDFGHJKMPQRTVWXY2346789";  // 24 symbols

    std::wstring widen(const char* s)
    {
        std::wstring w;
        for (; *s; ++s) w += static_cast<wchar_t>(static_cast<unsigned char>(*s));
        return w;
    }
}

std::wstring DecodeProductKeyRegion(const uint8_t region[15])
{
    uint8_t zero[15] = { 0 };
    if (std::memcmp(region, zero, 15) == 0)
        return {};  // no key stored (sub_14000FB8C returns 0)

    uint8_t key[15];
    std::memcpy(key, region, 15);

    // Windows 8+ detection: bit 3 of the last key byte. When set, clear it and
    // fold in (byte >> 1) & 0x78, then re-insert an 'N' after decoding.
    const bool isWin8 = (key[14] & 8) != 0;
    if (isWin8)
        key[14] = static_cast<uint8_t>((key[14] & 0xF7) | ((key[14] >> 1) & 0x78));

    char out[32] = { 0 };   // the 29-char dashed key (a2 in the original)
    char src[32] = { 0 };   // the dashless accumulator used by the Win8 path
    int  srcIdx = 24;       // v9: fills src backwards from index 23
    int  lastRem = 0;       // v6: remainder of the final (most significant) digit
    int  counter = 29;      // v10: paired with the output position for dash spacing

    for (int outPos = 28; outPos >= 0; --outPos, --counter)
    {
        if (counter % 6 == 0)
        {
            out[outPos] = '-';
            continue;
        }

        // Divide the 15-byte big-endian-of-little number by 24, low byte first.
        unsigned rem = 0;
        for (int i = 14; i >= 0; --i)
        {
            unsigned acc = static_cast<unsigned>(key[i]) | (rem << 8);
            key[i] = static_cast<uint8_t>(acc / 24);
            rem = acc % 24;
        }

        if (isWin8)
        {
            if (srcIdx - 1 >= 0)                // the original's lone src[-1] write
                src[srcIdx - 1] = kAlphabet[rem];  // is discarded, so guard it out
            --srcIdx;
        }
        lastRem = static_cast<int>(rem);
        out[outPos] = kAlphabet[rem];
    }

    if (isWin8 && lastRem < 30)
    {
        // src now holds 24 symbols at [0..23]. Insert 'N' at position lastRem,
        // giving 25 symbols, then regroup into five dash-separated groups of five.
        char merged[64] = { 0 };
        std::memcpy(merged, src, static_cast<size_t>(lastRem));
        merged[lastRem] = 'N';
        std::strcat(merged, src + lastRem);

        char res[32] = { 0 };
        res[5] = res[11] = res[17] = res[23] = '-';
        std::memcpy(res, merged, 5);
        std::memcpy(res + 6, merged + 5, 5);
        std::memcpy(res + 12, merged + 10, 5);
        std::memcpy(res + 18, merged + 15, 5);
        std::memcpy(res + 24, merged + 20, 5);
        res[29] = 0;
        std::memcpy(out, res, 30);
    }

    return widen(out);
}

std::wstring DecodeProductKeyBlob(const uint8_t* blob, size_t size)
{
    // Region dispatch from sub_14000FE18: a DigitalProductId blob (>= 0x44 bytes)
    // keeps the 15-byte region at offset 52, a DigitalProductId4 blob (>= 0x338)
    // keeps it at offset 808, and a bare 16-byte blob keeps it at offset 0.
    size_t off;
    if (size >= 0x338)
        off = 808;
    else if (size == 16)
        off = 0;
    else if (size >= 0x44)
        off = 52;
    else
        return {};

    if (off + 15 > size)
        return {};
    return DecodeProductKeyRegion(blob + off);
}
