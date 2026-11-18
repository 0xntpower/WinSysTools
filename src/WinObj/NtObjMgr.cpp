// NtObjMgr.cpp -- native Object Manager access for the WinObj recreation.
//
// The native calls are resolved once from ntdll by name. The structures below
// are the documented native shapes (UNICODE_STRING, OBJECT_ATTRIBUTES,
// OBJECT_DIRECTORY_INFORMATION, OBJECT_BASIC_INFORMATION) declared locally so
// the translation unit does not depend on the WDK headers.
#include "NtObjMgr.h"

#include <vector>

namespace
{
    // ---- native types ----------------------------------------------------
    using NTSTATUS = LONG;

    constexpr NTSTATUS kStatusSuccess = 0;
    constexpr NTSTATUS kStatusMoreEntries = 0x00000105;
    constexpr NTSTATUS kStatusNoMoreEntries = 0x8000001A;
    constexpr NTSTATUS kStatusBufferTooSmall = 0xC0000023;

    constexpr ULONG kObjInherit = 0x00000002;
    constexpr ULONG kObjPermanent = 0x00000010;
    constexpr ULONG kObjExclusive = 0x00000020;

    constexpr ACCESS_MASK kDirectoryQuery = 0x0001;
    constexpr ACCESS_MASK kSymlinkQuery = 0x0001;

    struct UNICODE_STRING
    {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR Buffer;
    };

    struct OBJECT_ATTRIBUTES
    {
        ULONG Length;
        HANDLE RootDirectory;
        UNICODE_STRING* ObjectName;
        ULONG Attributes;
        PVOID SecurityDescriptor;
        PVOID SecurityQualityOfService;
    };

    struct OBJECT_DIRECTORY_INFORMATION
    {
        UNICODE_STRING Name;
        UNICODE_STRING TypeName;
    };

    struct OBJECT_BASIC_INFORMATION
    {
        ULONG Attributes;
        ACCESS_MASK GrantedAccess;
        ULONG HandleCount;
        ULONG PointerCount;
        ULONG PagedPoolCharge;
        ULONG NonPagedPoolCharge;
        ULONG Reserved[3];
        ULONG NameInfoSize;
        ULONG TypeInfoSize;
        ULONG SecurityDescriptorSize;
        LARGE_INTEGER CreationTime;
    };

    constexpr ULONG kObjectBasicInformation = 0;

    // ---- function pointers -----------------------------------------------
    using FnOpenByName = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, OBJECT_ATTRIBUTES*);
    using FnQueryDirectory = NTSTATUS(NTAPI*)(HANDLE, PVOID, ULONG, BOOLEAN, BOOLEAN, PULONG, PULONG);
    using FnQuerySymlink = NTSTATUS(NTAPI*)(HANDLE, UNICODE_STRING*, PULONG);
    using FnQueryObject = NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    using FnQuerySecurity = NTSTATUS(NTAPI*)(HANDLE, SECURITY_INFORMATION, PSECURITY_DESCRIPTOR, ULONG, PULONG);
    using FnSetSecurity = NTSTATUS(NTAPI*)(HANDLE, SECURITY_INFORMATION, PSECURITY_DESCRIPTOR);

    struct Ntdll
    {
        FnOpenByName OpenDirectory = nullptr;
        FnQueryDirectory QueryDirectory = nullptr;
        FnOpenByName OpenSymlink = nullptr;
        FnQuerySymlink QuerySymlink = nullptr;
        FnQueryObject QueryObject = nullptr;
        FnQuerySecurity QuerySecurity = nullptr;
        FnSetSecurity SetSecurity = nullptr;

        // Per-type open routines. Every one of these shares the by-name open
        // signature (handle, access, attributes).
        FnOpenByName OpenMutant = nullptr;
        FnOpenByName OpenEvent = nullptr;
        FnOpenByName OpenSemaphore = nullptr;
        FnOpenByName OpenTimer = nullptr;
        FnOpenByName OpenSection = nullptr;
        FnOpenByName OpenJob = nullptr;
        FnOpenByName OpenKeyedEvent = nullptr;

        Ntdll()
        {
            HMODULE h = ::GetModuleHandleW(L"ntdll.dll");
            if (!h)
            {
                h = ::LoadLibraryW(L"ntdll.dll");
            }
            if (!h)
            {
                return;
            }
            auto get = [&](const char* n)
            {
                return ::GetProcAddress(h, n);
            };
            OpenDirectory = reinterpret_cast<FnOpenByName>(get("NtOpenDirectoryObject"));
            QueryDirectory = reinterpret_cast<FnQueryDirectory>(get("NtQueryDirectoryObject"));
            OpenSymlink = reinterpret_cast<FnOpenByName>(get("NtOpenSymbolicLinkObject"));
            QuerySymlink = reinterpret_cast<FnQuerySymlink>(get("NtQuerySymbolicLinkObject"));
            QueryObject = reinterpret_cast<FnQueryObject>(get("NtQueryObject"));
            QuerySecurity = reinterpret_cast<FnQuerySecurity>(get("NtQuerySecurityObject"));
            SetSecurity = reinterpret_cast<FnSetSecurity>(get("NtSetSecurityObject"));
            OpenMutant = reinterpret_cast<FnOpenByName>(get("NtOpenMutant"));
            OpenEvent = reinterpret_cast<FnOpenByName>(get("NtOpenEvent"));
            OpenSemaphore = reinterpret_cast<FnOpenByName>(get("NtOpenSemaphore"));
            OpenTimer = reinterpret_cast<FnOpenByName>(get("NtOpenTimer"));
            OpenSection = reinterpret_cast<FnOpenByName>(get("NtOpenSection"));
            OpenJob = reinterpret_cast<FnOpenByName>(get("NtOpenJobObject"));
            OpenKeyedEvent = reinterpret_cast<FnOpenByName>(get("NtOpenKeyedEvent"));
        }
    };

    const Ntdll& api()
    {
        static const Ntdll instance;
        return instance;
    }

    // Fill an OBJECT_ATTRIBUTES for an absolute path. The UNICODE_STRING it
    // points at must outlive the call, so the caller owns both.
    void initObjectName(UNICODE_STRING& us, OBJECT_ATTRIBUTES& oa, const std::wstring& path)
    {
        us.Buffer = const_cast<PWSTR>(path.c_str());
        us.Length = static_cast<USHORT>(path.size() * sizeof(wchar_t));
        us.MaximumLength = us.Length;

        oa.Length = sizeof(oa);
        oa.RootDirectory = nullptr;
        oa.ObjectName = &us;
        oa.Attributes = 0x00000040; // OBJ_CASE_INSENSITIVE
        oa.SecurityDescriptor = nullptr;
        oa.SecurityQualityOfService = nullptr;
    }

    std::wstring fromCounted(const UNICODE_STRING& us)
    {
        if (!us.Buffer || us.Length == 0)
        {
            return std::wstring();
        }
        return std::wstring(us.Buffer, us.Length / sizeof(wchar_t));
    }

    bool success(NTSTATUS s)
    {
        return s >= 0;
    }
} // namespace

namespace nt
{
    std::vector<ObjEntry> EnumerateDirectory(const std::wstring& path, bool resolveSymlinks)
    {
        std::vector<ObjEntry> out;
        const Ntdll& nt = api();
        if (!nt.OpenDirectory || !nt.QueryDirectory)
        {
            return out;
        }

        UNICODE_STRING us{};
        OBJECT_ATTRIBUTES oa{};
        initObjectName(us, oa, path);

        HANDLE dir = nullptr;
        if (!success(nt.OpenDirectory(&dir, kDirectoryQuery, &oa)) || !dir)
        {
            return out;
        }

        std::vector<BYTE> buffer(64 * 1024);
        ULONG context = 0;
        BOOLEAN restart = TRUE;

        for (;;)
        {
            ULONG retLen = 0;
            NTSTATUS st = nt.QueryDirectory(dir, buffer.data(), static_cast<ULONG>(buffer.size()), FALSE, restart,
                                            &context, &retLen);
            restart = FALSE;

            if (st == kStatusNoMoreEntries)
            {
                break;
            }
            if (st == kStatusBufferTooSmall || retLen > buffer.size())
            {
                buffer.resize(retLen ? retLen + 4096 : buffer.size() * 2);
                restart = (out.empty()) ? TRUE : restart;
                continue;
            }
            if (!success(st) && st != kStatusMoreEntries)
            {
                break;
            }

            auto* info = reinterpret_cast<OBJECT_DIRECTORY_INFORMATION*>(buffer.data());
            size_t added = 0;
            for (; info->Name.Buffer != nullptr; ++info)
            {
                ObjEntry e;
                e.name = fromCounted(info->Name);
                e.type = fromCounted(info->TypeName);
                e.isDirectory = (_wcsicmp(e.type.c_str(), L"Directory") == 0);
                out.push_back(std::move(e));
                ++added;
            }

            if (st != kStatusMoreEntries)
            {
                break;
            }
            if (added == 0)
            {
                break; // guard against a non-advancing call
            }
        }

        ::CloseHandle(dir);

        if (resolveSymlinks)
        {
            const std::wstring base = (path == L"\\") ? std::wstring() : path;
            for (ObjEntry& e : out)
            {
                if (_wcsicmp(e.type.c_str(), L"SymbolicLink") == 0)
                {
                    std::wstring target;
                    if (ResolveSymlink(base + L"\\" + e.name, target))
                    {
                        e.symlink = target;
                    }
                }
            }
        }

        return out;
    }

    bool ResolveSymlink(const std::wstring& path, std::wstring& target)
    {
        const Ntdll& nt = api();
        if (!nt.OpenSymlink || !nt.QuerySymlink)
        {
            return false;
        }

        UNICODE_STRING us{};
        OBJECT_ATTRIBUTES oa{};
        initObjectName(us, oa, path);

        HANDLE link = nullptr;
        if (!success(nt.OpenSymlink(&link, kSymlinkQuery, &oa)) || !link)
        {
            return false;
        }

        std::vector<wchar_t> buf(2048);
        UNICODE_STRING result{};
        result.Buffer = buf.data();
        result.Length = 0;
        result.MaximumLength = static_cast<USHORT>(buf.size() * sizeof(wchar_t));

        ULONG retLen = 0;
        NTSTATUS st = nt.QuerySymlink(link, &result, &retLen);
        ::CloseHandle(link);
        if (!success(st))
        {
            return false;
        }

        target.assign(result.Buffer, result.Length / sizeof(wchar_t));
        return true;
    }

    HANDLE OpenForQuery(const std::wstring& path, const std::wstring& type, ACCESS_MASK access)
    {
        const Ntdll& nt = api();

        FnOpenByName open = nullptr;
        if (_wcsicmp(type.c_str(), L"Directory") == 0)
        {
            open = nt.OpenDirectory;
        }
        else if (_wcsicmp(type.c_str(), L"SymbolicLink") == 0)
        {
            open = nt.OpenSymlink;
        }
        else if (_wcsicmp(type.c_str(), L"Mutant") == 0)
        {
            open = nt.OpenMutant;
        }
        else if (_wcsicmp(type.c_str(), L"Event") == 0)
        {
            open = nt.OpenEvent;
        }
        else if (_wcsicmp(type.c_str(), L"Semaphore") == 0)
        {
            open = nt.OpenSemaphore;
        }
        else if (_wcsicmp(type.c_str(), L"Timer") == 0)
        {
            open = nt.OpenTimer;
        }
        else if (_wcsicmp(type.c_str(), L"Section") == 0)
        {
            open = nt.OpenSection;
        }
        else if (_wcsicmp(type.c_str(), L"Job") == 0)
        {
            open = nt.OpenJob;
        }
        else if (_wcsicmp(type.c_str(), L"KeyedEvent") == 0)
        {
            open = nt.OpenKeyedEvent;
        }

        if (!open)
        {
            return nullptr;
        }

        UNICODE_STRING us{};
        OBJECT_ATTRIBUTES oa{};
        initObjectName(us, oa, path);

        HANDLE h = nullptr;
        if (!success(open(&h, access, &oa)))
        {
            return nullptr;
        }
        return h;
    }

    ObjDetails QueryDetails(const std::wstring& path, const std::wstring& type)
    {
        ObjDetails d;
        const Ntdll& nt = api();

        if (_wcsicmp(type.c_str(), L"SymbolicLink") == 0)
        {
            d.isSymlink = true;
            ResolveSymlink(path, d.symlinkTarget);
        }

        // READ_CONTROL alone is enough for ObjectBasicInformation. The per-type
        // query access makes the open succeed on types that reject a bare
        // READ_CONTROL request.
        ACCESS_MASK access = READ_CONTROL | 0x0001;
        HANDLE h = OpenForQuery(path, type, access);
        if (!h)
        {
            h = OpenForQuery(path, type, READ_CONTROL);
        }
        if (!h || !nt.QueryObject)
        {
            return d;
        }

        OBJECT_BASIC_INFORMATION obi{};
        ULONG retLen = 0;
        NTSTATUS st = nt.QueryObject(h, kObjectBasicInformation, &obi, sizeof(obi), &retLen);
        if (success(st))
        {
            d.haveBasic = true;
            d.attributes = obi.Attributes;
            d.handleCount = obi.HandleCount;
            d.pointerCount = obi.PointerCount;
            d.pagedQuota = obi.PagedPoolCharge;
            d.nonPagedQuota = obi.NonPagedPoolCharge;
            d.permanent = (obi.Attributes & kObjPermanent) != 0;
            d.exclusive = (obi.Attributes & kObjExclusive) != 0;
        }
        ::CloseHandle(h);
        return d;
    }

    PSECURITY_DESCRIPTOR QuerySecurity(HANDLE object, SECURITY_INFORMATION si)
    {
        const Ntdll& nt = api();
        if (!object || !nt.QuerySecurity)
        {
            return nullptr;
        }

        ULONG len = 0;
        NTSTATUS st = nt.QuerySecurity(object, si, nullptr, 0, &len);
        if (len == 0)
        {
            return nullptr;
        }

        auto* sd = static_cast<PSECURITY_DESCRIPTOR>(::LocalAlloc(LPTR, len));
        if (!sd)
        {
            return nullptr;
        }
        st = nt.QuerySecurity(object, si, sd, len, &len);
        if (!success(st))
        {
            ::LocalFree(sd);
            return nullptr;
        }
        return sd;
    }

    bool ApplySecurity(HANDLE object, SECURITY_INFORMATION si, PSECURITY_DESCRIPTOR sd)
    {
        const Ntdll& nt = api();
        if (!object || !nt.SetSecurity)
        {
            return false;
        }
        return success(nt.SetSecurity(object, si, sd));
    }

    bool IsElevated()
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return false;
        }
        TOKEN_ELEVATION elev{};
        DWORD cb = 0;
        bool result = false;
        if (::GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &cb))
        {
            result = elev.TokenIsElevated != 0;
        }
        ::CloseHandle(token);
        return result;
    }
} // namespace nt
