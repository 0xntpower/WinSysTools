// VncEnumerator.cpp -- reconstructs VNCPassView's password scan.
// Mirrors sub_40867A (registry scan of the known VNC server keys), sub_4089B4
// (read the REG_BINARY "Password" value), sub_4088EF (DES-decrypt it) and
// sub_4087BF / sub_40806F (UltraVNC's ultravnc.ini passwd= value), written as
// straight-line C++ over EnumerateVncPasswords().
#include "VncPassword.h"
#include "RegKey.h"
#include "Des.h"
#include <windows.h>
#include <string>

namespace
{
    // Read the encrypted "Password" REG_BINARY value, decrypt it, and append a row.
    void addFromRegistry(std::vector<VncPassword>& out, HKEY root,
                         const std::wstring& subKey, Hive hive, VncVersion ver)
    {
        RegKey key;
        if (!key.open(root, subKey)) return;
        auto blob = key.queryBinary(L"Password");
        if (!blob || blob->size() < 8 || blob->size() >= 0xFF) return;

        VncPassword rec;
        rec.hive = hive;
        rec.version = ver;
        std::string pw = des::vncDecrypt(blob->data(), blob->size());
        rec.password.assign(pw.begin(), pw.end());
        out.push_back(std::move(rec));
    }

    // sub_403382: parse a hex string ("1a2b3c..") into bytes, skipping spaces.
    std::vector<BYTE> hexToBytes(const std::wstring& hex)
    {
        std::vector<BYTE> bytes;
        for (size_t i = 0; i + 1 < hex.size(); )
        {
            if (hex[i] == L' ') { ++i; continue; }
            wchar_t pair[3] = { hex[i], hex[i + 1], 0 };
            bytes.push_back(static_cast<BYTE>(wcstoul(pair, nullptr, 16)));
            i += 2;
        }
        return bytes;
    }

    // sub_4087BF / sub_40806F: UltraVNC stores passwd= (hex) under [ultravnc] in
    // ultravnc.ini, found in its install directory (registry InstallLocation).
    void addFromUltraVnc(std::vector<VncPassword>& out)
    {
        RegKey key;
        if (!key.open(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Ultravnc2_is1"))
            return;
        auto install = key.queryString(L"InstallLocation");
        if (!install || install->empty()) return;

        std::wstring iniPath = *install;
        if (iniPath.back() != L'\\') iniPath += L'\\';
        iniPath += L"ultravnc.ini";
        if (iniPath.size() >= MAX_PATH) iniPath = L"ultravnc.ini";  // falls back to %windir%

        wchar_t buf[1024] = {};
        ::GetPrivateProfileStringW(L"ultravnc", L"passwd", L"", buf,
                                   static_cast<DWORD>(std::size(buf)), iniPath.c_str());
        if (buf[0] == 0) return;

        std::vector<BYTE> blob = hexToBytes(buf);
        if (blob.size() < 8) return;

        VncPassword rec;
        rec.hive = Hive::System;
        rec.version = VncVersion::UltraVNC;
        std::string pw = des::vncDecrypt(blob.data(), blob.size());
        rec.password.assign(pw.begin(), pw.end());
        out.push_back(std::move(rec));
    }
}

std::vector<VncPassword> EnumerateVncPasswords()
{
    std::vector<VncPassword> out;

    // Order and (hive, version) codes exactly as sub_40867A scans them.
    addFromRegistry(out, HKEY_CURRENT_USER,  L"SOFTWARE\\RealVNC\\WinVNC4",       Hive::CurrentUser, VncVersion::RealVnc4);
    addFromRegistry(out, HKEY_LOCAL_MACHINE, L"SOFTWARE\\RealVNC\\WinVNC4",       Hive::System,      VncVersion::RealVnc4);
    addFromRegistry(out, HKEY_CURRENT_USER,  L"SOFTWARE\\ORL\\WinVNC3",           Hive::CurrentUser, VncVersion::Vnc3);
    addFromRegistry(out, HKEY_CURRENT_USER,  L"SOFTWARE\\ORL\\WinVNC3\\Default",  Hive::CurrentUser, VncVersion::Vnc3);
    addFromRegistry(out, HKEY_LOCAL_MACHINE, L"SOFTWARE\\ORL\\WinVNC3",           Hive::System,      VncVersion::Vnc3);
    addFromRegistry(out, HKEY_LOCAL_MACHINE, L"SOFTWARE\\ORL\\WinVNC3\\Default",  Hive::System,      VncVersion::Vnc3);
    addFromRegistry(out, HKEY_CURRENT_USER,  L"SOFTWARE\\TightVNC\\Server",       Hive::CurrentUser, VncVersion::TightVNC);
    addFromRegistry(out, HKEY_LOCAL_MACHINE, L"SOFTWARE\\TightVNC\\Server",       Hive::System,      VncVersion::TightVNC);

    addFromUltraVnc(out);

    return out;
}
