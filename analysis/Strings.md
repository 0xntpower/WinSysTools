# Strings (Sysinternals) - reconstruction notes

Console recreation of the Sysinternals **Strings** utility, version 2.54. This is
a command-line tool, not a WTL GUI, so the toolbar, light-mode, and window-geometry
conventions of the NirSoft tools do not apply. Source lives at `src/Strings/`
(`Strings.cpp` plus `Strings.vcxproj`), built as an x64 and Win32 console
Application with the static CRT.

Original binary: `targets/sysinternals/strings64.exe`
(MD5 `e911a217d43ff6c453785e5606b46ce5`, SHA-256
`05dbb43499bcff44424c7ac2d987ea9e9742cd90afe360e84ba0bccade17f71b`).

## What it does

Scans a file, a wildcard, or a directory for runs of printable ASCII (`-a`) and
little-endian Unicode (`-u`) characters at least a minimum length long (`-n`,
default 3) and prints each run on its own line. Switches: `-a`, `-u`, `-b <bytes>`,
`-n <length>`, `-o` (print offset), `-f <offset>` (start offset), `-s` (recurse),
`-nobanner`, `-accepteula`, plus one file or directory or wildcard argument.

## Reverse engineering (IDA idalib MCP primary)

Key functions and globals recovered from the binary:

| Address | Role |
|---------|------|
| `0x140003EF0` | `main` - argument parse, path resolve, result summary |
| `0x1400037E0` | core per-file scan (the byte-accurate heart) |
| `0x1400033B0` | directory and wildcard walker, recursion for `-s` |
| `0x140003DC0` | usage printer |
| `0x140001050` | banner printer, strips `-nobanner` |
| `0x140002160` / `0x140002400` | EULA check, strips `-accepteula`, registry flag |

### Scan algorithm (`0x1400037E0`)

The file is read in windows of `0xFFFF` bytes into a `0x10000` buffer. Two run
trackers advance together over the window, one for ASCII and one for Unicode.
Each is gated by `(other == -1 || pos - other < minLen)`, so once one kind
reaches the minimum length the other kind is suppressed for that span. This is how
the original avoids reporting the same span twice.

- ASCII printable test is a plain inclusive range `0x20..0x7E`, written in the
  binary as `(unsigned char)(c - 32) > 0x5E` for the non-printable case.
- Unicode printable test reads the little-endian 16-bit value and calls the CRT
  `_ismbcprint`, with an explicit allow for `0x7E` (`~`) which that routine rejects.
  The recreation calls the same `_ismbcprint` so the classification is identical.
- A Unicode run prints the low byte of each wide character, which for UTF-16LE
  ASCII is the readable character. The recreation writes those low bytes directly.

Boundary handling: on a full window with a run still open, the original rewinds
the file to the run start and re-reads, so a run that straddles a 64 KB window is
seen whole. An offset base is carried and adjusted through the rewind so printed
offsets stay anchored to the scan start. The recreation ports this verbatim, which
is why the multi-window `notepad.exe` and `kernel32.dll` outputs match byte for byte.

### Output format

- Line ending is CRLF. The original writes to stdout in the default mode, so each
  terminating `\n` becomes `\r\n`. The recreation keeps stdout in that mode.
- With `-o`, the offset is decimal, zero padded to a minimum width of 4, as
  `%04u:` inside a window and `%04d:` at a window flush. The two produce identical
  output for non-negative offsets.
- With a wildcard or `-s`, each line is prefixed with the full file name and `: `.
- The offset with `-f` is relative to the scan start, not the absolute file offset.
- `-b` scans at most that many bytes from the start or from the `-f` point.

### EULA and registry

`-accepteula` (or a prior accepted run) lets the tool run unattended and writes
`HKCU\Software\Sysinternals\Strings\EulaAccepted` as REG_DWORD `1`. The recreation
reads that value, and on `-accepteula` creates the key and writes it. A first run
with neither prints the accept notice to stderr and exits with code 1, matching the
original's headless path.

### Integer parsing

`-n`, `-f`, `-b` go through the CRT integer parse (base 10, like `atoi`). A value
that parses to `0` is rejected with usage, so `-f 0x04` and `-f 0` both print usage.
Defaults in `.data`: `minLen = 3`, `maxBytes = -1` (scan to end), `startOffset = 0`.

## Validation (byte for byte vs the original)

Each case ran the original and the Release x64 rebuild with the same arguments plus
`-nobanner -accepteula`, captured raw stdout bytes through `cmd` redirection, and
compared SHA-256. The Debug x64 build was checked on the switch cases as well and
matched the original on every one.

| Case | Input | Switches | Result |
|------|-------|----------|--------|
| default | sample.txt | none | match |
| ascii | sample.txt | -a | match |
| unicode | sample.txt | -u | match |
| min length | sample.txt | -n 5 | match |
| offset | sample.txt | -o | match |
| ascii+offset | sample.txt | -a -o -n 4 | match |
| unicode+offset | sample.txt | -u -o | match |
| default | notepad.exe | none | match |
| ascii | notepad.exe | -a | match |
| unicode | notepad.exe | -u | match |
| offset | notepad.exe | -o | match |
| ascii+offset | notepad.exe | -a -o -n 8 | match |
| default | kernel32.dll | none | match |
| offset | kernel32.dll | -o | match |
| unicode | kernel32.dll | -u -n 6 | match |
| start offset | notepad.exe | -a -o -f 40000 | match |
| byte limit | notepad.exe | -a -o -b 20000 | match |
| start+limit | kernel32.dll | -o -f 1000 -b 5000 | match |
| wildcard | tree\*.txt | -a -o | match |
| wildcard star | tree\* | -a | match |
| recurse | tree | -a -s -o | match |
| recurse unicode | tree | -u -s | match |

All 22 cases are byte identical. `notepad.exe` (360 KB) and `kernel32.dll`
(862 KB) exercise the multi-window rewind path. The usage lines, the
`No matching files were found.` and the `No strings found.` stderr messages also
match. The usage first line differs only where it echoes its own executable path,
which is correct.

## Deviations from the original

- **No `-q` switch.** Version 2.54 has no `-q`. The argument parser treats an
  unrecognized token as the path, so the original prints usage when `-q` is given.
  The recreation matches that, rather than inventing a switch the binary lacks.
- **No stdin scan.** Version 2.54 requires a file, directory, or wildcard argument.
  A bare pipe into the tool prints usage. The recreation matches.
- **EULA front end.** The original's RichEdit accept dialog and the IoT and Nano
  Server console Y/N prompt are not reproduced. The headless path matters here, so
  the `-accepteula` switch, the registry flag, and the first-run notice are what the
  recreation carries.
- **Path buffers.** The original uses fixed 272-byte path buffers. The recreation
  uses `std::string` for the walker so a long path is not truncated. This is a
  fidelity improvement over a buffer defect and does not change extracted output.
- **Banner source.** The original reads its banner strings from its own version
  resource. The recreation hardcodes the same lines with version 2.54. With
  `-nobanner` the banner never prints, and the hardcoded content matches the
  original's banner otherwise.

## Build

VS 2022 Community MSBuild, PlatformToolset v143, `LanguageStandard stdcpp20`,
`CharacterSet Unicode`, SubSystem Console, static CRT (`/MT` Release, `/MTd` Debug).
The repo `Directory.Build.targets` adds the Release hardening and speed flags, so the
project does not repeat them. All four configurations build clean (one benign
`D9025 /Ob2 overridden by /Ob3` notice from the shared targets). Not added to any
solution, per instruction.

Status: done and validated.
