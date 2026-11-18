// TraceProtocol.h -- the record format and IPC constants shared by the
// AdInsight GUI and its injected hook DLL.
//
// The original drops an embedded hook DLL (ADInsightDll.dll, resource type
// BINRES id 1170), loads it, and the DLL reports every intercepted wldap32
// LDAP client call back to the GUI over the named pipe \\.\pipe\ADInsightPipe.
// This header captures the exact names and the per-call data model that both
// sides agree on. The record here is a fixed-size POD so a single message-mode
// pipe write carries one captured call with no framing logic.
#pragma once

#include <cstdint>

namespace adin
{
    // Named pipe the GUI hosts and the injected DLL connects to, matching the
    // original binary's string.
    inline constexpr const wchar_t* kPipeName = L"\\\\.\\pipe\\ADInsightPipe";

    // The hook DLL file name and its embedded-resource identity, matching the
    // original (FindResourceW(nullptr, MAKEINTRESOURCE(1170), L"BINRES")).
    inline constexpr const wchar_t* kHookDllName = L"ADInsightDll.dll";
    inline constexpr const wchar_t* kResTypeBinres = L"BINRES";
    inline constexpr int kHookDllResId = 1170;

    // Exported entry points the GUI resolves from the hook DLL, matching the
    // original's GetProcAddress names.
    inline constexpr const char* kExportTraceInit = "AdmonTraceInit";
    inline constexpr const char* kExportMonitorStart = "AdmonMonitorStart";
    inline constexpr const char* kExportMonitorStop = "AdmonMonitorStop";
    inline constexpr const char* kExportPingDll = "AdmonPingDLL";

    // Window message the pipe-server thread posts to the main window for each
    // captured call. The original posts WM_USER+2 (0x402) with the record as
    // lParam. The lParam here is a heap TraceRecord* the window takes ownership
    // of and frees.
    inline constexpr unsigned int kMsgRecord = 0x0402;  // WM_USER + 2
    inline constexpr unsigned int kMsgConnect = 0x040C; // client attached
    inline constexpr unsigned int kMsgError = 0x040D;   // error text in lParam

    inline constexpr uint32_t kRecordMagic = 0x4E494441; // 'ADIN'

    // Phase of a captured call. A request is logged when the LDAP function is
    // entered, a response when it returns, so the GUI can pair them and show a
    // duration the way the original does.
    enum class Phase : uint32_t
    {
        Request = 0,
        Response = 1
    };

    inline constexpr int kProcNameLen = 64;
    inline constexpr int kOperationLen = 64;
    inline constexpr int kDnLen = 320;
    inline constexpr int kDetailLen = 640;

    // One intercepted LDAP client call. Fixed size so one pipe message is one
    // record. The GUI assigns the visible sequence number on receipt.
#pragma pack(push, 8)
    struct TraceRecord
    {
        uint32_t magic;                   // kRecordMagic
        uint32_t phase;                   // adin::Phase
        uint32_t pid;                     // calling process id
        uint32_t tid;                     // calling thread id
        int32_t resultCode;               // LDAP result on a response, else 0
        uint32_t seq;                     // call sequence, paired across phases
        uint64_t fileTime;                // FILETIME of the call
        uint64_t durationUs;              // response only: elapsed microseconds
        wchar_t process[kProcNameLen];    // calling image base name
        wchar_t operation[kOperationLen]; // wldap32 function name
        wchar_t dn[kDnLen];               // distinguished name or primary target
        wchar_t detail[kDetailLen];       // formatted parameters
    };
#pragma pack(pop)
} // namespace adin
