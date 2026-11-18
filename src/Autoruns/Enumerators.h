// Enumerators.h -- the autostart providers this recreation reads.
//
// EnumerateAll walks the in-scope providers (Logon, Services, Drivers,
// Scheduled Tasks and Winlogon) and returns their locations in the order the
// original lays the tabs out. Every call is strictly read-only. The returned
// entries carry no signature yet, the caller fills that on a background thread.
#pragma once

#include "AutorunEntry.h"
#include <vector>

namespace ar
{
    [[nodiscard]] std::vector<Location> EnumerateAll();
}
