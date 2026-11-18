// NtObjMgr.h -- the Object Manager namespace walker that backs the WinObj recreation.
//
// WinObj browses the NT Object Manager namespace through the native ntdll
// routines, none of which have import-library stubs, so they are resolved by
// name at run time (the original resolves them the same way, through
// GetProcAddress on ntdll). This header exposes a small typed API over them:
//
//   EnumerateDirectory  NtOpenDirectoryObject + NtQueryDirectoryObject, one
//                       OBJECT_DIRECTORY_INFORMATION record per child, each
//                       carrying the child Name and its object TypeName.
//   ResolveSymlink      NtOpenSymbolicLinkObject + NtQuerySymbolicLinkObject,
//                       the target path a SymbolicLink object points at.
//   QueryDetails        NtQueryObject(ObjectBasicInformation) over a handle
//                       opened for the object's type, for the Properties page.
//   OpenForQuery        the per-type open dispatch, also used by the Security
//                       page to hand a handle to NtQuerySecurityObject.
#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace nt
{
    // One row in a directory listing: the child's name, its object type, and
    // (for a SymbolicLink) the resolved target. type drives the Type column.
    struct ObjEntry
    {
        std::wstring name;
        std::wstring type;
        std::wstring symlink;   // target, only for SymbolicLink entries
        bool isDirectory = false;
    };

    // The Basic Information shown on the Properties Details page. Filled only
    // when a handle to the object can be opened, matching the original, which
    // leaves the fields blank for objects it cannot open.
    struct ObjDetails
    {
        bool haveBasic = false;
        unsigned long attributes = 0;
        unsigned long handleCount = 0;
        unsigned long pointerCount = 0;
        unsigned long pagedQuota = 0;
        unsigned long nonPagedQuota = 0;
        bool permanent = false;
        bool exclusive = false;

        bool isSymlink = false;
        std::wstring symlinkTarget;
    };

    // Enumerate one object directory. path is an absolute Object Manager path
    // such as L"\\" or L"\\BaseNamedObjects". When resolveSymlinks is set, each
    // SymbolicLink child is opened and its target filled in (the list view wants
    // the target column, the tree build does not). Returns the children, or an
    // empty vector when the directory cannot be opened.
    std::vector<ObjEntry> EnumerateDirectory(const std::wstring& path, bool resolveSymlinks);

    // Resolve a SymbolicLink object to its target string. Returns false when the
    // link cannot be opened or queried.
    bool ResolveSymlink(const std::wstring& path, std::wstring& target);

    // Open a handle to the named object using the open routine for its type,
    // requesting access. Returns a valid handle or nullptr. The caller closes it
    // with CloseHandle. Only the common, cleanly single-call types are handled.
    HANDLE OpenForQuery(const std::wstring& path, const std::wstring& type, ACCESS_MASK access);

    // Fill the Details struct for one object. Always returns a struct with the
    // symlink target when type is SymbolicLink. The Basic Information is present
    // only when a handle could be opened.
    ObjDetails QueryDetails(const std::wstring& path, const std::wstring& type);

    // Query an opened object's security descriptor (self-relative) for the given
    // SECURITY_INFORMATION. Returns a LocalAlloc'd buffer the caller frees with
    // LocalFree, or nullptr. Used by the Security property page.
    PSECURITY_DESCRIPTOR QuerySecurity(HANDLE object, SECURITY_INFORMATION si);

    // Apply a security descriptor to an opened object. Returns true on success.
    bool ApplySecurity(HANDLE object, SECURITY_INFORMATION si, PSECURITY_DESCRIPTOR sd);

    // True when the current process token is elevated (used for the window title
    // suffix and to decide whether the Run As Administrator command is offered).
    bool IsElevated();
}
