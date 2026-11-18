// UpdateEnumerator.h -- reads the Windows Update history through the Windows
// Update Agent COM API, the original's default data source (LoadFrom = 1).
#pragma once
#include "UpdateRecord.h"
#include <vector>

// Queries the local machine's update history via the WUA API and returns one
// UpdateRecord per IUpdateHistoryEntry. Mirrors sub_140015348 in the binary:
// CoCreateInstance(CLSID_UpdateSearcher) -> GetTotalHistoryCount -> QueryHistory,
// then reads the same set of fields from each entry.
//
// Returns the WUA HRESULT via hrOut (S_OK on success). COM must already be
// initialised on the calling thread.
std::vector<UpdateRecord> EnumerateUpdateHistory(HRESULT* hrOut = nullptr);
