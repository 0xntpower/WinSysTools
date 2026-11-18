# ProcDump (Sysinternals) - reconstruction notes

Console recreation of the Sysinternals **ProcDump** utility, version 11.0. This is
a command-line tool, not a WTL GUI, and it is not a PsTool, so the NirSoft toolbar
and window conventions and the shared `PsTools.h` front end do not apply. Source
lives at `src/ProcDump/`, built as an x64 console Application with the static CRT.

## Source layout

The implementation is split into a thin entry point plus a header per concern, all
under a `procdump` namespace with inline free functions and classes, mirroring the
PsService unit split:

- `ProcDump.cpp` - banner, app constants and `wmain` (the top-level flow)
- `ProcDumpCommon.h` - shared primitives: the RAII `ScopedHandle`, the `Config` and
  `Target` records, the `DumpType` enum, and the narrow, switch and timestamp helpers
- `CommandLine.h` - the EULA gate and the switch and argument parse
- `DumpWriter.h` - the dump-type flag words, the filename scheme, and the
  MiniDumpWriteDump call with its status lines
- `Monitor.h` - process selection, the monitoring header, the samplers, and the
  trigger loops (immediate, timed, CPU, commit, hung, exception)
- `Usage.h` - the usage help block

The code was moved verbatim and run through the repo `.clang-format` (HRC v1.1
Allman config). Behaviour is unchanged.

Original binary: `targets/sysinternals/procdump64.exe`
(MD5 `68a1f7c796de1d0df6b2d78e182df3a0`, SHA-256
`5b165b01f9a1395cae79e0f85b7a1c10dc089340cf4e7be48813ac2f8686ed61`, 424856 bytes,
PDB `ProcDump64.pdb`).

## What it does

Monitors a target process and writes a user-mode crash dump when a trigger fires.
The target is a PID, a process name, or a service name, optionally followed by a
dump file or dump folder. Dump sizes are selected with `-mm` (Mini, default),
`-ma` (Full), `-mt` (Triage), `-mp` (MiniPlus), `-mc <Mask>` (Custom) and
`-md <DLL>` (Callback). Triggers are the immediate or timed count (`-n`, `-s`),
CPU usage (`-c`, `-cl`, `-u`), memory commit (`-m`, `-ml`), an unhandled exception
(`-e`, `-b`), a hung window (`-h`) and termination (`-t`). `-o` overwrites,
`-w` waits for a named process, `-accepteula` accepts the EULA.

## Scope

The reconstruction reproduces the core faithfully: argument parsing, process
selection by PID or name, the MiniDumpWriteDump dump writer with the per-type flag
words, the dump-file naming and collision handling, the console status lines and
the monitoring header, the EULA gate, and the trigger set (immediate, timed, CPU,
memory commit, exception, hung window). The clone (`-r`), Windows Error Reporting
(`-wer`), AeDebug install and uninstall (`-i`, `-u`), process launch (`-x`),
performance counter (`-p`, `-pl`), debug-string logging (`-l`), native-debugger
(`-g`), content filtering (`-f`, `-fx`), kernel dump (`-mk`), WOW64 (`-64`),
cancel (`-cancel`) and the managed-CLR exception interop are out of scope and are
noted below.

## Reverse engineering (IDA idalib MCP primary, rea cross-check)

Key functions recovered from the binary:

| Address | Role |
|---------|------|
| `0x140011550` | dbghelp loader - GetProcAddress for MiniDumpWriteDump, PSS and reflection, builds the dump-flags table |
| `0x140006A50` / `0x140006800` | per-version dump-flag computation and dbghelp version detection |
| `0x140013D00` | dump dispatcher - builds the command-line user stream, routes to the normal, reflection or snapshot writer |
| `0x140014060` | normal writer - prints the `initiated` line, calls the MiniDumpWriteDump wrapper |
| `0x140014620` | MiniDumpWriteDump wrapper - the retry, the `complete` line, the file-size readback |
| `0x140014900` | MiniDumpWriteDump callback - the memory-size estimate for the `writing` line |
| `0x140013CE0` | full-memory classifier for the dump type |

### Dump-type flags (`0x140006A50`)

The flags are not constants. `sub_140006A50` computes a MINIDUMP_TYPE word from the
detected dbghelp version (`ImagehlpApiVersion`, falling back to export probing that
yields a synthetic major 6 for a modern dbghelp). For a modern dbghelp the three
computed words are:

- Triage = `0x001285A0` (WithUnloadedModules, FilterModulePaths, WithProcessThreadData, WithoutOptionalData, WithFullAuxiliaryState, IgnoreInaccessibleMemory, FilterTriage)
- Mini and the metadata-rich base = `0x00469925` (WithDataSegs, WithHandleData, WithUnloadedModules, WithProcessThreadData, WithFullMemoryInfo, WithThreadInfo, WithFullAuxiliaryState, IgnoreInaccessibleMemory, WithTokenInformation, WithIptTrace)
- Full = `0x00469927` (the Mini base plus WithFullMemory)

The recreation uses these exact words. MiniPlus and Callback share the Mini base
(their extra memory comes from the write callback in the original). Custom uses the
user hex mask. When the first MiniDumpWriteDump call fails with E_INVALIDARG the
wrapper clears WithIptTrace (`0x00400000`) and retries, which the recreation mirrors.

### Dump writer and status lines

The wrapper at `0x140014620` calls the dynamically resolved MiniDumpWriteDump with
the per-dump flags, the exception information, the user stream and the callback,
then reads the written file size back with GetFileSizeEx. The status lines are
`[%02d:%02d:%02d] ` prefixed:

- `Dump %ld initiated: %s` with the dump path
- `Dump %ld writing: Estimated dump file size is %ld MB.` for the Full dump only
- `Dump %ld complete: %ld MB written in %0.1f seconds`
- `Dump count reached.` after the last dump

The MB figure is `(bytes + 1023999) / 1024000` in both the estimate and the complete
line, taken straight from the binary. The estimate `writing` line is printed only
for the Full (`-ma`) dump in practice, observed across `-mm`, `-ma`, `-mt`, `-mp`,
`-mc 0` and `-mc 2`.

### Process selection and naming

A numeric target is a PID, anything else is a name matched case-insensitively
against each process base name with and without a `.exe` suffix. A name with no
match prints `No process matching the specified name can be found.` plus the
elevation hint, a PID with no match prints the PID variant, and more than one name
match prints `Multiple processes match the specified name.`. `-w` waits with
`Waiting for process named %s...`.

The default filename mask is `PROCESSNAME_YYMMDD_HHMMSS.dmp`, with `PROCESSNAME`
preserving the on-disk case of the image base name (not uppercased - a mixed-case
copy produced `MixedCaseProc.exe_...dmp`), `YYMMDD` using the two-digit year. A
folder argument is concatenated verbatim with a backslash and the expanded mask,
which is why a trailing-slash folder yields `folder/\name`. Without `-o` a name
collision appends `-1`, `-2` and so on before the extension. The mask substitutions
are PROCESSNAME, PID, EXCEPTIONCODE, YYMMDD and HHMMSS.

### Monitoring header and modes

An immediate single dump (count 1 with no condition trigger) prints the banner and
the status lines with no header block. Any other run (count greater than one, or a
CPU, memory, exception, hung or terminate trigger) prints the full monitoring
header (`Process:`, `Process image:`, the thresholds, `Number of dumps:`,
`Dump folder:`, `Dump filename/mask:` and so on) followed by
`Press Ctrl-C to end monitoring without terminating the process.`. The above
threshold operator is `>=`, the below operator (`-cl`, `-ml`) is `<`.

### Trigger line formats

- Timed: `[time] Timed:` before each dump
- CPU: a status line every held second, `[time] CPU: %d%% %lds %s`, with the empty
  reason while building up and `(Trigger)` on the second the hold reaches `-s`
- Commit: `[time] Commit: % 7dMb`, which fires on the first evaluation that meets
  the threshold
- Hung window: `[time] Hung Window:`

### EULA

`-accepteula` or a prior accepted run lets the tool run unattended. The recreation
honours the suite-wide flag under HKLM or HKCU `Software\Sysinternals` and the
per-tool flag under HKCU `Software\Sysinternals\ProcDump`, and on acceptance writes
the per-tool `EulaAccepted` REG_DWORD 1. A first run with neither prints the accept
notice to stderr and exits.

### Exit codes

A completed run returns the number of dumps written (1 for a single dump, 2 for
`-n 2`). A usage error, an unparseable argument, and a target that cannot be found
return 127.

## Validation (vs the original, normalized output)

Each case ran the original and the Release x64 rebuild against the same throwaway
`ping.exe` process, captured stdout and stderr, and diffed after normalizing the
timestamp, the dump path, the dump size and the CPU or commit value. Only a
throwaway process the session spawned was dumped, and every dump file was deleted
afterward.

| Case | Switches | Result |
|------|----------|--------|
| mini | -mm | match |
| full | -ma (with the estimate line) | match |
| triage | -mt | match |
| miniplus | -mp | match |
| custom zero mask | -mc 0 | match |
| custom full mask | -mc 2 | match |
| default folder | -mm, no folder | match |
| timed | -mm -n 2 -s 1 | match |
| cpu above | -mm -c 0 -u -s 2 | match |
| cpu below | -mm -cl 100 -s 2 | match |
| cpu sustained | -mm -c 0 -u -s 3 | match |
| memory above | -mm -m 1 -s 2 | match |
| memory below | -mm -ml 1000000 | match |
| overwrite | -mm -o | match |
| by name | -mm ping | match |
| usage, no args | (none) | byte identical |
| usage, -? | -? | byte identical |
| not found, PID | -mm 999999 | byte identical |
| not found, name | -mm zzzno | byte identical |
| debug build | -mm, -ma | match |

Exit codes match on every case (1, 2 and 127). Both builds were checked. Every
produced dump is a structurally valid minidump (signature `MDMP`, version
`0xA793`, in-bounds stream directory, SystemInfo, ThreadList and ModuleList present)
and opens. The Mini dumps are 307 KB against the original's, the Full dumps 14.7 MB,
each within about 250 bytes of the original.

Triggers validated against the original: immediate, timed, CPU above and below,
and memory commit above and below. Reproduced but not exercised against the
original: the exception trigger (`-e`, which needs a faulting target) and the hung
window trigger (`-h`, which needs a target with a window). The ping test process
has neither.

## Deviations from the original

- **Dump-comment user stream.** The original injects one MINIDUMP user stream
  holding the launching command line (`*** <cmdline>`), which makes each original
  dump carry one extra stream. The recreation omits that stream, so a Mini or Full
  dump has one fewer stream and is about 250 bytes smaller. Both open the same way.
- **Version-adaptive flags fixed to modern dbghelp.** The original recomputes the
  flag words from the running dbghelp version. The recreation uses the exact words
  for a modern dbghelp (major 6), which is every supported host here.
- **MiniPlus memory selection.** The original drives MiniPlus memory inclusion and
  the largest-region exclusion through its write callback. The recreation writes
  MiniPlus with the Mini base flags and no custom memory callback, so the dump is a
  valid MiniPlus-class dump without the exact region pruning.
- **Out-of-scope switches.** The clone, WER, AeDebug install and uninstall, launch,
  performance counter, debug-string logging, native-debugger, filter, kernel dump,
  WOW64 and cancel paths are not implemented. The parser rejects an unrecognized
  switch with usage rather than silently accepting it.
- **Banner source.** The original reads its banner from its version resource. The
  recreation hardcodes the same lines (v11.0, the 2009-2022 copyright).

## Build

VS 2022 Community MSBuild, PlatformToolset v143, `LanguageStandard stdcpp20`,
`CharacterSet Unicode`, SubSystem Console, static CRT (`/MT` Release, `/MTd` Debug).
Libraries advapi32, user32 and psapi are linked, dbghelp is loaded at runtime from
System32 the way the original resolves it. The repo `Directory.Build.targets` adds
the Release hardening and speed flags, so the project does not repeat them. Both x64
configurations build clean (one benign `D9025 /Ob2 overridden by /Ob3` notice from
the shared targets). x64 only, per the task. Not added to any solution, per
instruction.

## Re-validation after the unit split

The behaviour-preserving split was re-checked read-only. Both x64 configurations
(Debug and Release) rebuild clean. The `-?` usage help, run with `-nobanner`, is
byte-identical between the original `procdump64.exe` and the Release rebuild (7806
bytes, `fc /b` reports no differences). A functional smoke test dumped a throwaway
`ping.exe` the session spawned with both binaries and then deleted the dumps and
killed the process: both wrote a structurally valid minidump with the same header
(signature `MDMP`, version `0xA793`), the recreation one stream and about 500 bytes
smaller, which is the documented dump-comment-stream deviation and not a change from
the refactor. No system or third-party process was dumped.

Status: done and validated, refactored into units.
