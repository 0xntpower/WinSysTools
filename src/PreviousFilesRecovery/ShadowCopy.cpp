// ShadowCopy.cpp -- VSS snapshot enumeration. Mirrors sub_140012FFC in the original:
// load VssApi.dll, create the backup components object, initialize it for backup,
// set the context to all snapshots, then walk IVssEnumObject capturing each
// snapshot's device object path, original volume and creation time. The volume
// name is mapped to a drive letter with GetVolumePathNamesForVolumeNameW.
#include "ShadowCopy.h"
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>

#pragma comment(lib, "ole32.lib")

namespace
{
    using CreateVssFn = HRESULT(STDAPICALLTYPE*)(IVssBackupComponents**);
    using FreePropFn = void(STDAPICALLTYPE*)(VSS_SNAPSHOT_PROP*);

    std::wstring mapVolumeToDrive(const std::wstring& volumeName)
    {
        if (volumeName.empty())
        {
            return {};
        }
        wchar_t names[4096]{};
        DWORD len = 0;
        if (::GetVolumePathNamesForVolumeNameW(volumeName.c_str(), names, 4096, &len) && names[0])
        {
            return names; // first mapped path, such as the drive root
        }
        return {};
    }
} // namespace

std::vector<ShadowCopy> EnumerateShadowCopies(wchar_t driveFilter)
{
    std::vector<ShadowCopy> out;

    HMODULE vssApi = ::LoadLibraryW(L"VssApi.dll");
    if (!vssApi)
    {
        return out;
    }

    auto create = reinterpret_cast<CreateVssFn>(
        ::GetProcAddress(vssApi, "?CreateVssBackupComponents@@YAJPEAPEAVIVssBackupComponents@@@Z"));
    auto freeProp = reinterpret_cast<FreePropFn>(::GetProcAddress(vssApi, "VssFreeSnapshotProperties"));

    if (create)
    {
        IVssBackupComponents* backup = nullptr;
        if (SUCCEEDED(create(&backup)) && backup)
        {
            backup->InitializeForBackup();
            backup->SetContext(VSS_CTX_ALL);

            IVssEnumObject* en = nullptr;
            if (SUCCEEDED(backup->Query(GUID_NULL, VSS_OBJECT_NONE, VSS_OBJECT_SNAPSHOT, &en)) && en)
            {
                VSS_OBJECT_PROP prop{};
                ULONG fetched = 0;
                while (en->Next(1, &prop, &fetched) == S_OK && fetched == 1)
                {
                    VSS_SNAPSHOT_PROP& s = prop.Obj.Snap;
                    ShadowCopy sc;
                    if (s.m_pwszSnapshotDeviceObject)
                    {
                        sc.deviceObject = s.m_pwszSnapshotDeviceObject;
                    }
                    if (s.m_pwszOriginalVolumeName)
                    {
                        sc.originalVolume = s.m_pwszOriginalVolumeName;
                    }
                    sc.driveLetter = mapVolumeToDrive(sc.originalVolume);
                    *reinterpret_cast<LONGLONG*>(&sc.creationTime) = s.m_tsCreationTimestamp;

                    if (freeProp)
                    {
                        freeProp(&s);
                    }

                    bool keep = driveFilter == 0 ||
                                (!sc.driveLetter.empty() && towlower(sc.driveLetter[0]) == towlower(driveFilter));
                    if (keep && !sc.deviceObject.empty())
                    {
                        out.push_back(std::move(sc));
                    }
                }
                en->Release();
            }
            backup->Release();
        }
    }

    ::FreeLibrary(vssApi);
    return out;
}
