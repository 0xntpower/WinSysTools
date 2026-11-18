# PsLoggedOn (Sysinternals PsLoggedon64.exe v1.35)

Source-accurate C++20 console reconstruction. Shows the users logged on to a
computer, both those logged on locally and those connected through resource
shares, with logon times. It can also search for a named user across the
computers of the current domain.

- Original: `targets/sysinternals/PsLoggedon64.exe` (x64, MSVC, static CRT, console, Unicode)
- Rebuild: `src/PsLoggedOn/PsLoggedOn.cpp` plus `src/common/PsTools.h`, project
  `src/PsLoggedOn/PsLoggedOn.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. The logic decompiled cleanly, rea cross-check not needed.

## What it does

```
Usage: psloggedon [-l] [-x] [\\computername]
    or psloggedon [username]
-l     Show only local logons
-x     Don't show logon times
-nobanner Do not display the startup banner and copyright message.
```

With no argument it reports the accounts logged on to the local machine. A
`\\computer` argument targets a remote machine. A bare `username` argument
searches every server in the current domain for that user. `-l` shows only the
local logons and skips the resource-share pass. `-x` drops the logon times.
`-accepteula` and `-nobanner` are the standard Sysinternals switches.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `wmain` 0x140003220 | Parse args, dispatch local or domain-search, drive the two enumerators |
| `sub_1400025A0` | Banner plus EULA driver, reads the version resource, run before parsing |
| `sub_140002070` | Banner printer, strips `-nobanner` from argv |
| `sub_140001AA0` | EULA check and accept, writes `HKCU\Software\Sysinternals\PsLoggedon\EulaAccepted` |
| `sub_1400018D0` | Strips `-accepteula`, drives the EULA check |
| `sub_1400026D0` | Local-logon enumerator, HKEY_USERS subkey walk, resolve each SID |
| `sub_140002CA0` | Resource-share enumerator, NetSessionEnum level 10 |
| `sub_140002C00` | Logon-time printer, GetDateFormat and GetTimeFormat |
| `sub_140003090` | Per-SID logon time, last-write time of the Volatile Environment subkey |
| `sub_140002500` | Nano Server check, HKLM ServerLevels NanoServer value |
| `sub_1400031D0` | Usage |

### The two enumeration paths

**Local logons (`sub_1400026D0`).** The loaded user profiles each appear as a
SID-named subkey under HKEY_USERS. The function opens HKEY_USERS (locally with
`RegOpenKey`, or on a remote machine with `RegConnectRegistry`), walks the
subkeys with `RegEnumKeyEx`, and skips three classes of name:

- `.default` matched case-insensitively
- any name containing `Classes` matched as a substring, which drops the per-user
  `_Classes` class hives
- the three service SIDs `S-1-5-18`, `S-1-5-19`, `S-1-5-20`

Each surviving name is a SID string. The original parses it back into a binary
SID with a `swscanf` of `S-%d-%x-%lu-...` followed by `AllocateAndInitializeSid`,
then calls `LookupAccountSid`. A profile that resolves is printed as
`domain\account`. The logon time comes from `sub_140003090`, which opens the
`<SID>\Volatile Environment` subkey and reads its last-write time through
`RegQueryInfoKey`, then converts to local time. A profile with no such subkey,
a service account for example, prints `<unknown time>`.

**Resource-share logons (`sub_140002CA0`).** `NetSessionEnum` at level 10 lists
inbound SMB sessions. For each session with a non-empty user name the client
name becomes the system passed to `LookupAccountName`, and the session's elapsed
seconds field gives the logon time as the current local time minus that many
seconds. Sessions whose account resolves print `domain\account`, the rest print
`clientname\account`.

### Argument model

`wmain` reads positional arguments after the banner and EULA driver have removed
`-nobanner` and `-accepteula`. A leading `-` or `/` marks a switch (`l`/`L` for
local only, `x`/`X` for no times, `?` for usage, anything else ignored), a
leading `\\` marks the computer, and anything else is the search user. More than
three real arguments prints usage and returns -1. A second computer or a second
user name is also a usage error. When a user name is present the tool runs the
domain-wide search with `NetServerEnum` and ignores any `\\computer`. Otherwise
it lists the local or named machine.

### Output format

The logon-time field is `     %s %s    ` with five leading and four trailing
spaces and no newline, date then time, both from the user locale. The account
line is `%s\t%s\\%s\n`. The prefix before the tab is five spaces when times are
hidden and empty when they are shown, since the time field already supplies the
leading spaces. The `<unknown time>` and Nano fallbacks carry their own trailing
tab, which is why those lines show two tabs before the account. The headers are
`Users logged on locally:` and `\nUsers logged on via resource shares:`. The
empty cases are `No one is logged on locally.` and
`\nNo one is logged on via resource shares.`. The remote and search paths erase
their progress line with a carriage return, 78 spaces, and a carriage return.

### Banner and EULA

The banner format from `src/common/PsTools.h` reproduces the original exactly,
drawn from the version resource fields InternalName `PsLoggedon`, FileVersion
`1.35`, FileDescription `See who's logged on`, the copyright, and the company.
The EULA flag lives at `HKCU\Software\Sysinternals\PsLoggedon\EulaAccepted` as a
DWORD, the same key the original writes, confirmed by inspection after a run of
each binary.

## Validation

Original and rebuild (x64 Release) run with identical arguments, stdout and
stderr captured to separate files and compared byte for byte, exit codes
compared. All deterministic on the local machine. The logon time shown is the
Volatile Environment last-write time, fixed at logon, so it is stable across the
two runs and the captures match without normalizing. Real account names are not
reproduced here and are shown as `<user>`.

| Arguments | stdout | stderr | exit |
|-----------|--------|--------|------|
| `-nobanner` (local, default) | identical (315 bytes) | identical | 0 / 0 |
| `-nobanner -x` | identical (232 bytes) | identical | 0 / 0 |
| `-nobanner -l` | identical (271 bytes) | identical | 0 / 0 |
| `-nobanner -l -x` | identical (188 bytes) | identical | 0 / 0 |
| (banner, no `-nobanner`) | identical (438 bytes) | identical | 0 / 0 |
| `-nobanner \\<thismachine>` | identical (430 bytes) | identical | 0 / 0 |
| `-nobanner -l \\<thismachine>` | identical (386 bytes) | identical | 0 / 0 |
| `-nobanner -?` | identical after argv0 normalize | identical | -1 / -1 |

The `\\<thismachine>` cases exercise the remote-registry branch, the
`Connecting to Registry of %s...` progress line, and the 78-space clear line.
Both match.

Output layout reproduced exactly, with names redacted and the volatile parts
intact because they are stable:

```
Users logged on locally:
     12/11/2026 16:37:19    	<DOMAIN>\<user>
     <unknown time>		NT SERVICE\<svc>
     <unknown time>		NT SERVICE\<svc>

No one is logged on via resource shares.
```

The first line carries a real logon time from the interactive user's Volatile
Environment key. The service accounts have no such key and fall to
`<unknown time>`, which is why they show two tabs. Under `-x` the time field is
dropped and every line reduces to five spaces, a tab, then `domain\account`.
Under `-l` the resource-share section is absent.

The EULA flag write was checked against the original. Both create
`HKCU\Software\Sysinternals\PsLoggedon\EulaAccepted` as DWORD 1 on `-accepteula`,
and both then run with no prompt.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The only
diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3` from those flags.

```
MSBuild src\PsLoggedOn\PsLoggedOn.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsLoggedOn\PsLoggedOn.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Reuse of the shared framework

`src/common/PsTools.h` is reused read-only for the banner (`Banner`,
`PrintBanner`), the `-nobanner` and `-accepteula` stripping (`StripFlag`), and
the EULA gate (`AcceptEula`, which carries the registry read and write). The
PsLoggedon argument model is positional and unlike the other PsTools front end,
so the parsing, the two enumerators, the time formatting, the Nano check, and
the error reporting is implemented locally in `PsLoggedOn.cpp` rather than taken
from the header. Nothing in the shared header was changed.

## Deviations

1. **SID string to binary SID**. The rebuild turns each HKEY_USERS subkey name
   into a SID with `ConvertStringSidToSidA` instead of the original's `swscanf`
   plus `AllocateAndInitializeSid`. Well-formed SIDs, which is every name that
   passes the filters, resolve identically. A malformed name is skipped by both,
   the original through its field-count guard and the rebuild through a failed
   conversion.
2. **ANSI account and registry APIs**. The original is Unicode throughout. The
   rebuild uses the ANSI registry and account APIs and prints with `printf`, the
   same choice the PsGetSid rebuild validated. For ASCII account names the
   console bytes are identical. `NetSessionEnum` and `NetServerEnum` have no ANSI
   form, so those stay wide and their names are narrowed for display.
3. **EULA interactive fallback**. The rebuild uses the console Y/N prompt that
   the shared framework falls back to on headless hosts. The original's RichEdit
   license dialog is not reproduced, which a console tool does not need.
4. **Local HKEY_USERS open-failure path**. The original's failure branch for the
   local `RegOpenKey` uses an uninitialized buffer as a format string, which is
   undefined behaviour that never triggers because HKEY_USERS always opens. The
   rebuild prints a well-formed error instead rather than reproduce the defect.
5. **Banner source**. The banner fields and version are fixed constants rather
   than read from a version resource at run time. The output is identical.
6. **Resource-share time truncation**. The original truncates the
   `10000000 * seconds` offset to 32 bits before subtracting it from the file
   time, so a session longer than about 7 minutes wraps. The rebuild keeps that
   truncation for fidelity.

## Note on scope

Not added to any `.sln` by request. The Sysinternals projects are consolidated
separately. No commit was made.
