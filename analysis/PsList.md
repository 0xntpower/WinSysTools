# PsList (Sysinternals PsList64.exe v1.41)

Source-accurate C++20 console reconstruction. Lists running processes with CPU
time, elapsed time, priority, thread and handle counts, and memory, plus
per-thread detail (`-d`), memory detail (`-m`), the combined view (`-x`), a
process tree (`-t`), and a task-manager refresh mode (`-s`). Reuses the shared
PsTools front end for the banner and the EULA gate.

- Original: `targets/sysinternals/pslist64.exe` (x64, MSVC, static CRT, console)
- Rebuild: `src/PsList/PsList.cpp` (banner and entry flow) plus the headers
  `ProcessEnum.h` (NtQuerySystemInformation layout and snapshot collection),
  `CommandLine.h` (switch parse and filter), and `ProcessPrint.h` (column layouts,
  tree, and task-manager loop), with `src/common/PsTools.h`, project
  `src/PsList/PsList.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: IDA idalib MCP primary. The program logic is one large `main`
  (`sub_140004410`, about 11 KB), so the format strings, counter names, the
  thread-state and wait-reason arrays, and the message strings were lifted from
  the binary directly and cross-checked against the live output.

## What it does

```
Usage: pslist64 [-d][-m][-x][-t][-s [n] [-r n] [\\computer [-u username][-p password][name|pid]
```

With no filter it lists every process. A trailing token filters by process name
(prefix match, case-insensitive) or, when the token is all digits, by PID. `-e`
makes the name match exact. `-d` shows per-thread detail, `-m` shows the memory
columns, `-x` shows both, and `-t` draws the parent-child tree. `-s [n]` runs the
chosen view task-manager style, refreshing every `-r n` seconds (default 1) for
an optional `n` seconds or until Escape. `\\computer` with `-u` and `-p` targets
a remote machine. `-accepteula` and `-nobanner` are the standard switches.

## Reverse engineering findings

### Column layout (format strings recovered from the binary)

Every column width comes straight from the original's format strings, so the
layout matches byte for byte.

| View | Header format | Row format |
|------|---------------|------------|
| default | `%-17s %5s %3s %3s %4s %6s %15s %15s` | `%-17s %5d %3d %3d %4d %6d %15s %15s` |
| `-m` | `%-17s %5s %7s %7s %7s %7s %8s %6s %4s` | `%-17s %5d %7llu %7I64d %7I64d %7I64d %8I64d %6I64d %4I64d` |
| `-t` | `%-30s %5s %3s %3s %4s %7s %7s %7s` | `%-30s %5d %3d %3d %4d %7llu %7d %7d` |
| thread | `%4s %3s %9s %16s %13s %13s %14s` | `%4d %3d %9I64d %16s %12s %13s %14s` |

The CPU time, elapsed time, and the per-thread user, kernel, and elapsed fields
all format a 100-nanosecond tick count through `%2d:%02d:%02d.%03d`
(hours, minutes, seconds, milliseconds). Memory columns are shown in KB (the raw
byte value divided by 1024), matching the help footer.

### Column to field mapping

- default: Name, Pid, Pri (base priority), Thd (thread count), Hnd (handle
  count), Priv (page file bytes), CPU Time (kernel plus user), Elapsed Time
  (now minus create time).
- `-m`: Name, Pid, VM (virtual bytes), WS (working set), Priv, Priv Pk (peak
  page file bytes), Faults (page fault count), NonP (nonpaged pool), Page
  (paged pool).
- `-t`: default fields through Hnd, then VM, WS, Priv, with the name indented two
  spaces per tree level.
- thread: Tid, Pri (current priority), Cswtch (context switches), State, User
  Time, Kernel Time, Elapsed Time.

### Data source

The original reads the registry performance counters. It resolves the counter
names recovered from the binary ("ID Process", "% User Time", "% Privileged
Time", "Elapsed Time", "Virtual Bytes", "Working Set", "Page File Bytes",
"Pool Nonpaged Bytes", "Pool Paged Bytes", "Page Faults/sec", "Priority Base",
"Thread Count", "Handle Count", "Creating Process ID", "ID Thread",
"Priority Current", "Thread State", "Thread Wait Reason", "Context Switches/sec")
to their perf-library indices, then walks the `PERF_DATA_BLOCK` returned by
`HKEY_PERFORMANCE_DATA`, locally or over `RegConnectRegistryA` for a remote
target.

This reconstruction reads the same field set from
`NtQuerySystemInformation(SystemProcessInformation)`, which the task permits and
which yields identical columns and identical values for ordinary processes. See
the deviations for the two spots where the data source shows through.

### Thread state and wait reason tables

Recovered verbatim from the string arrays in the binary (states at `0x14003ace0`,
wait reasons at `0x14003a3a0`, both with a 0x40 stride). A running or ready
thread shows the state name. A waiting thread shows `Wait:` joined to its wait
reason, for example `Wait:LpcReceive` or `Wait:Executive`. A wait reason past the
recovered table shows `Wait:Unknown`, matching the original.

### Per-mode structure

Recovered from the exact byte layout of the original output.

- default and `-m`: `title:\n\n`, then the header, then one row per process.
- `-d`: `Thread detail for %s:\n\n`, then for each process `\n%s %d:\n`, the
  thread header, and the thread rows.
- `-x`: `Process and thread information for %s:\n`, then for each process `\n`,
  the memory header, the memory row, the thread header, and the thread rows.
- A name or PID filter that matches nothing prints the title, then
  `process <name|pid> was not found on <computer>`, then a blank line. The name
  path ends with the original's trailing line-clear space, the PID path does not.

## Validation

Original and rebuild (x64 Release) were run with the same arguments and
`-nobanner`, and the output was compared. The process list is volatile, so the
counters that move between two runs (elapsed time, CPU time, context switches,
and memory that drifts) were normalized, and a stable process launched for the
test (a `notepad`) was used to confirm the static columns.

| Check | Result |
|-------|--------|
| default title plus header | byte-identical |
| `-m` title plus header | byte-identical |
| `-t` title plus header | byte-identical |
| `-d` title plus thread header | byte-identical |
| `-x` title plus headers | byte-identical |
| banner plus full usage plus abbreviation key (`-?`) | byte-identical (exe path aside) |
| banner forced on the usage path under `-nobanner` | byte-identical |
| name-not-found, all of default/`-m`/`-d`/`-x` | byte-identical |
| PID-not-found, default and `-m` | byte-identical |
| stable `notepad`, default columns | identical but for the drifting Elapsed Time |
| stable `notepad`, `-m` columns | fully identical (VM, WS, Priv, Priv Pk, Faults, NonP, Page) |
| stable `notepad`, `-d` thread rows | identical but for the drifting Elapsed Time |
| stable `notepad`, `-x` | structure identical, only live thread churn between the two snapshots |
| Debug build versus Release build | identical output |

A representative match (same instant, `-m` on a long-lived `notepad`):

```
Name                Pid      VM      WS    Priv Priv Pk   Faults   NonP Page
Notepad           248692 2154558576  125168  107280  109304    35976     57 1199
```

The memory values, priority, thread count, handle count, and CPU time came out
identical between the two tools, which confirms the field set read from
`NtQuerySystemInformation` matches what the original reads from the performance
counters for an ordinary process.

## Deviations

1. **Data source.** The original reads the registry performance counters. This
   reconstruction reads the same fields from
   `NtQuerySystemInformation(SystemProcessInformation)`. For ordinary processes
   the columns and values are identical, as the validation shows. Two artifacts
   follow from the choice.
   - **Tree nesting of kernel minimal processes.** The `-t` column layout and
     indentation are identical, but the Registry and Secure System processes nest
     differently. The performance counter "Creating Process ID" reports 0 for
     these minimal processes, so the original roots them, while
     `NtQuerySystemInformation` reports their parent as the System process (4), so
     the rebuild nests them under System. The nesting mechanics, indentation, and
     column widths all match.
   - **Elapsed time on the Idle and System pseudo-threads.** For a real thread the
     elapsed field matches. The per-processor Idle threads and the early System
     threads carry a create time of zero in the perf view, which the original
     renders as it reads it, and the rebuild computes `now minus create time` for
     the same records, so those specific pseudo-threads can show a different
     elapsed value. Real threads are unaffected.
2. **Remote targets.** The original serves `\\computer` through the remote
   registry. This local-focused reconstruction queries the local machine, accepts
   the `\\computer`, `-u`, and `-p` switches, and reports a clear message for a
   non-local target rather than reproducing the remote-registry path. Validation
   was local only.
3. **Memory Compression instance name.** The performance counter instance name is
   `Memory Compression`, while the image name from `NtQuerySystemInformation` is
   `MemCompression`. The rebuild shows the image base name for every process, so
   that one pseudo-process reads `MemCompression`.
4. **Task-manager mode input.** The original drives `-s` with
   `ReadConsoleInputW`. The rebuild keeps the same clear-and-redraw loop with a
   simpler `_kbhit` poll for the Escape key, which reads identically for a console
   tool.
5. **EULA interactive fallback.** The shared PsTools front end uses the console
   Y/N prompt that the original falls back to on headless hosts. The GUI dialog is
   not reproduced, which a console tool does not need.
6. **Banner source.** The banner fields and version are fixed constants rather
   than read from a version resource at run time. The output is identical.

## Build

VS 2022 Community MSBuild, v143, x64 Debug and Release, both clean. The repo
`Directory.Build.targets` applies the Release hardening and speed flags. The only
diagnostic is the harmless `D9025 overriding /Ob2 with /Ob3` from those flags.

```
MSBuild src\PsList\PsList.vcxproj /p:Configuration=Debug   /p:Platform=x64
MSBuild src\PsList\PsList.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Shared framework note

PsList uses the PsTools banner and EULA gate from `src/common/PsTools.h` unchanged
(read-only, as required). Its command line differs from the SID and service tools,
so the switch parsing lives in `CommandLine.h`, the process and thread collection in
`ProcessEnum.h`, and the column rendering, tree builder, and task-manager loop in
`ProcessPrint.h`, each in `namespace pslist`. `PsList.cpp` keeps only the banner and
the entry flow. No addition to `PsTools.h` was needed.

## Re-validation after the file split (2026-11-18)

`PsList.cpp` was split into `ProcessEnum.h`, `CommandLine.h`, and `ProcessPrint.h`
(behaviour-preserving code motion plus the HRC clang-format pass), and both x64
configs rebuilt clean. Two checks confirmed the behaviour held.

- `-?` usage. The banner apart, the rebuilt `Usage:` block through the abbreviation
  key is byte-identical to `targets/sysinternals/pslist64.exe`, the invoked exe path
  aside. All 32 usage lines matched.
- Stable `lsass` (`-nobanner lsass`). The title, the header row, and the column
  layout matched the original. The stable identity columns came out identical (Name
  `lsass`, Pid `1552`, Pri `9`, Thd `13`, Priv `17428` KB). Only the naturally
  volatile fields moved between the two snapshots, the handle count by one (2320
  against 2319) and the elapsed time by the few seconds between the two runs. CPU
  Time matched to the millisecond, the service being idle across both.

## Note on scope

Not added to any `.sln` by request. No commit was made.
