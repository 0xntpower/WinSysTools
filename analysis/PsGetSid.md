# PsGetSid (Sysinternals PsGetsid64.exe v1.46)

Source-accurate C++20 console reconstruction. Displays the SID of a computer or
user account, or translates a SID back to its account name, with the shared
PsTools command-line front end. This is the first PsTools binary reconstructed,
so the shared framework was factored into `src/common/PsTools.h` for the rest of
the suite to reuse.

- Original: `targets/sysinternals/PsGetsid64.exe` (x64, MSVC, static CRT, console)
- Rebuild: `src/PsGetSid/PsGetSid.cpp` plus `src/common/PsTools.h`, project
  `src/PsGetSid/PsGetSid.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. rea not needed, the logic decompiled cleanly.

## What it does

```
Usage: psgetsid64 [\\computer[,computer2[,...] | @file] [-u Username [-p Password]]] [account | SID]
```

With no account argument it reports the machine SID. With an account name it
reports that account's SID. With a SID string it reports the matching account.
The target can be the local system, one or more remote computers, a wildcard for
the whole domain, or a response file. `-u` and `-p` supply alternate credentials
for the remote computers. `-accepteula` and `-nobanner` are the standard
Sysinternals switches.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `main` 0x1400062E0 | OS gate, read local name, parse args, prompt password, run per computer |
| `sub_1400036D0` | Banner plus EULA driver, run before parsing |
| `sub_1400029E0` | Banner printer, strips `-nobanner` from argv |
| `sub_140002C90` | Short banner from the version resource |
| `sub_140001B90` | EULA satisfied check (stored flag or `-accepteula` present) |
| `sub_140001CC0` / `sub_140001C20` | Read the EULA flag from the registry |
| `sub_1400020E0` / `sub_140002300` | Strip `-accepteula`, prompt or dialog, write the flag |
| `sub_140005770` | Parse the shared command line |
| `sub_140003190` | Iterate the target list (`@file`, `*`, comma list, single) |
| `sub_140005EA0` | Per-computer worker |
| `sub_1400053C0` | Translate account to SID, or SID to account |
| `sub_140004B40` | Machine SID from the LSA account-domain policy |
| `sub_140004DC0` | Build a SID string (0x hex authority when the top word is set, else decimal) |
| `sub_140004900` | Parse a SID string into a binary SID |
| `sub_1400059D0` | Remote machine SID through the GETSIDSV service over a named pipe |
| `sub_1400040F0` / `sub_1400041C0` | Connect and disconnect `\\computer\IPC$` with credentials |
| `sub_140003800` | Print a Windows error code, netmsg.dll for codes 2100 to 2483 |
| `sub_140006090` | Usage |

### The shared PsTools command-line framework (`src/common/PsTools.h`)

The front end is identical across the PsTools binaries, so it was recovered as a
header-only unit. It owns:

- **Banner**. Format `\n%s v%s - %s\n%s\n%s\n\n` from five version-resource
  fields (InternalName, FileVersion, FileDescription, LegalCopyright,
  CompanyName). It goes to stdout when stdout is a pipe, otherwise to stderr, so
  a redirect to a file keeps stdout clean. `-nobanner` suppresses it.
- **EULA gate**. Accepted when the suite-wide flag is set under HKLM or HKCU
  `Software\Sysinternals`, or the per-tool flag under
  `HKCU\Software\Sysinternals\<app>\EulaAccepted` (DWORD). `-accepteula` or a
  console Y/N prompt records it, then the run proceeds. A decline prints
  `Eula declined.` and exits 1.
- **Target parsing**. A leading `-` or `/` is a switch, `@` names a response
  file, `\\` prefixes a computer, anything else is the account or SID. `-u`
  needs a computer target and `-p` needs `-u`. With no computer the target is
  the local machine. `-u` without `-p` prompts for a hidden password.
- **Target iteration**. `@file` reads one computer per line, `*` enumerates the
  domain with NetServerEnum, a comma list splits, a single name is called
  directly. A `\\computer:` header is printed before each in the multi forms.
- **Alternate credentials**. WNetAddConnection2A to `\\computer\IPC$` with the
  supplied user and password, and the matching cancel.
- **Error reporting**. FormatMessage, loading netmsg.dll for the LANMAN error
  range, written to stderr.
- **Local identity** and the **hidden password prompt**.

### SID logic

The account-to-SID path calls `LookupAccountNameA(computer, name)` with the
standard size-then-fill pattern, converts the SID to a string, and prints
`SID for domain\name:` (or `SID for name:` when the typed name already has a
backslash) followed by the SID. The SID-to-account path parses the SID,
calls `LookupAccountSidA`, and prints `Account for SID:` followed by the use
type and `domain\name`. The machine SID path opens the LSA policy and reads the
account-domain SID from `PolicyAccountDomainInformation`. The rebuild uses
`ConvertSidToStringSidA` and `ConvertStringSidToSidA` for the SID string
conversions. Their output matches the original's hand-built format byte for
byte (decimal identifier authority, or a 12-digit 0x form when the high word is
set), so the shown SIDs are identical while the code stays idiomatic.

One detail worth keeping: the local machine SID line carries a leading carriage
return (`\rSID for \\NAME:`) and is preceded by a line-clear written to stderr
(a bare `\r\r` when output is redirected). The rebuild reproduces both, so
stdout and stderr match exactly.

## Validation

Original and rebuild (x64 Release) run with the same arguments and `-nobanner`,
stdout and stderr compared byte for byte, exit codes compared. All deterministic
on the local machine. No real external account or credential was used.

| Arguments | stdout | stderr | exit |
|-----------|--------|--------|------|
| (none, machine SID) | identical | identical | 0 / 0 |
| `Administrators` | identical | identical | 0 / 0 |
| `SYSTEM` | identical | identical | 0 / 0 |
| `Users` | identical | identical | 0 / 0 |
| `Everyone` | identical | identical | 0 / 0 |
| `S-1-5-18` | identical | identical | 0 / 0 |
| `S-1-5-32-544` | identical | identical | 0 / 0 |
| `S-1-5-32-545` | identical | identical | 0 / 0 |
| `NoSuchUser_zzz123` | identical (error path) | identical | -1 / -1 |
| banner, no `-nobanner` | identical | identical | 0 / 0 |

Sample outputs reproduced exactly:

```
SID for BUILTIN\Administrators:
S-1-5-32-544

Account for S-1-5-18:
Well Known Group: NT AUTHORITY\SYSTEM

SID for \Everyone:
S-1-1-0
```

The EULA flag write was checked against the original. Both the original and the
rebuild create `HKCU\Software\Sysinternals\PsGetSid\EulaAccepted` as DWORD 1 on
`-accepteula`, and both then run headless with no prompt.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The only
diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3` from those flags.

```
MSBuild src\PsGetSid\PsGetSid.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsGetSid\PsGetSid.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Deviations

1. **Remote machine SID over the GETSIDSV service**. The original deploys a
   bundled GETSIDSV helper to the remote `ADMIN$`, starts it through the SCM,
   then reads the SID from its `\pipe\getsidsv` named pipe. That helper binary
   is out of scope, so the rebuild reproduces the faithful client half (the
   260-byte request and response, the status DWORD, the SID string, and the exact
   error path) and leaves the service auto-deploy unreproduced. Validation was
   local only, as allowed.
2. **SID string parsing**. The rebuild uses `ConvertStringSidToSidA`, which is
   stricter than the original's lenient hand parser. A malformed SID string that
   the original would still hand to `LookupAccountSid` (then report
   `Error querying SID:`) instead reports
   `Error: The SID specified is not a valid SID.` here. Well-formed SIDs are
   identical. Both are stderr error paths that exit nonzero.
3. **EULA interactive fallback**. The rebuild uses the console Y/N prompt that
   the original falls back to on headless hosts. The GUI RichEdit dialog is not
   reproduced, which a console tool does not need.
4. **Multi-target aggregate**. The original's return folds to any-succeeded. The
   rebuild returns all-succeeded, which is saner and identical on the
   single-target path the local cases exercise.
5. **OS version gate**. The original's "Windows XP or higher" check is dropped as
   always true on supported systems.
6. **Banner source**. The banner fields and version are fixed constants rather
   than read from a version resource at run time. The output is identical.

## Note on scope

Not added to any `.sln` by request. The Sysinternals projects are consolidated
separately. No commit was made.
