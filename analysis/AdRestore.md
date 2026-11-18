# AdRestore (Sysinternals adrestore64.exe v1.2)

Source-accurate C++20 x64 console reconstruction. AdRestore enumerates
tombstoned (deleted) Active Directory objects and can restore a chosen one. It
is a standalone Sysinternals command-line tool, not a PsTools binary and not a
WTL GUI, so it does not carry the PsTools `\\computer -u -p` front end.

- Original: `targets/sysinternals/adrestore64.exe` (x64, MSVC, static CRT, console, v1.2)
  - MD5 `cd6e3b21991207942c0003f0c5462729`, PDB `D:\a\1\s\x64\Release\adrestore64.pdb`
- Rebuild: `src/AdRestore/AdRestore.cpp`, project `src/AdRestore/AdRestore.vcxproj`
  (console, v143, stdcpp20, Unicode, static CRT, SubSystem Console)
- Engine: IDA idalib MCP primary. The logic decompiled cleanly, so rea was not needed.

## What it does

```
Usage: AdRestore [-r] [searchfilter]

   -r       Prompt to restore deleted objects found.
```

With no argument it lists every tombstone. With a search filter it lists the
tombstones whose name contains that string. With `-r` it prompts after each
object to restore it. `-accepteula` accepts the Sysinternals license on the
command line. The banner always prints.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `main` 0x140002B30 | CoInitialize, banner, EULA gate, arg parse, two-pass search, final count |
| `sub_140002660` | Bind: open rootDSE, read defaultNamingContext, open the WKGUID container |
| `sub_1400022C0` | Run the search, print each row, prompt and drive the restore |
| `sub_1400027B0` | Restore: build the new DN, ldap_init/connect/bind, ldap_modify_ext_sW |
| `sub_140002A80` | Usage |
| `sub_140001910` / `sub_140001E90` | Strip `-accepteula` / `/accepteula` from argv, then gate |
| `sub_140001AE0` | EULA registry check and write, console prompt or RichEdit dialog |
| `sub_1400013A0` | EULA accepted check across the three registry keys |
| `sub_140002110` | Wide printf to stdout |
| `sub_1400020B0` | Narrow printf to stdout (usage) |
| `sub_140002AC0` | swprintf wrapper (filter and WKGUID path formatting) |

### Startup and argument parsing

`main` calls `CoInitialize(nullptr)`, prints the three banner lines
unconditionally, then calls the EULA helper with the app name `AdRestore` and
the real `argc`/`argv`. The helper strips any `-accepteula` or `/accepteula`
entry in place and decrements `argc` before the main loop runs, so the switch
never reaches the parser. A declined EULA returns 1 from `main`.

The parser is deliberately loose. A token whose first character is `-` or `/`
is a switch, and the only accepted switch is one whose second character upper
cases to `R`, so `-r`, `/r`, `-R`, and even `-restore` all set the restore
flag. Any other switch prints usage and exits 1. A token that is not a switch
is the search filter, and a second non-switch token prints usage and exits 1.

### Two-pass search

With a filter string the tool formats `(cn=*%s*)` then `(ou=*%s*)` and searches
with each. With no filter it uses `(cn=*)` then `(ou=*)`. Each pass adds its
match count to a running total, and the run ends with
`Found %d item%s matching search criteria.` where the plural `s` is dropped
when the count is exactly 1.

### Binding the Deleted Objects container (ADSI)

`sub_140002660` reaches the container through ADSI rather than a raw LDAP search:

1. `ADsOpenObject(L"LDAP://rootDSE", nullptr, nullptr, ADS_SECURE_AUTHENTICATION, IID_IADs, ...)`.
2. `IADs::Get(L"defaultNamingContext", ...)` to read the directory naming context as a BSTR.
3. Format `LDAP://<WKGUID=18e2ea80684f11d2b9aa00c04f79f805,<naming context>>`, where the
   GUID is the well-known identifier of the Deleted Objects container.
4. `ADsOpenObject(path, ..., ADS_SECURE_AUTHENTICATION | ADS_FAST_BIND, IID_IADsContainer, ...)`.
5. `QueryInterface(IID_IDirectorySearch)` on the container.

The three IIDs were read from the binary and decode to `IID_IADs`
`{FD8256D0-FD15-11CE-ABC4-02608C9E7553}`, `IID_IADsContainer`
`{001677D0-FD16-11CE-ABC4-02608C9E7553}`, and `IID_IDirectorySearch`
`{109BA8EC-92F0-11D0-A790-00C04FD8D5A8}`.

### Search preferences and row walk

`IDirectorySearch::SetSearchPreference` receives three entries, confirmed from
the raw stack fill:

| Preference | Type | Value |
|------------|------|-------|
| `ADS_SEARCHPREF_SEARCH_SCOPE` (5) | INTEGER (7) | `ADS_SCOPE_ONELEVEL` (1) |
| `ADS_SEARCHPREF_TOMBSTONE` (13) | BOOLEAN (6) | TRUE (1) |
| `ADS_SEARCHPREF_PAGESIZE` (7) | INTEGER (7) | 1000 |

The TOMBSTONE preference is what makes deleted objects visible. It maps to the
LDAP show-deleted control under the covers, which is the ADSI equivalent of the
raw `LDAP_SERVER_SHOW_DELETED_OID` the restore path sets directly.

`ExecuteSearch` requests the attributes `cn`, `distinguishedName`, and
`lastKnownParent`. For each row and each attribute, `GetColumn` returns the
value. The tool prints an attribute only when its ADS type is a string kind.
The decompile uses a bit test against the mask `0x13A` with the type as the bit
index, which selects ADS types 1, 3, 4, 5, and 8 (DN, case-ignore, printable,
numeric, and octet strings). Each column prints as `name: value,value` with a
trailing newline, and every row ends with a blank line.

### Restore path (raw WLDAP32)

When `-r` is set, after each object the tool asks
`Do you want to restore this object (y/n)? ` and reads a line. A leading `Y` or
`y` triggers `sub_1400027B0` with the object's live (deleted) distinguishedName
and its lastKnownParent. The restore uses raw WLDAP32:

1. Copy the deleted DN. Find the `\0A` escape that begins the tombstone RDN
   suffix (`CN=name\0ADEL:<guid>`), cut there to recover `CN=name`, then append
   `,` and the lastKnownParent to build the restored DN. When the marker is
   absent and a parent was supplied, the function returns `E_INVALIDARG`.
2. `ldap_initW(nullptr, 389)`, `ldap_set_optionW(LDAP_OPT_PROTOCOL_VERSION, 3)`,
   `ldap_connect`, `ldap_bind_sW(nullptr, nullptr, LDAP_AUTH_NEGOTIATE)`.
3. `ldap_modify_ext_sW` with two modifications and one server control:
   - delete the `isDeleted` attribute,
   - replace `distinguishedName` with the restored DN (only when a parent is known),
   - server control `1.2.840.113556.1.4.417` (`LDAP_SERVER_SHOW_DELETED_OID`), critical.
4. On error, `ldap_err2stringW` feeds `OutputDebugStringW`. `ldap_unbind` always runs.

The object is identified to `ldap_modify_ext_sW` by its current deleted DN, and
the restored DN is carried as the replacement value, which moves the object back
under its former parent.

One original quirk is reproduced. The restore returns the raw LDAP result, and
the caller treats any non-negative value as success. LDAP error codes are small
positive numbers, so a failed `ldap_modify_ext_sW` still reports
`Restore succeeded`. Only the startup HRESULT failures, which are negative,
report `Restore failed: %d`. The rebuild keeps this behavior for fidelity.

### EULA

The EULA counts as accepted when a nonzero `EulaAccepted` DWORD is present under
`HKLM\Software\Sysinternals`, `HKCU\Software\Sysinternals`, or
`HKCU\Software\Sysinternals\AdRestore`. `-accepteula`, or an accepted prompt,
writes the per-app value. The rebuild shares the exact key the original uses.

## Validation

This machine is not domain-joined, so live tombstone enumeration and restore
against a domain controller were not possible. The deterministic surface was
validated by running the original and the rebuild (x64 Release) with identical
arguments and comparing stdout, stderr, and exit code. The enumeration and
restore logic were validated by code inspection against the decompile and by the
no-domain path. No restore was ever attempted, and no Active Directory output
was fabricated.

| Arguments | stdout | stderr | exit |
|-----------|--------|--------|------|
| `/?` | identical | identical | 1 / 1 |
| `-accepteula` (list all, no domain) | identical | identical | 0 / 0 |
| `-accepteula comp` (filter) | identical | identical | 0 / 0 |
| `-accepteula -r comp` (restore flag) | identical | identical | 0 / 0 |
| `-accepteula a b` (two filters) | identical | identical | 1 / 1 |
| `-accepteula -x` (bad switch) | identical | identical | 1 / 1 |
| `/accepteula /r` | identical | identical | 0 / 0 |
| `-accepteula -R MyServer` | identical | identical | 0 / 0 |
| `-accepteula one two three` | identical | identical | 1 / 1 |

The no-domain run reproduces exactly:

```

AdRestore v1.2
by Mark Russinovich
Sysinternals - www.sysinternals.com


Enumerating domain deleted objects:

Found 0 items matching search criteria.

```

On a host without a reachable domain the `ADsOpenObject` failure is swallowed,
the match count stays zero, and the run exits 0 with no error line. The rebuild
matches this. The EULA write was checked both ways: `-accepteula` records
`HKCU\Software\Sysinternals\AdRestore\EulaAccepted` as DWORD 1 and the next run
proceeds with no prompt. With the flag cleared, the rebuild prompts, a declined
prompt exits 1 with no enumeration, and an accepted prompt proceeds and records
the flag.

What could not be validated here, and needs a domain controller:

- the enumeration of real tombstones and the printed `cn` / `distinguishedName`
  / `lastKnownParent` lines,
- the live restore through `ldap_modify_ext_sW`.

Both were reconstructed faithfully from the decompile and are covered by the
error path and code inspection only.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The
only diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3`.

```
MSBuild src\AdRestore\AdRestore.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\AdRestore\AdRestore.vcxproj /p:Configuration=Release /p:Platform=x64
```

Libraries: `wldap32`, `activeds`, `adsiid`, `ole32`, `oleaut32`, `advapi32`.
`ConformanceMode` is off for this project because `activeds.h` and `iads.h` do
not parse under `/permissive-`, and `WIN32_LEAN_AND_MEAN` is not defined because
ADSI needs the full OLE headers that the lean macro strips.

## Deviations

1. **EULA interactive fallback.** The rebuild uses the console Y/N prompt that
   the original falls back to on headless hosts. The GUI RichEdit license dialog
   the original shows on an interactive desktop is not reproduced, which a
   console tool does not need.
2. **No `-nobanner`.** AdRestore v1.2 has no `-nobanner` switch. The banner is
   printed unconditionally in `main`, with no suppression path anywhere in the
   binary. The rebuild matches this and does not add one.
3. **Buffer management.** The restore DN is built with `std::wstring` rather than
   the original's `operator new` buffer sized at `len + 53`. The resulting DN is
   identical, so the unreachable out-of-memory return of the original is not
   reproduced.
4. **distinguishedName guard on restore.** The rebuild checks the `GetColumn`
   result for `distinguishedName` before dereferencing its value, where the
   original dereferences unconditionally. The attribute is always present on the
   rows walked, so observable behavior is unchanged, and the guard only avoids a
   null dereference on a malformed row.

## Note on scope

Not added to any `.sln` by request. Not committed. No real Active Directory data
was read, printed, or fabricated, and no restore was attempted.
