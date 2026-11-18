// PcapLoader.h -- the WinPcap / Npcap capture driver loader, reproducing the original's
// dynamic load path (sub_1400219FC). The original never links wpcap. It loads it at run
// time and resolves the pcap entry points with GetProcAddress, so the program builds and
// launches with no SDK and no capture driver present. This recreation does the same, and
// with no driver the loader reports unavailable so the window still opens and degrades.
//
// SECURITY: this loader only resolves the entry points and reports whether the driver is
// usable. It does NOT open an adapter or pump packets. Live capture is intentionally not
// wired up. The working data path is loading packets from an .ssp file.
#pragma once
#include <windows.h>
#include <string>

// Minimal pcap declarations, inline, so no Npcap SDK is needed to build. The opaque
// handle and header types are only enough to hold the resolved entry points.
struct pcap;
typedef struct pcap pcap_t;
struct pcap_if;
typedef struct pcap_if pcap_if_t;

struct pcap_pkthdr_min
{
    // timeval + captured length + original length, layout as WinPcap defines it
    long tv_sec;
    long tv_usec;
    unsigned int caplen;
    unsigned int len;
};

// The entry points the original resolves, in the same order. Signatures follow the
// public pcap API. Several of these are never called here, they are resolved only to
// match the original's availability check.
struct PcapApi
{
    HMODULE module = nullptr;

    int(__cdecl* findalldevs_ex)(const char*, void*, pcap_if_t**, char*) = nullptr;
    void(__cdecl* freealldevs)(pcap_if_t*) = nullptr;
    pcap_t*(__cdecl* open)(const char*, int, int, int, void*, char*) = nullptr;
    void(__cdecl* close)(pcap_t*) = nullptr;
    int(__cdecl* next_ex)(pcap_t*, pcap_pkthdr_min**, const unsigned char**) = nullptr;
    int(__cdecl* loop)(pcap_t*, int, void*, unsigned char*) = nullptr;
    int(__cdecl* compile)(pcap_t*, void*, const char*, int, unsigned int) = nullptr;
    int(__cdecl* setfilter)(pcap_t*, void*) = nullptr;
    void(__cdecl* freecode)(void*) = nullptr;
    const unsigned char*(__cdecl* next)(pcap_t*, pcap_pkthdr_min*) = nullptr;
    int(__cdecl* findalldevs)(pcap_if_t**, char*) = nullptr;
    pcap_t*(__cdecl* open_live)(const char*, int, int, int, char*) = nullptr;
    pcap_t*(__cdecl* open_offline)(const char*, char*) = nullptr;
    int(__cdecl* dispatch)(pcap_t*, int, void*, unsigned char*) = nullptr;

    bool available = false;

    ~PcapApi()
    {
        if (module)
        {
            ::FreeLibrary(module);
        }
    }

    // Load wpcap the way the original does, then resolve every entry point and verify the
    // required set is present. Returns true only when the driver is usable.
    bool Load()
    {
        if (module)
        {
            ::FreeLibrary(module);
            module = nullptr;
        }
        available = false;

        char sysdir[MAX_PATH]{};
        ::GetSystemDirectoryA(sysdir, MAX_PATH);

        std::string npcapDll = std::string(sysdir) + "\\Npcap\\wpcap.dll";
        if (::GetFileAttributesA(npcapDll.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            module = ::LoadLibraryA("wpcap.dll");
        }
        else
        {
            std::string npcapDir = std::string(sysdir) + "\\Npcap";
            char saved[MAX_PATH]{};
            ::GetCurrentDirectoryA(MAX_PATH, saved);
            ::SetCurrentDirectoryA(npcapDir.c_str());
            module = ::LoadLibraryA(npcapDll.c_str());
            if (!module)
            {
                module = ::LoadLibraryA("wpcap.dll");
            }
            ::SetCurrentDirectoryA(saved);
        }
        if (!module)
        {
            return false;
        }

        auto proc = [&](const char* name)
        {
            return ::GetProcAddress(module, name);
        };
        findalldevs_ex = reinterpret_cast<decltype(findalldevs_ex)>(proc("pcap_findalldevs_ex"));
        freealldevs = reinterpret_cast<decltype(freealldevs)>(proc("pcap_freealldevs"));
        open = reinterpret_cast<decltype(open)>(proc("pcap_open"));
        close = reinterpret_cast<decltype(close)>(proc("pcap_close"));
        next_ex = reinterpret_cast<decltype(next_ex)>(proc("pcap_next_ex"));
        loop = reinterpret_cast<decltype(loop)>(proc("pcap_loop"));
        compile = reinterpret_cast<decltype(compile)>(proc("pcap_compile"));
        setfilter = reinterpret_cast<decltype(setfilter)>(proc("pcap_setfilter"));
        freecode = reinterpret_cast<decltype(freecode)>(proc("pcap_freecode"));
        next = reinterpret_cast<decltype(next)>(proc("pcap_next"));
        findalldevs = reinterpret_cast<decltype(findalldevs)>(proc("pcap_findalldevs"));
        open_live = reinterpret_cast<decltype(open_live)>(proc("pcap_open_live"));
        open_offline = reinterpret_cast<decltype(open_offline)>(proc("pcap_open_offline"));
        dispatch = reinterpret_cast<decltype(dispatch)>(proc("pcap_dispatch"));

        // The original requires this set before it treats the driver as usable.
        bool ok = findalldevs && freealldevs && close && loop && compile && setfilter && freecode && next &&
                  open_live && dispatch && open_offline;
        if (!ok)
        {
            ::FreeLibrary(module);
            module = nullptr;
            return false;
        }
        available = true;
        return true;
    }
};
