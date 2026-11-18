// WmiQuery.h -- runs a WQL query and returns a dynamic-column result.
//
// Mirrors the original's query core (sub_140017508 / sub_140017218 in the
// binary): CoCreateInstance(CLSID_WbemLocator) -> IWbemLocator::ConnectServer
// (default namespace root\CIMV2) -> IWbemServices::ExecQuery(L"WQL", query,
// WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY) ->
// IEnumWbemClassObject::Next, reading each object's non-system properties with
// BeginEnumeration / Next / EndEnumeration.
#pragma once
#include "WmiResult.h"
#include <string>

struct WmiQueryParams
{
    std::wstring nameSpace = L"root\\CIMV2";   // Namespace (default)
    std::wstring classOrQuery = L"Win32_Process"; // Class, or a full WQL query
    int          connectComputer = 1;          // 1 this computer, 2 remote
    std::wstring computerName;                  // for connectComputer == 2
    std::wstring userName;                      // optional remote credentials
    std::wstring password;
};

// Executes the query. COM must already be initialised on the calling thread.
WmiResult RunWmiQuery(const WmiQueryParams& p);
