// Capture.h -- the capture backends. SniffPass supports four capture methods: Raw
// Sockets (WS2_32, no driver), WinPcap (wpcap.dll), the old Network Monitor driver
// (npptools.dll) and Network Monitor 3.x (NmApi.dll). The three driver backends are all
// loaded dynamically with LoadLibrary and GetProcAddress, so the program builds and runs
// with no SDK and no driver installed, exactly as the original does.
//
// Security: this recreation performs NO live capture. It probes a backend for
// availability and enumerates adapter names, nothing more. No socket is ever put into
// receive mode and no packet is ever read, so no real credential can be seen. The only
// credential parsing that runs is the synthetic self test in CredParser.
#pragma once
#include <string>
#include <vector>

enum CaptureMethod { CAP_RAW = 0, CAP_WINPCAP = 1, CAP_NETMON = 2, CAP_NETMON3 = 3 };

struct Adapter
{
    std::wstring name;         // adapter id or device name
    std::wstring description;  // friendly description
};

// Enumerate network adapters for the Capture Options dialog, via GetAdaptersInfo loaded
// dynamically from iphlpapi.dll. Returns an empty list when the API is unavailable.
std::vector<Adapter> EnumerateAdapters();

// Probe whether the selected capture backend is usable on this machine. Raw Sockets is
// always reported usable (WS2_32 is always present). The driver methods load their DLL
// and resolve every entry point, matching the original's probe, and report false when
// the driver is absent.
bool IsBackendAvailable(int method);

// A short human name for a method, for status and message text.
const wchar_t* CaptureMethodName(int method);
