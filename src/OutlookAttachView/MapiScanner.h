// MapiScanner.h -- the Extended MAPI logon and the folder, message and
// attachment walk that fills the attachment list.
//
// The original loads mapi32.dll dynamically, resolves MAPIInitialize,
// MAPIUninitialize and MAPILogonEx, logs on to the default profile and walks
// every store, folder, message and attachment. This reproduces that path with
// clean C++ over the Extended MAPI interfaces in MapiMin.h. With MAPI absent or
// no profile present, Scan reports the reason and the window still opens.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "AttachRecord.h"

enum class ScanStatus
{
    Ok,            // logon succeeded and the mailbox was scanned
    NoMapi,        // mapi32.dll or its entry points could not be loaded
    NoProfile,     // MAPI is present but no profile or Outlook session was found
    Failed,        // logon or a scan step failed for another reason
};

struct ScanResult
{
    ScanStatus status = ScanStatus::NoProfile;
    HRESULT    hr = 0;
    std::vector<AttachRecord> records;
    std::wstring message;        // human-readable status for the UI
};

// Options that steer the scan, mirroring the Mailbox Scan Options window and
// the relevant config values. Only the parts that affect which rows appear are
// honoured here. A cancel callback lets the UI stop a long scan.
struct ScanParams
{
    DisplayOptions display;
    bool showInlineAttachments = true;   // ShowInlineAttachment
    std::function<bool()> cancelled;     // returns true to stop
    std::function<void(const std::wstring&)> onFolder;  // status, current folder
};

// Returns true if the mapi32.dll entry points are available on this system.
bool MapiAvailable();

// Log on to the default profile and scan the mailbox. Never shows a logon
// dialog, so with no profile it returns NoProfile rather than prompting.
ScanResult ScanMailbox(const ScanParams& params);

struct AttachOpResult
{
    ScanStatus status = ScanStatus::NoProfile;
    int saved = 0;        // attachments saved or deleted
    int failed = 0;
    std::wstring message;
};

// Save the selected attachments into a folder, reopening each message and
// attachment by its stored entry id. Logs on once for the whole batch. With no
// session this reports NoProfile and touches nothing. Code path for the
// "Copy Selected Files To..." option.
AttachOpResult SaveAttachments(const std::vector<AttachRecord>& recs, const std::wstring& folder);

// Delete the selected attachments from Outlook. Code path for the
// "Delete Selected Attachments" option. Only the attachment is removed, not the
// message, matching the original.
AttachOpResult DeleteAttachments(const std::vector<AttachRecord>& recs);
