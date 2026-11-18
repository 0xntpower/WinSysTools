// Dpapi.h -- the .rdp "password 51:b:<hex>" recovery core.
// Mirrors sub_407FCE: the hex value is decoded two chars per byte, and when the blob
// is longer than 0x20 bytes it is handed to CryptUnprotectData with no entropy and
// the CRYPTPROTECT_UI_FORBIDDEN flag. The DPAPI output is UTF-16 plaintext.
#pragma once
#include <windows.h>
#include <string>
#include <vector>

// sub_407FCE's inner loop: parse a hex string into bytes, two wide chars per byte.
[[nodiscard]] std::vector<BYTE> HexToBytes(const std::wstring& hex);

// DPAPI-decrypt an .rdp password blob. Returns the recovered UTF-16 plaintext, or an
// empty string when the blob is too short (<= 0x20 bytes, as the original gates) or
// CryptUnprotectData fails (for example a blob sealed by a different user).
[[nodiscard]] std::wstring RecoverRdpPassword(const std::vector<BYTE>& blob);

// Convenience: decode a hex value and recover it in one step.
[[nodiscard]] std::wstring RecoverRdpPasswordHex(const std::wstring& hex);

namespace dpapi
{
    // /selftest -- round-trips a known password through CryptProtectData and the
    // recovery path above. Returns true on a match.
    [[nodiscard]] bool selfTest();
}
