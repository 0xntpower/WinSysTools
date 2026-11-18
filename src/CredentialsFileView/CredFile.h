// CredFile.h -- Windows Credentials file parsing and DPAPI decode.
//
// A Credentials file on Vista and later is a small CredentialFile header
// followed by one DPAPI blob. CryptUnprotectData turns that blob into a
// CREDENTIAL structure whose fields become the upper-pane columns. On Windows
// XP a single file holds several entries, each prefixed by its own length.
//
// The running-system decode path (CryptUnprotectData) is reproduced here. The
// offline master-key path used by the "Decrypt Credentials files of any system"
// option, which derives keys from the SYSTEM and SECURITY hives of an external
// drive, is out of scope and is noted in analysis/CredentialsFileView.md.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "CredRecord.h"

// One decryption request, built from the Decryption Options dialog or the
// command line. source mirrors the original's DataSource value (1..4).
struct DecodeRequest
{
    int source = 2;                        // 1 current user, 2 any system, 3 another user, 4 all users
    std::vector<std::wstring> credFolders; // Windows Credentials folders to scan
    std::wstring password;                 // Windows login password (external path)
    DisplayOptions display;
};

// Enumerate and decode every Credentials file under the request's folders,
// appending one CredRecord per decrypted entry. Returns the number added.
int DecodeFolders(const DecodeRequest& req, std::vector<CredRecord>& out);

// Decode a single Credentials file. Appends one record per entry it holds.
int DecodeFile(const std::wstring& path, const DecodeRequest& req, std::vector<CredRecord>& out);

// Parse one already-decrypted Vista CREDENTIAL structure into a record's
// decoded columns (Entry Type, Persist, Modified Time, Entry Name, User Name,
// Password, Decrypted Size). Returns false if the buffer is too small. Exposed
// for the self-check.
bool ParseVistaEntry(const BYTE* plain, size_t size, const DisplayOptions& disp, CredRecord& rec);

// Locate the DPAPI blob inside raw file bytes by scanning the first 16 bytes
// for the DPAPI provider GUID, the way the original does. Returns the offset,
// or -1 if no blob is found.
int LocateDpapiBlob(const BYTE* data, size_t size);

// Self-check: parse a synthetic decrypted entry with known, non-secret values
// and confirm every decoded column. detail receives a short pass/fail note.
bool CredSelfTest(std::wstring& detail);
