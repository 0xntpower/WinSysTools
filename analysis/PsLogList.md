# PsLogList (Sysinternals psloglist64.exe v2.82)

Source-accurate C++20 console reconstruction. Dumps the records of an event log
on a local or remote NT system through the classic `ReadEventLog` API, resolving
each record's message from the source's registered message DLLs. Reuses the shared
PsTools command-line front end for the banner, the EULA gate, alternate
credentials, the error printer, the local computer name, the hidden password
prompt, and the target iteration.

- Original: `targets/sysinternals/psloglist64.exe` (x64, MSVC, static CRT, console)
- Rebuild: `src/PsLogList/` (thin `PsLogList.cpp` plus the `LogListUtil.h`,
  `CommandLine.h`, `Usage.h`, `EventMessage.h`, and `EventLog.h` units) over
  `src/common/PsTools.h`, project `src/PsLogList/PsLogList.vcxproj` (console, v143,
  stdcpp20, Unicode, static CRT)
- Engines: IDA idalib MCP primary. The whole tool decompiled cleanly, so the rea
  cross-check was not needed.

## What it does

```
Usage: psloglist [\\computer[,computer2[,...] | @file] [-u username [-p password]]]
       [-s [-t delimiter]] [-m #|-n #|-d #|-h #] [-c][-x][-r][-a mm/dd/yy][-b mm/dd/yy]
       [-f filter] [-i ID,[ID,...]] | -e ID,[ID,...]] [-o source[,...]] [-q source[,...]] <event log>
```

With no event-log argument it dumps the System log. The log can also be
Application, Security, or any named log, and a case-insensitive prefix of the
three built-ins maps to the proper-cased name. The record selection switches are
`-n` (most recent N), `-r` (oldest first), `-d`/`-h`/`-m` (previous N days, hours,
or minutes), `-a`/`-b` (after and before a date), `-f` (keep only types whose
first letter appears in the filter), `-i`/`-e` (include or exclude up to ten event
ids), and `-o`/`-q` (keep or drop records from the named sources, with a trailing
`*` for a substring match). `-s` lists one record per line with a delimiter set by
`-t`, `-x` adds the extended data hex dump, and `-c` clears the log after the dump.
`-accepteula` and `-nobanner` are the standard Sysinternals switches.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `main` 0x140009EC0 | OS gate, banner plus EULA driver, parse, run per computer |
| `sub_140003E90` | Banner plus EULA driver, run before parsing |
| `sub_1400077B0` | PsLogList command-line parse |
| `sub_140009A00` | Usage |
| `sub_140004D30` | Per-computer dumper, the read loop and record formatting |
| `sub_140006A20` | Message formatter, message DLLs and `%%n` parameters |
| `sub_140004B60` | Final `FormatMessage` across each message module |
| `sub_140008560` | Module loader, `LoadLibraryEx` as a data file, cached |
| `sub_1400089D0` | Read a registry string value |
| `sub_140004AA0` / `sub_140008BC0` | `ExpandEnvironmentStrings` and the ADMIN$ remote remap |
| `sub_1400063A0` | Source `-o` and `-q` filter |
| `sub_140008670` | SID to account name via `LookupAccountSid` |
| `sub_140006300` | Clear the log after display via `ClearEventLog` |
| `sub_140003CE0` | Open the remote registry via `LogonUser` and `RegConnectRegistry` |
| `sub_140003FC0` | Print a Windows error code, netmsg.dll for codes 2100 to 2483 |
| `sub_140003850` | ForEachComputer, the shared target iterator |
| `sub_140001F50` / `sub_1400041C0` | printf to stdout and fprintf to a stream |

### The record

Each record is the classic `EVENTLOGRECORD`. The dumper reads `RecordNumber` at
offset 8, `TimeGenerated` at 12, the full `EventID` at 20 (its low 16 bits are the
displayed and filtered id), `EventType` at 24, `NumStrings` at 26, `StringOffset`
at 36, `UserSidLength` at 40, `UserSidOffset` at 44, `DataLength` at 48, and
`DataOffset` at 52. The source name follows the fixed header at offset 56, and the
computer name follows the source's terminator.

`TimeGenerated` is a 1970-epoch `time_t`. The original turns it into a FILETIME
with `(t + 11644473600) * 10000000`, converts to local time, and renders the date
and time with `GetDateFormat` and `GetTimeFormat` under `LOCALE_USER_DEFAULT`. The
type word maps to ERROR, WARNING, INFORMATION, SUCCESS AUDIT, FAILURE AUDIT, or
UNKNOWN TYPE, tested bit by bit in that order.

### Read loop

`sub_140004D30` opens the log with `OpenEventLogW`, opens
`HKLM\SYSTEM\CurrentControlSet\Services\EventLog\<Log>` for message resolution,
then reads into a 0x4000 buffer with `EVENTLOG_SEQUENTIAL_READ` plus
`EVENTLOG_BACKWARDS_READ` by default or `EVENTLOG_FORWARDS_READ` under `-r`. It
grows the buffer on `ERROR_INSUFFICIENT_BUFFER` and stops on `ERROR_HANDLE_EOF`,
printing `No records in %s event log on %s.` when nothing was shown. A per-record
guard stops the walk when the record numbers stop being monotonic, which catches a
wrapped log. `-n` stops once the printed count reaches the limit.

### Record selection

A record passes when it clears, in order, the date window (`-a`, `-b`, and the
single `-d`/`-h`/`-m` cutoff compared against the record's local time), the type
filter (`-f` keeps a record whose type's first letter is in the filter string), the
id filter (`-i` keeps only listed ids, `-e` drops listed ids, the two are mutually
exclusive), and the source filter. The source filter splits `-o` or `-q` by comma,
matches each token against the source name exactly or, when the token carries a
`*`, as a case-insensitive substring of the part before the `*`, then negates the
result for `-q`.

### Message resolution

For every record the formatter opens the source subkey under the log key, reads
`EventMessageFile` from the source (falling back to the log's `PrimaryModule` key),
expands the environment variables in each semicolon-separated path, and loads each
DLL with `LoadLibraryEx` as a data file through a process-wide cache. It then calls
`FormatMessage` with `FROM_HMODULE | ALLOCATE_BUFFER | ARGUMENT_ARRAY`, the full
32-bit event id, and the record's insertion strings as the argument array, trying
each module until one resolves. Any `%%n` reference inside an insertion string is
resolved first against `ParameterMessageFile`. When no module resolves the id, the
message is the insertion strings joined with spaces, presented under "Insertion
strings". A final pass folds embedded line breaks, replacing tab, CR, and LF with a
space in one-line mode and a lone LF with a space in full mode.

### Output framing

The field layout was confirmed against the original. Record bodies are redacted
here for privacy. Full mode frames one record as:

```
<Log> log on \\<COMPUTER>:
[NNNNN] <Source>
   Type:     INFORMATION 
   Computer: <COMPUTER>
   Time:     dd/mm/yyyy HH:MM:SS   ID:       NNNN 
   User:     <NAME>\<DOMAIN>
   <message body redacted>
```

Date and time share a line with the id, each line that labels a field carries its
exact leading spaces, and the user field prints the account name, a backslash, and
the domain in that order, which is the order `LookupAccountSid` returns them. The
`-s` one-line form uses the delimiter between every field:

```
NNNNN<d>System<d><Source><d>INFORMATION<d><COMPUTER><d>dd/mm/yyyy HH:MM:SS<d>NNNN<d><NAME>\<DOMAIN><d>"<message body redacted>"
```

## Validation

Original and rebuild (x64 Release) were run with the same arguments and
`-nobanner`, stdout and stderr compared, exit codes compared. The `-r` runs read
the oldest records, which are immutable, so the comparison is byte exact. Volatile
fields did not need normalization because historical records and their timestamps
are stable. No log was ever cleared.

| Case | Result |
|------|--------|
| `/?` usage | identical |
| full `-r -n 10 System` | identical, 61 lines |
| one line `-s -r -n 10 System` | identical, 10 lines |
| tab delimiter `-s -t \t -r -n 400 System` | identical, 400 lines |
| extended data `-x -r -n 8 System` | identical, 153 lines |
| source include `-o Microsoft-Windows-* -r -n 300 System` | identical, 2242 lines |
| source exclude `-q DCOM -r -n 150 System` | identical, 901 lines |
| include id `-i 19 -r System` | identical, 6800 lines |
| exclude id `-e 19 -s -r -n 120 System` | identical, 120 lines |
| Application log `-r -n 40 Application` | identical, 281 lines |
| extended one line `-x -s -r -n 200 System` | identical, 200 lines |
| large forward scan `-r -n 2000 System` | identical, 12023 lines |
| bad argument `-y` | identical usage, 41 lines |
| nonexistent log `NoSuchLogXYZ` | identical error, 3 lines |
| newest default `-n 8 System` | identical |
| banner | identical |

One redacted sample, with the record body removed, confirmed the full-mode and
one-line field layout shown above matches the original byte for byte, including the
date, time, id, and user framing.

### One robustness difference

On `-f e -r -n 120 System` the original stops at 358 complete lines and exits with
code 127, crashing on a specific old WindowsUpdateClient error record during the
forward scan. The rebuild matches the original byte for byte through all 358 lines,
then formats that record and continues to the full request, exiting 0. The crash is
reproducible in the original and does not occur on the newest-first path, where
`-f e -n 20 System` gives the same 134 lines for both. The rebuild is more robust
here. Every other case above is byte exact.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The only
diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3` from those flags.

```
MSBuild src\PsLogList\PsLogList.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsLogList\PsLogList.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Refactor into units

The single `PsLogList.cpp` was split into a thin entry point plus five headers,
each a `#pragma once` unit under `namespace psloglist` with the free functions and
the module cache made `inline`, the RAII guards kept as classes, and every body
moved unchanged:

- `LogListUtil.h` the Win32 and CRT includes, the tool constants, the `EventLogGuard`
  and `RegKeyGuard` guards, the `Options` struct, and the `Widen` and `ContainsNoCase`
  helpers
- `CommandLine.h` the digit and id-list predicates, the log-name canonicalizer, the
  `-d`/`-h`/`-m` cutoff and `-a`/`-b` date parsing, and `ParseCommandLine`
- `Usage.h` the full original switch list
- `EventMessage.h` the record decode helpers, the message-DLL resolution, the SID
  lookup, and `PrintRecord`
- `EventLog.h` the source and record-selection filters, the log clear, and the
  per-computer read loop

`PsLogList.cpp` keeps the banner and app constants and `main`, and the five headers
are listed in the project's `ClInclude` group. The repo `.clang-format` (Allman, 120
column) was run over the result, the decorative section dividers are gone, and the
sub_ map stays in this file rather than the source banner. Behaviour is unchanged.

x64 Debug and Release both build clean, with only the documented `D9025` `/Ob3`
override. Re-validation ran the original `psloglist64.exe` and the rebuilt Release
exe with the same arguments and compared the bytes:

| Case | Result |
|------|--------|
| `-nobanner -r -n 5 System` oldest five | identical, 31 lines |
| `-nobanner -r -n 5 Application` oldest five | identical, 36 lines |
| `-nobanner -n 5 System` newest five | identical, 35 lines |
| `-nobanner -n 5 Application` newest five | identical, 31 lines |
| `-nobanner -x -r -n 8 System` extended data | identical, 153 lines |
| `-nobanner -s -r -n 10 System` one line | identical, 10 lines |
| `-nobanner -?` usage body | identical, 41 lines |
| `-nobanner -y` bad argument | identical, 41 lines |
| `-nobanner NoSuchLogXYZ` missing log | identical, 3 lines |

The banner first line (`PsLoglist v2.82 ...`) matches. The copyright and author lines
differ by design, as the recreation does not carry the Sysinternals copyright, which
is the pre-existing banner deviation noted below.

## Shared framework reuse

PsLogList reuses `src/common/PsTools.h` (read-only) for `PrintBanner`, `StripFlag`,
`AcceptEula`, `PrintSystemError`, `LocalComputerName`, `PromptHiddenPassword`,
`RemoteConnect`, `RemoteDisconnect`, and `ForEachComputer`. Nothing was added to
the shared header. The parser, the record read and format, the message resolution,
the SID resolution, the record selection filters, and the clear path are all local
to the PsLogList units, because the tool's grammar is far richer than the shared
account-list parser.

## Deviations

1. **Scope of switches.** The record-read, message-format, and record-selection
   switches are reproduced in full. The event-file and listing modes are out of
   scope and reported through usage when passed: `-g` (export an evt file), `-l`
   (read a saved evt file), `-w` (wait for new events), and `-z` (list the
   registered logs). The full original usage still lists them for fidelity.
2. **Remote message DLLs.** The original rewrites a remote source's message DLL
   paths onto the target's ADMIN$ share (`sub_140008BC0`) to resolve messages from
   the remote machine. The rebuild reads the remote registry through
   `RegConnectRegistry` and loads the message DLLs from the local file system, so
   remote message resolution is faithful only when the target carries the same
   DLLs. The remote path is validated locally only, as allowed.
3. **Original crash on one record.** The original crashes on a specific record
   during a `-r` forward scan of the System log, as recorded under validation. The
   rebuild handles the record and continues. This is a robustness improvement, not
   a format difference.
4. **Error exit codes.** The original returns the per-computer callback code
   directly, which is 1 on an open failure and 2 on a registry open failure. The
   rebuild folds any dump failure to 1 through the shared target iterator. Success
   is 0 and usage is -1 in both.
5. **Error-message flags.** The rebuild routes error codes through
   `pstools::PrintSystemError`, which omits `FORMAT_MESSAGE_IGNORE_INSERTS` that
   `sub_140003FC0` sets. The open and read error codes PsLogList emits carry no
   insert tokens, so the output is the same.
6. **OS version gate.** The original's Windows XP or higher check is dropped as
   always true on supported systems.
7. **Banner source.** The banner fields and version are fixed constants rather than
   read from a version resource at run time. The output is identical.

## Note on scope

Not added to any `.sln` by request. The Sysinternals projects are grouped
separately. No commit was made.
