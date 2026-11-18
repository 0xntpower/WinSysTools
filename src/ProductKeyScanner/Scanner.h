// Scanner.h -- collects product keys from the registry, WMI and the BIOS, mirroring
// the data sources recovered from the binary:
//   * registry walk of HKLM\SOFTWARE reading DigitalProductId / DigitalProductId4
//     (sub_1400100E4 + sub_14000FE18), skipping the Perflib/Classes subtrees
//   * an offline SOFTWARE hive on an external drive (RegLoadAppKey)
//   * WMI SoftwareLicensingProduct / SoftwareLicensingService (sub_140010AAC)
//   * the ACPI MSDM firmware table (GetSystemFirmwareTable)
#pragma once
#include <string>
#include <vector>
#include <atomic>
#include "ProductKey.h"

enum class ScanSource { CurrentSystem = 0, ExternalHive = 1 };

struct ScanOptions
{
    ScanSource   source = ScanSource::CurrentSystem;
    std::wstring hivePath;         // SoftwareHivePath (external SOFTWARE file)
    bool         useWMI  = true;   // UseWMI
    bool         useBIOS = true;   // UseBIOS
};

// Enumerate all product keys. `stop`, when non-null and set, cancels early (Esc).
std::vector<ProductKeyRecord> EnumerateProductKeys(const ScanOptions& opts,
                                                   std::atomic<bool>* stop = nullptr);
