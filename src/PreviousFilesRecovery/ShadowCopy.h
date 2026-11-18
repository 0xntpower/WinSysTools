// ShadowCopy.h -- enumeration of the volume shadow copies on the machine, built on
// the same VSS COM path the original uses (CreateVssBackupComponents loaded from
// VssApi.dll, then IVssBackupComponents::Query for the snapshot objects).
#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct ShadowCopy
{
    std::wstring deviceObject;   // the shadow-copy device object path (GLOBALROOT)
    std::wstring originalVolume; // the volume GUID name
    std::wstring driveLetter;    // mapped volume path such as C: (may be empty)
    FILETIME creationTime{};     // snapshot creation time (local-convertible UTC)
};

// Enumerate every shadow copy. When driveFilter is a letter the list is limited to
// snapshots whose mapped volume starts with that letter, matching the original's
// per-drive filter. Returns an empty list when VSS is unavailable or none exist.
std::vector<ShadowCopy> EnumerateShadowCopies(wchar_t driveFilter = 0);
