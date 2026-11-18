// AutorunEntry.h -- the data model for the Autoruns recreation.
//
// Autoruns groups autostart items under the location that configures them (a
// registry key or a folder). A Location carries the display label, the key path
// used by Jump to Entry, the location timestamp, the owning tab category and the
// entries found there. An AutorunEntry is one configured item: its on-disk image,
// the version-info description and company, the signature verdict and signer, and
// the enabled state (the checkbox). The Publisher string is formatted from the
// verdict as the original does, (Verified) signer or (Not verified) company.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ar
{
    // The provider categories this recreation enumerates. The out-of-scope
    // categories (Explorer through Office) exist so their tabs render for
    // fidelity, with an empty body.
    enum class Category
    {
        Logon = 0,
        Services,
        Drivers,
        ScheduledTasks,
        Winlogon,
        // --- out of scope, tab shown empty ---
        Explorer,
        InternetExplorer,
        Codecs,
        BootExecute,
        ImageHijacks,
        AppInit,
        KnownDlls,
        Winsock,
        PrintMonitors,
        LsaProviders,
        NetworkProviders,
        Wmi,
        Office,
        Count
    };

    enum class SigState
    {
        Unknown = 0,   // not verified yet
        Verified,      // WinVerifyTrust returned trusted
        NotVerified    // no valid Authenticode signature
    };

    struct AutorunEntry
    {
        std::wstring name;         // Autorun Entry (value name, task name, file name)
        std::wstring description;  // version-info FileDescription
        std::wstring company;      // version-info CompanyName
        std::wstring signer;       // Authenticode subject when verified
        std::wstring version;      // version-info FileVersion
        std::wstring imagePath;    // resolved full path to the image on disk
        std::wstring launchString; // the raw configured command line
        std::wstring timestamp;    // entry location time, formatted
        bool enabled = true;       // the row checkbox state
        SigState sig = SigState::Unknown;

        // The Publisher column text as the original formats it.
        [[nodiscard]] std::wstring Publisher() const
        {
            switch (sig)
            {
            case SigState::Verified:
                return L"(Verified) " + (signer.empty() ? company : signer);
            case SigState::NotVerified:
                return L"(Not Verified) " + company;
            default:
                return company;
            }
        }

        // Hide Microsoft keys off a Microsoft signer or company. A verified
        // signer that names Microsoft, or an unverified image whose company
        // names Microsoft, counts as a Microsoft entry.
        [[nodiscard]] bool IsMicrosoft() const
        {
            const std::wstring& who = (sig == SigState::Verified && !signer.empty()) ? signer : company;
            return who.find(L"Microsoft") != std::wstring::npos;
        }
    };

    struct Location
    {
        std::wstring label;        // display path of the location
        std::wstring jumpPath;     // regedit LastKey path, or a folder path
        std::wstring timestamp;    // location (key or folder) last-write time
        bool isRegistry = true;    // Jump to Entry opens regedit when true
        Category category = Category::Logon;
        std::vector<AutorunEntry> entries;
    };

    inline const wchar_t* CategoryName(Category c)
    {
        switch (c)
        {
        case Category::Logon:            return L"Logon";
        case Category::Services:         return L"Services";
        case Category::Drivers:          return L"Drivers";
        case Category::ScheduledTasks:   return L"Scheduled Tasks";
        case Category::Winlogon:         return L"Winlogon";
        case Category::Explorer:         return L"Explorer";
        case Category::InternetExplorer: return L"Internet Explorer";
        case Category::Codecs:           return L"Codecs";
        case Category::BootExecute:      return L"Boot Execute";
        case Category::ImageHijacks:     return L"Image Hijacks";
        case Category::AppInit:          return L"AppInit";
        case Category::KnownDlls:        return L"KnownDLLs";
        case Category::Winsock:          return L"Winsock Providers";
        case Category::PrintMonitors:    return L"Print Monitors";
        case Category::LsaProviders:     return L"LSA Providers";
        case Category::NetworkProviders: return L"Network Providers";
        case Category::Wmi:              return L"WMI";
        case Category::Office:           return L"Office";
        default:                         return L"";
        }
    }
}
