// Des.h -- the VNC password obfuscation core.
//
// VNC stores a password by DES-encrypting it (ECB) with a fixed, well-known 8-byte
// key. VNCPassView recovers the plaintext by DES-decrypting each 8-byte block with
// that same key. The original binary ships a d3des (Richard Outerbridge) decryptor
// whose key schedule is pre-set for decryption and whose key bytes are written one
// at a time in sub_4088EF -- exactly the constant below.
//
// This is a clean, standard DES rather than a transliteration of the d3des tables.
// It is behaviourally identical for ECB because d3des is standard DES. selfTest()
// proves the core against the FIPS DES test vector and proves the VNC round trip.
#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace des
{
    // Recovered from VNCPassView sub_4088EF -- the fixed VNC DES key.
    inline constexpr std::array<uint8_t, 8> kVncKey = {
        0x17, 0x52, 0x6B, 0x06, 0x23, 0x4E, 0x58, 0x07
    };

    void encryptBlock(const uint8_t key[8], const uint8_t in[8], uint8_t out[8]);
    void decryptBlock(const uint8_t key[8], const uint8_t in[8], uint8_t out[8]);

    // Decrypt a stored VNC blob: every whole 8-byte block is DES-decrypted with
    // kVncKey. The plaintext is an ASCII password (VNC caps it at 8 characters),
    // so the result is cut at the first NUL.
    [[nodiscard]] std::string vncDecrypt(const uint8_t* cipher, size_t len);

    [[nodiscard]] bool selfTest();
}
