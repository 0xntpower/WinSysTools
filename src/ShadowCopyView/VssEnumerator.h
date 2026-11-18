// VssEnumerator.h -- enumerate Volume Shadow Copy snapshots and browse the files
// inside them. The original resolves VssApi.dll at run time and drives the VSS COM
// interface IVssBackupComponents, so this does the same: CreateVssBackupComponents
// -> InitializeForBackup -> SetContext(VSS_CTX_ALL) -> Query(VSS_OBJECT_SNAPSHOT),
// then walks the returned snapshots. Files are read straight from each snapshot's
// device object path (\\?\GLOBALROOT\Device\HarddiskVolumeShadowCopyN) with
// FindFirstFile, exactly as the original browses them. COM must already be
// initialised on the calling thread.
#pragma once
#include "Records.h"
#include <vector>
#include <string>

// Enumerate every shadow copy on the machine. Returns an empty list when there are
// none. hrOut carries the VSS HRESULT (S_OK on success, or the first failure).
std::vector<SnapshotRecord> EnumerateSnapshots(const DisplayOptions& opt, HRESULT* hrOut = nullptr);

// List the files and folders directly under folder (for example "\\Windows") inside
// the given snapshot. folder is empty for the snapshot root.
std::vector<FileRecord> EnumerateFiles(const SnapshotRecord& snap,
                                       const std::wstring& folder,
                                       const DisplayOptions& opt);

// Copy a set of selected names (files or folders) from folder inside the snapshot to
// destDir on a local disk. Folders copy recursively. A name that already exists in the
// destination is copied under a numbered name instead of overwriting, matching the
// original. Returns the number of files copied.
int CopyItemsFromSnapshot(const SnapshotRecord& snap,
                          const std::wstring& folder,
                          const std::vector<std::wstring>& names,
                          const std::wstring& destDir);

// /CopyFile: copy one file named by sourcePath (no drive letter, for example
// "Windows\\system32\\config\\SYSTEM") from the shadow copy identified by shadowId
// (a device object path, a number such as "3", or a volume such as "C:\\") into the
// full path destFile. Returns true on success.
bool CopyFileFromShadow(const std::wstring& shadowId,
                        const std::wstring& sourcePath,
                        const std::wstring& destFile);

// Format a FILETIME the way the columns show it: the user locale short date, a space,
// then the user locale time. Local time unless gmt is set. Empty for a zero time.
std::wstring FormatTimeCell(const FILETIME& ft, bool gmt);
