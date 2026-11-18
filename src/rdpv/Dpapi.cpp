// Dpapi.cpp -- hex decode plus DPAPI recovery for the .rdp password blob.
#include "Dpapi.h"
#include <dpapi.h>   // CryptProtectData / CryptUnprotectData (crypt32)

// The original gates the DPAPI call on a decoded length greater than 0x20 bytes.
constexpr size_t kMinBlobBytes = 0x20;

std::vector<BYTE> HexToBytes(const std::wstring& hex)
{
    std::vector<BYTE> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        wchar_t pair[3] = { hex[i], hex[i + 1], 0 };
        bytes.push_back(static_cast<BYTE>(wcstoul(pair, nullptr, 16)));
    }
    return bytes;
}

std::wstring RecoverRdpPassword(const std::vector<BYTE>& blob)
{
    if (blob.size() <= kMinBlobBytes)
        return {};

    DATA_BLOB in{ static_cast<DWORD>(blob.size()), const_cast<BYTE*>(blob.data()) };
    DATA_BLOB out{ 0, nullptr };

    // Exactly the original's call: no entropy, no prompt, CRYPTPROTECT_UI_FORBIDDEN.
    if (!::CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &out) || out.pbData == nullptr)
        return {};

    // The plaintext is UTF-16. Length is out.cbData bytes.
    std::wstring result(reinterpret_cast<const wchar_t*>(out.pbData),
                        out.cbData / sizeof(wchar_t));
    // Trim at the first NUL in case the blob was NUL-terminated.
    if (auto nul = result.find(L'\0'); nul != std::wstring::npos)
        result.resize(nul);
    ::LocalFree(out.pbData);
    return result;
}

std::wstring RecoverRdpPasswordHex(const std::wstring& hex)
{
    return RecoverRdpPassword(HexToBytes(hex));
}

namespace dpapi
{
    bool selfTest()
    {
        const std::wstring secret = L"P@ssw0rd-Test-123";
        DATA_BLOB in{
            static_cast<DWORD>((secret.size() + 1) * sizeof(wchar_t)),
            reinterpret_cast<BYTE*>(const_cast<wchar_t*>(secret.c_str()))
        };
        DATA_BLOB out{ 0, nullptr };
        if (!::CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr,
                                CRYPTPROTECT_UI_FORBIDDEN, &out))
            return false;

        std::vector<BYTE> blob(out.pbData, out.pbData + out.cbData);
        ::LocalFree(out.pbData);

        // Round-trip through the exact recovery path, then feed the blob straight in.
        if (RecoverRdpPassword(blob) != secret)
            return false;

        // Also verify the hex decoder on a fixed vector.
        auto b = HexToBytes(L"00FF10aB");
        return b.size() == 4 && b[0] == 0x00 && b[1] == 0xFF && b[2] == 0x10 && b[3] == 0xAB;
    }
}
