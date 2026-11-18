# PsFile (Sysinternals psfile64.exe v1.04)

Source-accurate C++20 console reconstruction. Lists the files opened on the local
or a remote computer through a network share, filtered by a file id or a path,
and can force-close a file by id. Uses the shared PsTools command-line front end.

- Original: `targets/sysinternals/psfile64.exe` (x64, MSVC, static CRT, console)
- Rebuild: `src/PsFile/PsFile.cpp` plus `src/common/PsTools.h`, project
  `src/PsFile/PsFile.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. The logic decompiled cleanly, so rea was not
  needed.

## What it does

```
Usage: psfile [\\RemoteComputer [-u Username [-p Password]]] [[Id | path] [-c]]
```

With no file identifier it lists every file opened remotely over a share. With an
`Id` it prints the single matching file. With a `path` it lists files whose server
path matches that full or partial path. `-c` closes the file named by the `Id`, or,
with a `path` and no id, closes every file the path matched. A leading `\\name`
targets a remote computer, and `-u` / `-p` supply alternate credentials for it.
`-accepteula` and `-nobanner` are the standard Sysinternals switches.

The enumeration is `NetFileEnum` level 3 on the srvsvc interface, the single-file
lookup is `NetFileGetInfo`, and the close is `NetFileClose`. All three require the
caller to be an administrator (or a server operator) on the target.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `sub_140004560` | `main` |
| `sub_140003090` | front end driver (version info, banner, EULA, arg strip) |
| `sub_140002980` | banner printer and `-nobanner` strip |
| `sub_140002080` / `sub_1400022A0` | EULA gate and `-accepteula` strip |
| `sub_140003F50` | command parser |
| `sub_140004440` | usage |
| `sub_1400039E0` | worker: enumerate, list, or close |
| `sub_140003EC0` | one open-file record |
| `sub_140003960` | close one file by id and report |
| `sub_140003290` / `sub_140003350` | remote connect / disconnect over `\\computer\IPC$` |
| `sub_1400013A0` | local computer name (`GetComputerNameW`) |
| `sub_140001520` | hidden password prompt |
| `sub_1400031C0` | print a system error (`FormatMessage`, netmsg.dll for net codes) |

### Argument grammar (`sub_140003F50`)

A token starting with `-` or `/` is a switch. `-u` and `-p` each consume the next
argument as the user name or password. `-c` sets the close flag and is accepted
only after an id or path has already been parsed, so `psfile -c` and `psfile -c 5`
both print usage while `psfile 5 -c` works. A token starting with `\\` names the
remote computer and only one is allowed. Any other token is a file id when its
first character is a digit (parsed with a `%d` scan, so a leading integer wins) or
a path otherwise. An id and a path are mutually exclusive. After the loop, `-u`
without a `\\computer` and `-p` without `-u` both print usage.

### Worker (`sub_1400039E0`)

The server name passed to the Net APIs is `\\` plus the typed computer, or NULL
for the local machine. The computer name shown in messages is the typed name, or
the local machine name when none was typed, which is why a local run still prints
`on NORT`.

- No id, list: header is `Files opened remotely on %s:` or, with a path,
  `Files opened remotely on %s matching %s:`, then one record per entry. An empty
  result prints `No files opened remotely on %s.`
- No id, close (`-c` with a path): `NetFileClose` on every matched entry, each
  printing `Closed file %s on %s.`
- Id, list: `File %d on %s:` then the one record.
- Id, close: `NetFileClose` then `Closed file %s on %s.`

The record format from `sub_140003EC0` is exact:

```
[<id>] <path>
    User:   <user>
    Locks:  <num_locks>
    Access: Read Write Create
```

Access bits are `Read ` (0x1), `Write ` (0x2), and `Create` (0x4), with the
trailing space after Read and Write but not Create. `FILE_INFO_3` is 32 bytes on
x64, matching the `32 * i` stride and the field offsets in the decompile.

### Remote path (`main`)

A typed computer that differs from the local name runs the worker first with no
credentials. On failure it calls `WNetAddConnection2` to `\\computer\IPC$` with
the supplied user and password and retries, then disconnects. A failed connect
prints `Unable to connect to %s. Verify user name and password.` and then reports
the original worker error. On a worker error the final message is
`Error listing remotely open files on %s:` when no id was given, or
`Error closing file %d on %s:` when an id was given (the original always says
"closing" in the id case even for a plain lookup), followed by the system error
message.

## Reuse of the shared front end

`src/common/PsTools.h` is reused read only for the banner, the `-nobanner` and
`-accepteula` strip, the EULA gate, the local computer name, the hidden password
prompt, the `\\computer\IPC$` connect and disconnect, and the system-error
printer. PsFile has its own argument grammar, so the parser is local and mirrors
`sub_140003F50` rather than `pstools::ParseCommandLine`.

Implemented locally because they are not in the shared header:

- The `NetFileEnum` / `NetFileGetInfo` / `NetFileClose` worker and the record
  format.
- `Widen` / `Narrow`, the active-code-page conversion needed because the Net
  share APIs are wide only while the suite front end is ANSI. Output matches the
  original for ASCII paths.
- A small RAII guard that frees the `NetApiBuffer`. The original leaks it, since
  the process exits right after. Output is unchanged.

## Deliberate departures

- The Windows-version gate (`sub_140002FD0`, "PsFile requires Windows XP or
  higher") is omitted. It always passes on any supported system, so it carries no
  behavior.
- The interactive RichEdit EULA dialog is skipped in favor of the shared console
  Y/N prompt, the same headless path the original falls back to, and the same
  choice already made across the suite.
- The leaked NetApi buffer is freed through RAII.

## Build

```
MSBuild src\PsFile\PsFile.vcxproj /p:Configuration=Debug   /p:Platform=x64 /m
MSBuild src\PsFile\PsFile.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```

Both configurations build clean. The only compiler note is the shared
`/Ob2` vs `/Ob3` D9025 from `Directory.Build.targets`, common to every tool in
the tree. The Net share functions pull in `netapi32.lib`, added alongside the
`advapi32.lib` and `mpr.lib` the shared front end already needs.

## Validation

Original versus rebuild, stdout compared on the same inputs. `NetFileEnum`
requires an administrator, so the non-elevated cases exercise the access-denied
error path and the elevated cases exercise the success path. Elevated runs went
through the project's `RewsElevatedLaunch` scheduled task with output redirected
to a file. No real open file was force-closed.

Non-elevated (current session is a filtered admin, medium integrity):

| Case | Result |
|------|--------|
| list, no args | identical. `Error listing remotely open files on NORT:` plus `Access is denied.` on stderr |
| path filter | identical, same error path |
| specific id | identical. `Error closing file 12345 on NORT:` |
| id plus `-c` | identical |
| invalid switch | usage, identical but for each exe echoing its own argv[0] path |
| `-c` before id | usage, identical |
| `-p` without `-u` | usage, identical |

Elevated (success path, a probe file held open over the loopback `\\127.0.0.1\C$`
share for the duration of the run):

| Case | Result |
|------|--------|
| list, nothing open | identical. `No files opened remotely on NORT.` |
| list, one file open | identical full record, header plus `[id] path`, `User`, `Locks`, `Access: Read ` |
| full-path filter | identical. `Files opened remotely on NORT matching <path>:` plus the record |
| partial-path prefix | identical, matched the file |
| filter that matches nothing | identical. `No files opened remotely on NORT.` |

The only stdout difference anywhere was the usage line echoing each binary's own
full path for `%s`, which is correct. The actual force-close was not run against a
real file, per the task. The remote credential path is reproduced from the
decompile and validated indirectly by the `\\`-prefixed computer parsing and the
`Unable to connect` and error-report messages matching, but a true remote target
was not available to test end to end.
