// Decoder.h -- the two product-key decoders recovered from ProductKeyScanner.
//
// DecodeProductKeyRegion: the classic base-24 DigitalProductId decode over the
// 15-byte key region, including the Windows 8+ variant (fourth-from-last byte
// bit-3 "isWin8" flag, nibble-swap, and the 'N' re-insertion). This is a faithful
// port of sub_14000FB8C (confirmed line-for-line against rea/Ghidra).
//
// ExtractKeyRegionOffset: the blob-size dispatch from sub_14000FE18 that locates
// the 15-byte region inside a DigitalProductId (offset 52) or DigitalProductId4
// (offset 808) REG_BINARY blob.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Decode a 15-byte key region into a product key string (e.g.
// "BBBBB-BBBBB-BBBBB-BBBBB-BBBBB"). Returns empty when the region is all zero.
std::wstring DecodeProductKeyRegion(const uint8_t region[15]);

// Decode a product key directly from a DigitalProductId / DigitalProductId4
// REG_BINARY blob. Returns empty when the blob is too small or holds no key.
std::wstring DecodeProductKeyBlob(const uint8_t* blob, size_t size);
