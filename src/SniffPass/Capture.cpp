// Capture.cpp -- backend probing and adapter enumeration. The WinPcap probe mirrors
// sub_140010A10 in the original: load wpcap.dll, resolve every pcap entry point, and
// treat the backend as usable only when the whole set resolves. The Network Monitor
// backends follow the same pattern against npptools.dll and NmApi.dll. The function
// pointer types are declared inline so no capture SDK is needed to build.
//
// No entry point here is ever called. Resolving a symbol only proves the driver is
// present. Starting a real capture would mean reading live packets, which this
// recreation never does.
#include "Capture.h"
#include <windows.h>

namespace
{
    // Load a module from System32 and the standard search path, then require that every
    // name in the list resolves. Returns true only when all are present.
    bool AllProcsPresent(const wchar_t* dll, const char* const* procs, int count)
    {
        HMODULE h = ::LoadLibraryW(dll);
        if (!h) return false;
        bool ok = true;
        for (int i = 0; i < count && ok; ++i)
            if (!::GetProcAddress(h, procs[i])) ok = false;
        ::FreeLibrary(h);
        return ok;
    }

    bool WinPcapAvailable()
    {
        static const char* const procs[] = {
            "pcap_findalldevs_ex", "pcap_freealldevs", "pcap_open", "pcap_close",
            "pcap_next_ex", "pcap_loop", "pcap_compile", "pcap_setfilter",
            "pcap_freecode", "pcap_next", "pcap_findalldevs", "pcap_open_live",
            "pcap_open_offline", "pcap_dispatch",
        };
        return AllProcsPresent(L"wpcap.dll", procs, (int)(sizeof(procs) / sizeof(procs[0])));
    }

    bool NetMon3Available()
    {
        static const char* const procs[] = {
            "NmApiInitialize", "NmApiClose", "NmConfigAdapter", "NmGetAdapter",
            "NmGetAdapterCount", "NmOpenCaptureEngine", "NmStartCapture", "NmStopCapture",
            "NmGetRawFrame", "NmGetRawFrameLength", "NmCloseHandle", "NmGetFrame",
            "NmGetFrameCount", "NmOpenCaptureFile",
        };
        return AllProcsPresent(L"NmApi.dll", procs, (int)(sizeof(procs) / sizeof(procs[0])));
    }

    bool NetMonAvailable()
    {
        static const char* const procs[] = {
            "CreateNPPInterface", "DestroyBlob", "GetNetworkInfoFromBlob", "GetNPPBlobTable",
        };
        return AllProcsPresent(L"npptools.dll", procs, (int)(sizeof(procs) / sizeof(procs[0])));
    }

    // Minimal GetAdaptersInfo shapes, declared inline to avoid the iphlpapi import lib.
    #pragma pack(push, 1)
    struct IP_ADDR_STRING_MIN { void* Next; char IpAddress[16]; char IpMask[16]; DWORD Context; };
    struct IP_ADAPTER_INFO_MIN
    {
        IP_ADAPTER_INFO_MIN* Next;
        DWORD ComboIndex;
        char  AdapterName[260];
        char  Description[132];
        UINT  AddressLength;
        BYTE  Address[8];
        DWORD Index;
        UINT  Type;
        UINT  DhcpEnabled;
        void* CurrentIpAddress;
        IP_ADDR_STRING_MIN IpAddressList;
        IP_ADDR_STRING_MIN GatewayList;
        IP_ADDR_STRING_MIN DhcpServer;
        BOOL  HaveWins;
        IP_ADDR_STRING_MIN PrimaryWinsServer;
        IP_ADDR_STRING_MIN SecondaryWinsServer;
        time_t LeaseObtained;
        time_t LeaseExpires;
    };
    #pragma pack(pop)

    std::wstring fromAnsi(const char* s)
    {
        if (!s || !*s) return {};
        int n = ::MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        std::wstring w(n ? n - 1 : 0, L'\0');
        if (n > 1) ::MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
        return w;
    }
}

std::vector<Adapter> EnumerateAdapters()
{
    std::vector<Adapter> out;
    HMODULE h = ::LoadLibraryW(L"iphlpapi.dll");
    if (!h) return out;

    using GetAdaptersInfoFn = DWORD(WINAPI*)(IP_ADAPTER_INFO_MIN*, PULONG);
    auto getInfo = reinterpret_cast<GetAdaptersInfoFn>(::GetProcAddress(h, "GetAdaptersInfo"));
    if (getInfo)
    {
        ULONG size = 0;
        if (getInfo(nullptr, &size) == ERROR_BUFFER_OVERFLOW && size > 0)
        {
            std::vector<BYTE> buf(size);
            auto* info = reinterpret_cast<IP_ADAPTER_INFO_MIN*>(buf.data());
            if (getInfo(info, &size) == NO_ERROR)
            {
                for (auto* p = info; p; p = p->Next)
                {
                    Adapter a;
                    a.name = fromAnsi(p->AdapterName);
                    a.description = fromAnsi(p->Description);
                    out.push_back(std::move(a));
                }
            }
        }
    }
    ::FreeLibrary(h);
    return out;
}

bool IsBackendAvailable(int method)
{
    switch (method)
    {
    case CAP_RAW:     return true;                 // WS2_32 raw sockets, no driver needed
    case CAP_WINPCAP: return WinPcapAvailable();
    case CAP_NETMON:  return NetMonAvailable();
    case CAP_NETMON3: return NetMon3Available();
    default:          return false;
    }
}

const wchar_t* CaptureMethodName(int method)
{
    switch (method)
    {
    case CAP_RAW:     return L"Raw Sockets";
    case CAP_WINPCAP: return L"WinPcap Packet Capture Driver";
    case CAP_NETMON:  return L"Network Monitor Driver";
    case CAP_NETMON3: return L"Network Monitor Driver 3.x";
    default:          return L"Unknown";
    }
}
