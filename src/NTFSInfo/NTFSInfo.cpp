// NTFSInfo - NTFS volume information dump.
//
// Source-accurate C++20 console recreation of the Sysinternals NtfsInfo v1.2
// utility. It opens a raw handle to the chosen volume, issues
// FSCTL_GET_NTFS_VOLUME_DATA through NtFsControlFile, and prints the volume,
// allocation, MFT and metadata-file figures in the original's exact layout.
//
// Reverse-engineered from targets/sysinternals/ntfsinfo64.exe. See
// analysis/NTFSInfo.md for the mapping of each printed field to the
// NTFS_VOLUME_DATA_BUFFER member it comes from.

#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "version.lib")

namespace
{
    // FSCTL_GET_NTFS_VOLUME_DATA == 0x00090064. The original resolves and calls
    // ntdll!NtFsControlFile directly rather than DeviceIoControl, which is why
    // its failure path reports NTSTATUS names.
    constexpr ULONG kFsctlGetNtfsVolumeData = 0x00090064u;

    constexpr long long kBytesPerMegabyte = 0x100000; // 1024 * 1024

    typedef struct _IO_STATUS_BLOCK_LOCAL
    {
        union
        {
            LONG Status;
            PVOID Pointer;
        } u;
        ULONG_PTR Information;
    } IO_STATUS_BLOCK_LOCAL;

    using NtFsControlFileFn = LONG(NTAPI*)(
        HANDLE FileHandle,
        HANDLE Event,
        PVOID ApcRoutine,
        PVOID ApcContext,
        IO_STATUS_BLOCK_LOCAL* IoStatusBlock,
        ULONG FsControlCode,
        PVOID InputBuffer,
        ULONG InputBufferLength,
        PVOID OutputBuffer,
        ULONG OutputBufferLength);

    NtFsControlFileFn g_NtFsControlFile = nullptr;

    // The exact table the original walks, 11 metadata file names in order.
    constexpr const char* kMetaDataFiles[] = {
        "$mft",   "$mftmirr", "$logfile", "$volume",  "$attrdef", "$bitmap",
        "$boot",  "$badclus", "$quota",   "$badclust", "$upcase",
    };

    // RAII wrapper over a Win32 handle closed with CloseHandle.
    class FileHandle
    {
    public:
        explicit FileHandle(HANDLE handle) noexcept : handle_(handle) {}
        ~FileHandle()
        {
            if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr)
            {
                CloseHandle(handle_);
            }
        }
        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;

        [[nodiscard]] HANDLE get() const noexcept { return handle_; }
        [[nodiscard]] bool valid() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }

    private:
        HANDLE handle_;
    };

    // RAII wrapper over a registry key closed with RegCloseKey.
    class RegKey
    {
    public:
        RegKey() noexcept = default;
        ~RegKey()
        {
            if (key_ != nullptr)
            {
                RegCloseKey(key_);
            }
        }
        RegKey(const RegKey&) = delete;
        RegKey& operator=(const RegKey&) = delete;

        [[nodiscard]] HKEY* address() noexcept { return &key_; }
        [[nodiscard]] HKEY get() const noexcept { return key_; }

    private:
        HKEY key_ = nullptr;
    };

    [[noreturn]] void PrintUsageAndExit(const char* program, int code)
    {
        std::printf("Usage: %s [-nobanner] [-accepteula] <drive letter>\n", program);
        std::printf("-nobanner    Do not display the startup banner and copyright message.\n");
        std::exit(code);
    }

    // Pull one StringFileInfo field out of this module's own version resource,
    // following the translation block the resource advertises, exactly as the
    // original reads its banner strings.
    bool GetVersionField(const void* block, const char* field, char* out, size_t outSize)
    {
        struct LangAndCodePage
        {
            WORD language;
            WORD codePage;
        };

        LangAndCodePage* translation = nullptr;
        unsigned int translationLen = 0;
        if (!VerQueryValueA(block, "\\VarFileInfo\\Translation",
                            reinterpret_cast<void**>(&translation), &translationLen) ||
            translationLen < sizeof(LangAndCodePage))
        {
            return false;
        }

        char subBlock[128] = {};
        std::snprintf(subBlock, sizeof(subBlock), "\\StringFileInfo\\%04x%04x\\%s",
                      translation[0].language, translation[0].codePage, field);

        char* value = nullptr;
        unsigned int valueLen = 0;
        if (!VerQueryValueA(block, subBlock, reinterpret_cast<void**>(&value), &valueLen) ||
            value == nullptr)
        {
            return false;
        }

        std::snprintf(out, outSize, "%s", value);
        return true;
    }

    // Print the startup banner from the module's own version resource, in the
    // original's "\n%s v%s - %s\n%s\n%s\n\n" shape.
    void PrintBanner()
    {
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0)
        {
            return;
        }

        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeA(path, &handle);
        if (size == 0)
        {
            return;
        }

        void* block = std::malloc(size);
        if (block == nullptr)
        {
            return;
        }

        if (GetFileVersionInfoA(path, 0, size, block))
        {
            char internalName[128] = {};
            char fileVersion[128] = {};
            char description[128] = {};
            char copyright[128] = {};
            char company[128] = {};

            GetVersionField(block, "InternalName", internalName, sizeof(internalName));
            GetVersionField(block, "FileVersion", fileVersion, sizeof(fileVersion));
            GetVersionField(block, "FileDescription", description, sizeof(description));
            GetVersionField(block, "LegalCopyright", copyright, sizeof(copyright));
            GetVersionField(block, "CompanyName", company, sizeof(company));

            std::printf("\n%s v%s - %s\n%s\n%s\n\n", internalName, fileVersion, description,
                        copyright, company);
        }

        std::free(block);
    }

    // Remove the first case-insensitive match of "-name" or "/name" from argv,
    // shifting the tail down and lowering argc, matching the original's switch
    // stripping. Returns true when the switch was present.
    [[nodiscard]] bool StripSwitch(int& argc, char** argv, const char* name)
    {
        char dash[64] = {};
        char slash[64] = {};
        std::snprintf(dash, sizeof(dash), "-%s", name);
        std::snprintf(slash, sizeof(slash), "/%s", name);

        for (int i = 1; i < argc; ++i)
        {
            if (_stricmp(argv[i], dash) == 0 || _stricmp(argv[i], slash) == 0)
            {
                for (int j = i; j < argc - 1; ++j)
                {
                    argv[j] = argv[j + 1];
                }
                --argc;
                return true;
            }
        }
        return false;
    }

    // Honour the EULA gate the original enforces. The acceptance flag lives at
    // HKCU\Software\Sysinternals\NTFSInfo\EulaAccepted. -accepteula writes it,
    // and an existing nonzero value satisfies the check. When neither holds,
    // this console build asks for -accepteula rather than raising the original's
    // GUI license dialog.
    [[nodiscard]] bool HandleEula(bool acceptOnCommandLine)
    {
        constexpr const char* kSubKey = "Software\\Sysinternals\\NTFSInfo";
        constexpr const char* kValue = "EulaAccepted";

        DWORD accepted = 0;

        if (!acceptOnCommandLine)
        {
            RegKey read;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, kSubKey, 0, KEY_READ, read.address()) == ERROR_SUCCESS)
            {
                DWORD data = 0;
                DWORD size = sizeof(data);
                DWORD type = 0;
                if (RegQueryValueExA(read.get(), kValue, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(&data), &size) == ERROR_SUCCESS &&
                    type == REG_DWORD)
                {
                    accepted = data;
                }
            }

            if (accepted == 0)
            {
                std::printf("You need to accept the Sysinternals license. "
                            "Run with the /accepteula command-line switch.\n");
                return false;
            }
        }
        else
        {
            accepted = 1;
        }

        RegKey write;
        if (RegCreateKeyA(HKEY_CURRENT_USER, kSubKey, write.address()) == ERROR_SUCCESS)
        {
            const DWORD value = accepted;
            RegSetValueExA(write.get(), kValue, 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&value), sizeof(value));
        }
        return accepted != 0;
    }

    [[nodiscard]] bool InitNtdll()
    {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll == nullptr)
        {
            return false;
        }
        g_NtFsControlFile =
            reinterpret_cast<NtFsControlFileFn>(GetProcAddress(ntdll, "NtFsControlFile"));
        return g_NtFsControlFile != nullptr;
    }

    // Format a system error the way the original does for a failed volume open:
    // FormatMessageA with the allocating flag, then "%s\n".
    void PrintSystemError(DWORD code)
    {
        char* buffer = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, code,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0,
                       nullptr);
        std::printf("%s\n", buffer);
        LocalFree(buffer);
    }

    // Map the NTSTATUS the control call returns to the original's named strings.
    void PrintNtStatus(LONG status)
    {
        switch (static_cast<DWORD>(status))
        {
        case 0xC000000D:
            std::printf("STATUS_INVALID_PARAMETER\n\n");
            return;
        case 0xC0000010:
            std::printf("STATUS_INVALID_DEVICE_REQUEST\n\n");
            return;
        case 0xC000001F:
            std::printf("STATUS_ALREADY_COMMITTED\n\n");
            return;
        case 0xC0000023:
            std::printf("STATUS_BUFFER_TOO_SMALL\n\n");
            return;
        default:
            break;
        }
        if (status != 0)
        {
            std::printf("0x%08x\n\n", static_cast<unsigned int>(status));
            return;
        }
        std::printf("STATUS_SUCCESS\n\n");
    }

    // Open \\.\<letter>: and run FSCTL_GET_NTFS_VOLUME_DATA into data. driveIndex
    // is zero-based with 0 meaning A. Returns false after printing the error.
    [[nodiscard]] bool QueryVolume(int driveIndex, NTFS_VOLUME_DATA_BUFFER& data)
    {
        char device[] = "\\\\.\\A:";
        device[4] = static_cast<char>(driveIndex + 'A');

        FileHandle volume(CreateFileA(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, 0, nullptr));
        if (!volume.valid())
        {
            std::printf("\nError opening volume: ");
            PrintSystemError(GetLastError());
            return false;
        }

        IO_STATUS_BLOCK_LOCAL iosb = {};
        const LONG status = g_NtFsControlFile(volume.get(), nullptr, nullptr, nullptr, &iosb,
                                              kFsctlGetNtfsVolumeData, nullptr, 0, &data,
                                              static_cast<ULONG>(sizeof(data)));
        if (status != 0)
        {
            std::printf("\nError obtaining NTFS information: ");
            PrintNtStatus(status);
            return false;
        }
        return true;
    }

    // Enumerate metadata files with FindFirstFile, as the original does. On a
    // live system volume these paths do not resolve, so the section stays empty,
    // which matches the original's behaviour exactly.
    void PrintMetaDataFiles(int driveIndex)
    {
        const char letter = static_cast<char>(driveIndex + 'A');
        for (const char* name : kMetaDataFiles)
        {
            char path[1024] = {};
            std::snprintf(path, sizeof(path), "%c:\\%s", letter, name);

            WIN32_FIND_DATAA findData = {};
            const HANDLE find = FindFirstFileA(path, &findData);
            if (find != INVALID_HANDLE_VALUE)
            {
                const long long fileSize =
                    static_cast<long long>((static_cast<unsigned long long>(findData.nFileSizeHigh) << 32) |
                                           findData.nFileSizeLow);
                std::printf("%-11s\t%I64d bytes\n", findData.cFileName, fileSize);
                FindClose(find);
            }
        }
    }

    void PrintReport(const NTFS_VOLUME_DATA_BUFFER& d, int driveIndex)
    {
        const long long numberSectors = d.NumberSectors.QuadPart;
        const long long totalClusters = d.TotalClusters.QuadPart;
        const long long freeClusters = d.FreeClusters.QuadPart;
        const long long bytesPerSector = d.BytesPerSector;
        const long long bytesPerCluster = d.BytesPerCluster;
        const long long mftValidData = d.MftValidDataLength.QuadPart;
        const long long mftStart = d.MftStartLcn.QuadPart;
        const long long mftMirror = d.Mft2StartLcn.QuadPart;
        const long long zoneStart = d.MftZoneStart.QuadPart;
        const long long zoneEnd = d.MftZoneEnd.QuadPart;

        // The original prints ClustersPerFileRecordSegment for BOTH the
        // "Bytes per MFT record" and "Clusters per MFT record" lines. This is
        // reproduced verbatim so the output diffs clean. BytesPerFileRecordSegment
        // is never shown.
        const int mftRecordField = static_cast<int>(d.ClustersPerFileRecordSegment);

        std::printf("\nVolume Size\n");
        std::printf("-----------\n");
        std::printf("Volume size            : %I64d MB\n", numberSectors * bytesPerSector / kBytesPerMegabyte);
        std::printf("Total sectors          : %I64d\n", numberSectors);
        std::printf("Total clusters         : %I64d\n", totalClusters);
        std::printf("Free clusters          : %I64d\n", freeClusters);
        std::printf("Free space             : %I64d MB (%I64d%% of drive)\n",
                    freeClusters * bytesPerCluster / kBytesPerMegabyte, 100 * freeClusters / totalClusters);

        std::printf("\nAllocation Size\n");
        std::printf("----------------\n");
        std::printf("Bytes per sector       : %d\n", static_cast<int>(bytesPerSector));
        std::printf("Bytes per cluster      : %d\n", static_cast<int>(bytesPerCluster));
        std::printf("Bytes per MFT record   : %d\n", mftRecordField);
        std::printf("Clusters per MFT record: %d\n", mftRecordField);

        std::printf("\nMFT Information\n");
        std::printf("---------------\n");
        std::printf("MFT size               : %I64d MB (%I64d%% of drive)\n",
                    mftValidData / kBytesPerMegabyte, 100 * mftValidData / bytesPerCluster / totalClusters);
        std::printf("MFT start cluster      : %I64d\n", mftStart);
        std::printf("MFT zone clusters      : %I64d - %I64d\n", zoneStart, zoneEnd);
        std::printf("MFT zone size          : %I64d MB (%I64d%% of drive)\n",
                    (zoneEnd - zoneStart) * bytesPerCluster / kBytesPerMegabyte,
                    100 * (zoneEnd - zoneStart) / totalClusters);
        std::printf("MFT mirror start       : %I64d\n", mftMirror);

        std::printf("\nMeta-Data files\n");
        std::printf("---------------\n");
        PrintMetaDataFiles(driveIndex);
    }
}

int main(int argc, char** argv)
{
    const char* program = argv[0];

    const bool noBanner = StripSwitch(argc, argv, "nobanner");
    if (!noBanner)
    {
        PrintBanner();
    }

    const bool acceptEula = StripSwitch(argc, argv, "accepteula");
    if (!HandleEula(acceptEula))
    {
        return 1;
    }

    if (argc != 2)
    {
        PrintUsageAndExit(program, 1);
    }

    if (!InitNtdll())
    {
        std::printf("Not running on supported version of Windows NT.\n");
        std::exit(1);
    }

    const char* arg = argv[1];
    const char first = arg[0];

    if (first == '-' || first == '/')
    {
        if (_stricmp(arg + 1, "help") == 0 || _stricmp(arg + 1, "h") == 0 ||
            _stricmp(arg + 1, "?") == 0)
        {
            PrintUsageAndExit(program, 0);
        }
        PrintUsageAndExit(program, 1);
    }

    int driveIndex = 0;
    if (first >= 'a' && first <= 'z')
    {
        driveIndex = first - 'a';
    }
    else if (first >= 'A' && first <= 'Z')
    {
        driveIndex = first - 'A';
    }
    else
    {
        std::printf("illegal drive: %c\n", first);
        std::exit(1);
    }

    NTFS_VOLUME_DATA_BUFFER data = {};
    if (!QueryVolume(driveIndex, data))
    {
        return 1;
    }

    PrintReport(data, driveIndex);
    return 0;
}
