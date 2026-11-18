// VncPassword.h -- one row of the VNCPassView list, reconstructed from the record
// the original builds in sub_4088EF (decrypted password + two type codes) and reads
// back in the record GetItemText at 0x401821.
#pragma once
#include <string>
#include <vector>

// Hive codes exactly as the original passes them (sub_40867A / sub_4087BF):
// Password Type column = module string 1100 + hive  ->  1101 "System" / 1102 "Current User".
enum class Hive { System = 1, CurrentUser = 2 };

// Version codes exactly as the original passes them. The Version column prints
// _itoa(code) and the original prints nothing when the code is 0 (TightVNC).
enum class VncVersion { TightVNC = 0, UltraVNC = 1, Vnc3 = 3, RealVnc4 = 4 };

struct VncPassword
{
    Hive       hive    = Hive::System;        // col 0  Password Type
    VncVersion version = VncVersion::Vnc3;    // col 1  Version
    std::wstring password;                    // col 2  Password (decrypted)

    // Display text per visible column index, mirroring the record getter at 0x401821.
    std::wstring column(int col) const
    {
        switch (col)
        {
        case 0:  return hive == Hive::System ? L"System" : L"Current User";
        case 1:  return version == VncVersion::TightVNC ? std::wstring()
                                 : std::to_wstring(static_cast<int>(version));
        case 2:  return password;
        default: return {};
        }
    }
};

[[nodiscard]] std::vector<VncPassword> EnumerateVncPasswords();
