# Coreinfo -- reverse-engineering notes

Target: `targets/sysinternals/Coreinfo64.exe` -- Sysinternals Coreinfo v3.6, x64 PE,
**Unicode**, not packed. md5 `3ae27d0e66846c2482275d845a469d9b`, sha256
`9c233b79795ef34595bc149f9c5ebcf0616c43af1fa6e3c7fb94848a49f7c05e`. Image base
`0x140000000`, 662 functions. Engine -- IDA idalib session `c38d459e`, Hex-Rays primary.
This is a console tool, so the WTL, toolbar and light-mode conventions from the guide do
not apply.

Rebuild lives in `src/Coreinfo/`, a C++20 x64 console vcxproj (Application, SubSystem
Console, v143, stdcpp20, Unicode, static CRT). The repo `Directory.Build.targets` applies
the Release hardening and speed flags.

## What it does

Coreinfo reads the CPU feature set through the `CPUID` instruction and the processor
topology through `GetLogicalProcessorInformationEx`, then renders one map of OS-visible
processors per resource with `*` for the applicable processors and `-` otherwise.

## Entry flow (`main` at 0x140005D90)

1. `sub_140002B00` pulls version-info strings for the banner and the app name.
2. `sub_140002660` prints the banner unless `-nobanner`, stripping that switch in place.
3. `sub_1400021C0` handles the EULA, stripping `-accepteula` in place. Returns 0 on decline.
4. `sub_140003020` parses the resource switches into a selection bitmask.
5. `GetLogicalProcessorInformationEx` is resolved. When present, `sub_140004980` runs and
   dispatches to the feature and topology dumps. The legacy fallback is `sub_1400054D0`.

## Switch bits (`sub_140003020`)

Each switch letter sets one bit in the mask. `-c` 0x01, `-n` 0x02, `-s` 0x04, `-l` 0x08,
`-g` 0x10, `-m` 0x20, `-f` 0x40, `-v` 0x80, `-d` 0x100. With no switches the mask becomes
`0xFFFFFEFF`, which is everything except `-d`. `-accepteula` and the undocumented `run32`
are ignored by the letter parser. Any unknown letter prints the usage block and returns 0.

The dispatch in `sub_140004980` is the key detail. It picks the feature dump when the mask
has bit 0x40, otherwise the virtualization dump when it has bit 0x80, as a single
if / else-if. Because the default mask has **both** `-f` and `-v` set, the else-if makes
`-f` win, so the default run shows the full feature dump and never the `-v`-only section.
`-v` appears only when `-f` is absent.

## EULA (`sub_1400021C0`, `sub_140002480`, `sub_140001C70`)

Acceptance is the REG_DWORD `EulaAccepted` under `HKCU\Software\Sysinternals\Coreinfo`. A
nonzero value means accepted. `-accepteula` sets it and runs headless. The rebuild
reproduces the key, the value and the headless path. It does not rebuild the RichEdit
license dialog, since this is a console recreation. Confirmed the rebuild runs headless off
the stored flag with no switch.

## CPU header (`sub_140003460`)

The brand and identity lines come from the **registry**, not from CPUID brand leaves.

- `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0` value `ProcessorNameString` -> the
  brand line (trailing spaces from the value are kept).
- `HKLM\System\CurrentControlSet\Control\Session Manager\Environment` value
  `processor_identifier` -> the family and stepping line.
- Microcode comes from `Update Revision` or `Update Signature` under the CentralProcessor
  key. A 4-byte value is used directly, an 8-byte value uses its high dword. Printed as
  `Microcode signature: %08X`.

## Feature table (the core)

Features are an array of 2580-byte records at `0x14003B870` (103 records), walked by
`sub_1400037F0`. Each record holds `{ int Leaf, int Reg, int Bit, wchar Name[260] at +0x0C,
wchar Desc at +0x214 }`. A record with `Bit == -1` is a separator that emits a blank line.
Records have no direct xrefs because the walk indexes by `2580 * index`, which is why a
plain string scan finds the names but IDA shows no references.

The walk selects the fetched CPUID register array by leaf (1, 7, 0x80000001, 0x80000007),
indexes the register by `Reg` (0 EAX, 1 EBX, 2 ECX, 3 EDX) and tests `Bit`. The table was
extracted verbatim into `src/Coreinfo/FeatureTable.h` by a reader that walks the records in
the raw image, so the names, descriptions, leaves, registers, bits and separator positions
match the original exactly. The `-v` tables are `0x140039A30` (AMD, 3 records) and
`0x14007C680` (Intel, 4 records), extracted the same way.

Leaves fetched by `sub_1400037F0`: 0, 1, 7 (subleaf 0), 0x80000000, 0x80000001, 0x80000007
and 0x80000008 (low 16 bits give the address widths). The thread is pinned with
`SetThreadAffinityMask(thread, 1)` around the reads and restored afterward.

### Features outside the table

- **HTT** is vendor specific. With leaf 1 EDX bit 28 set, Intel marks it when leaf 1 EBX
  bits 16..23 are nonzero and labels it `Hyperthreading enabled`. AMD marks it when leaf
  0x80000001 ECX bit 1 is set and labels it `Multicore`.
- **CET / Kernel CET / User CET** come from `NtQuerySystemInformation` class 221. Bit 0 is
  support, bit 1 user, bit 8 kernel. When the call fails the original opens its helper
  driver, which needs admin, and otherwise reports that it requires admin rights. On this
  machine the syscall succeeds and all three read `-`.
- **PREFETCHW** is appended by `sub_1400036D0` after the table. The original runs a
  self-modifying probe around the `prefetchw` instruction. The rebuild reads the equivalent
  leaf 0x80000001 ECX bit 8 (3DNowPrefetch), which gives the same result, and marks that as
  a deliberate change from the runtime probe.

### Tail lines

`Maximum implemented CPUID leaves` uses leaf 0 EAX and leaf 0x80000000 EAX. `Maximum
implemented address width` uses leaf 0x80000008 EAX, bits 8..15 virtual, bits 0..7 physical.
`Processor signature` is leaf 1 EAX.

## Topology maps (`sub_140004980`, printer `sub_1400032B0`)

Vendor is read from leaf 0 (`GenuineIntel` -> 1, `AuthenticAMD` -> 0, else 3). The buffer
from `GetLogicalProcessorInformationEx(RelationAll)` is walked once per section. The group
relationship record supplies the active group count and each group's active processor count
and mask. The affinity printer walks every active group, picks the matching `GROUP_AFFINITY`
mask or falls back to the whole group's mask, and emits one character per active processor.

Section order and bits match the original: cores 0x01, sockets 0x04, NUMA 0x02, NUMA cost
0x20, caches 0x08, groups 0x10, distance 0x100. On a single-group machine the printer emits
the map first with no newline, then the two-space label, which is the layout the original
produces when the active group count is 1.

Cache lines use the type table at `0x140033508` -- index 0 Unified, 1 Instruction, 2 Data,
3 Trace, 4 Unknown -- with a per-type running counter. Sizes render in KB below 1 MB and MB
at or above, format `%-17s %3d, Level %d, %4d%s, Assoc %3d, LineSize %3d`.

### Driver-backed paths not reproduced

`-d` distance, the Intel `-v` SLAT bits, the multi-node NUMA cost matrix and the CET
fallback rely on the embedded `COREINFO100.SYS` helper driver loaded through
`sub_140002DC0`, which needs admin. The rebuild keeps the degraded no-driver behavior these
paths show without admin. AMD `-v` (including NP via leaf 0x8000000A EDX bit 0) and the
single-node `No NUMA nodes.` path are reproduced in full, and both are validation targets
below. This machine is AMD with one NUMA node, so no reproduced output depends on the driver.

## Output encoding

`sub_140001590` is a wide printf to a narrow stream opened in the default translated mode.
Redirected output is narrow ANSI with CRLF line endings (verified `0d 0a` at the start of a
capture). The rebuild writes narrow `printf` in the default translated mode, which gives the
same CRLF bytes.

## Validation

Original and rebuild run with the same switches and `-nobanner`, stdout diffed. The CPU is
an AMD Ryzen 7 3700X (8 cores, 16 threads, 1 socket, 1 NUMA node, 1 group), so the output is
deterministic across runs. No field needed normalizing.

| Switch | Result |
|--------|--------|
| default | byte-identical |
| `-f` | byte-identical |
| `-c` | byte-identical |
| `-l` | byte-identical |
| `-n` | byte-identical |
| `-g` | byte-identical |
| `-s` | byte-identical |
| `-m` | byte-identical (`No NUMA nodes.`) |
| `-v` | byte-identical (AMD HYPERVISOR / SVM / NP) |
| `-?` usage | byte-identical |
| banner (no `-nobanner`) | byte-identical |

Build: VS 2022 Community MSBuild v143, `/p:Platform=x64`, Debug and Release both clean. The
only warning is the shared `Directory.Build.targets` `/Ob3` override, same as the other
tools.

### Deviations

- PREFETCHW uses the CPUID bit rather than the original's execute-and-compare probe. Same
  result on this part, and on any part where the CPUID bit agrees with hardware support.
- The RichEdit EULA dialog is not rebuilt. The registry flag, `-accepteula` and the headless
  path are reproduced, which is what matters for unattended runs.
- The `COREINFO100.SYS` helper driver is not bundled, so the driver-only paths degrade the
  same way the original does without admin rather than installing a driver.
