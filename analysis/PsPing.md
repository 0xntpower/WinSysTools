# PsPing (Sysinternals psping64.exe v2.12)

Source-accurate C++20 console reconstruction. Reproduces the two ping engines the
tool is built around, ICMP ping and TCP connect ping, with their per-ping lines,
the summary statistics, and the latency histogram. Uses the shared PsTools front
end for the banner and the EULA gate.

- Original: `targets/sysinternals/psping64.exe` (x64, MSVC, static CRT, console)
- Rebuild: `src/PsPing/PsPing.cpp` plus `src/common/PsTools.h`, project
  `src/PsPing/PsPing.vcxproj` (console, v143, stdcpp20, Unicode, static CRT)
- Engine: rea (Ghidra backend) drove the decompilation this session because all
  four idalib workers stayed busy with other tools for the whole run. The decompiles
  were cross-checked against the live binary output and the recovered format strings,
  and the result validates byte for byte on the ping paths.

## What it does

```
ICMP:    psping [-t|-n count[s]] [-i interval] [-w count] [-q] [-h [buckets|list]] [-l size[k]] [-6|-4] destination
TCP:     psping [-t|-n count[s]] [-i interval] [-w count] [-q] [-h [buckets|list]] [-6|-4] destination:port
latency: psping -l size[k|m] -n count[s] [-r] [-u] [-w count] [-f] [-h ...] [-6|-4] destination:port
bw:      psping -b -l size[k|m] -n count[s] [-r] [-u [target]] [-i io] [-w count] [-f] [-h ...] destination:port
server:  psping [-6|-4] [-f] -s address:port
```

A destination with no port runs ICMP ping. A `destination:port` with no request
size runs TCP connect ping. A request size together with a port selects a latency
or bandwidth test, and `-s` runs the server. `-accepteula` and `-nobanner` are the
standard Sysinternals switches. `-? [i|t|l|b]` prints the per-mode usage.

This reconstruction implements the ICMP and TCP ping paths in full. The latency,
bandwidth, UDP, and server engines are out of scope and are covered in the
departures section below.

## Reverse engineering findings

### Function map

| Address | Role |
|---------|------|
| `sub_14000AB70` | `main`: WSAStartup, stdin/stdout file-type probe, Ctrl handler, mode switch |
| `sub_1400078F0` | front end (version info, banner, `-nobanner` and `-accepteula` strip) |
| `sub_140009B60` | argument parser and mode resolution |
| `sub_1400096E0` | usage printer (mode selects ICMP, TCP, latency, or bandwidth help) |
| `sub_140004E60` | ICMP ping loop (`IcmpSendEcho` / `Icmp6SendEcho2`, QPC timing) |
| `sub_1400059B0` | TCP connect loop (`socket` / `bind` / async connect / `getsockname`) |
| `sub_1400066D0` | latency and bandwidth worker (modes 2 through 6) |
| `sub_1400043D0` / `sub_140004420` / `sub_1400044E0` | stats init, add one sample, print |
| `sub_140004B30` | histogram bucket builder |
| `sub_140004C90` / `sub_140004DA0` | histogram printer and the print-if-not-server wrapper |
| `sub_140008040` | parse a `host` or `host:port` endpoint |
| `sub_140008930` | print a labeled error (prefix plus error message) |

### Argument grammar (`sub_140009B60`)

Each switch dispatches on `toupper(letter) - '4'`. The recovered mapping is `-4`,
`-6`, `-?`, `-b`, `-f`, `-h`, `-i`, `-l`, `-n`, `-q`, `-r`, `-s`, `-t`, `-u`, `-w`.
A token that is not a switch is the destination, parsed by `sub_140008040`. `-u`,
`-i`, `-l`, `-n`, `-s`, and `-w` consume the following argument. `-h` consumes the
next argument only when it starts with a digit, otherwise it keeps the default
bucket count.

Numeric suffixes recovered from the parser:

- `-l size` takes `k` for kilobytes and `m` for megabytes, with an overflow guard.
- `-n count` takes `s`, `m`, or `h` to switch from a ping count to a duration in
  seconds, minutes, or hours.

Defaults recovered from the `.data` initializers: count 4, warmup unset (resolved
to 1 for ping and 5 for latency and bandwidth), interval unset (resolved to one
second), ICMP request size 32 with a 64000 cap, and the ICMP send timeout 5000 ms.
`-t` sets the count to infinite. `-i 0` selects the fast one-millisecond interval,
and any other interval is read in seconds and stored in milliseconds.

### Mode resolution (`sub_140009B60` tail)

The parser resolves the mode from the destination and the request size. A port
with no request size gives TCP ping. No port and no server gives ICMP ping. A
request size with a port gives latency when `-b` is absent and bandwidth when it
is present, with `-r` choosing the receive direction. `-s` with no destination
gives the server. The ICMP branch defaults the size to 32 and rejects a size over
64000 with `Request size must be < 64000`.

### ICMP ping loop (`sub_140004E60`)

The handle comes from `IcmpCreateFile` for IPv4 or `Icmp6CreateFile` for IPv6. The
loop waits on three objects, a stop event, a Ctrl-Break event, and a periodic
waitable timer set to the interval. A stop ends the loop, a break prints interim
statistics, and a timer tick sends one echo. The round trip is measured with
`QueryPerformanceCounter` around the send and scaled to milliseconds in single
precision, which is why the per-ping value carries sub-millisecond resolution that
the `IcmpSendEcho` round-trip field cannot give. Each tick increments the iteration
counter, and only ticks past the warmup count feed the statistics and the
histogram. The header line counts the warmup, so `-n 4` prints `5 iterations
(warmup 1)` and five reply lines while counting four.

Lines, exact:

```
Pinging %s with %d bytes of data:
%d iterations (warmup %d) ping test:
Reply from %s: %.2fms
Error pinging %s:
Request timed out.
```

### TCP connect loop (`sub_1400059B0`)

Each tick creates a socket, sets it non-blocking with `FIONBIO`, binds the source
endpoint, and connects with a timeout. On success `getsockname` reads the local
address and port for the line. The warmup iterations print a `(warmup)` marker and
are not counted. The per-ping line is two writes, the `Connecting to` prefix and
the `from` suffix.

Lines, exact:

```
TCP connect to %s:
Connecting to %s (warmup): from %s: %.2fms
Connecting to %s: from %s: %.2fms
```

### Statistics (`sub_1400044E0`)

The stats block is five 32-bit fields, sent, received, minimum, maximum, and sum.
The average is the sum over the received count, and the minimum, maximum, and sum
track received replies. The printed loss percentage is `(sent - received) * 100 / sent`.

```
  Sent = %d, Received = %d, Lost = %d (%d%% loss),
  Minimum = %.2fms, Maximum = %.2fms, Average = %.2fms
```

### Histogram (`sub_140004B30` and `sub_140004C90`)

A run with fewer than two samples prints `Not enough data for histogram`. Otherwise
the header `Latency\tCount` prints, then one row per bucket. The auto builder sets
the width to `(max - min) / (buckets - 1)`, places bucket `i` at `min + i * width`,
and counts each sample into bucket `(int)((sample - min) / width)`. The default
bucket count is 20, and `-h N` overrides it. A comma list such as `-h 0.01,0.05,1`
gives explicit upper bounds, and a trailing overflow bucket prints as `#INF`. The
first bucket label equals the minimum and the last equals the maximum.

## Reuse of the shared front end

`src/common/PsTools.h` is reused read only for the banner, the `-nobanner` and
`-accepteula` strip, and the EULA gate. PsPing carries its own argument grammar, so
the parser here is local and mirrors `sub_140009B60` rather than
`pstools::ParseCommandLine`. The error printer `pstools::PrintSystemError` backs the
resolver and ICMP error paths.

Implemented locally because they are not in the shared header:

- The ICMP and TCP ping loops, the statistics block, and the histogram.
- Endpoint parsing and resolution over `getaddrinfo`, and the numeric address
  formatting for the reply and connect lines.
- The waitable-timer pacing and the stop and break events.

## Deliberate departures and scope

- The latency and bandwidth engines (modes 2 through 5), the `-s` server, and UDP
  are out of scope. The task calls for the ICMP and TCP ping paths, their
  statistics, and the histogram, which are reproduced in full. The parser still
  resolves those other modes and the dispatcher reports a one-line notice for them
  on stderr rather than running a partial engine.
- Quiet mode (`-q`) suppresses the per-ping lines and shows a simple percentage
  meter. The original draws a wider scale decoration that is built character by
  character rather than from a single format string. The meter wording here is
  modelled and is not byte-for-byte, and this path is outside the validated set.
- The malformed-address error `...` is reported by the original as
  `The parameter is incorrect` because its own parser rejects the token before
  resolution, while this build resolves the token and reports `Host not found`. The
  common unknown-host case matches exactly.
- The interactive EULA dialog is replaced by the shared console gate, the same
  choice already made across the suite.

## Build

```
MSBuild src\PsPing\PsPing.vcxproj -p:Configuration=Debug   -p:Platform=x64 -m
MSBuild src\PsPing\PsPing.vcxproj -p:Configuration=Release -p:Platform=x64 -m
```

Both configurations build clean. The only compiler note is the shared `/Ob2`
versus `/Ob3` D9025 from `Directory.Build.targets`, common to every tool in the
tree. The project links `ws2_32.lib` and `iphlpapi.lib` as the original does, plus
`advapi32.lib` for the shared EULA registry gate.

## Validation

All runs on the local loopback only, original versus rebuild, stdout compared on the
same inputs after normalizing the volatile round-trip times and the ephemeral local
TCP port. The Release build was used for the diffs, and the Debug build was smoke
tested.

| Case | Result |
|------|--------|
| ICMP `-n 4 127.0.0.1` | framing byte-identical, Sent 4, Received 4, Lost 0, exit 0 |
| ICMP default count `127.0.0.1` | `5 iterations (warmup 1)`, Sent 4, matches |
| TCP `-n 4 127.0.0.1:<port>` (self-opened listener) | framing byte-identical, Sent 4, Received 4, Lost 0, exit 0 |
| ICMP `-n 6 -h 5` | header, five bucket rows, first label is the minimum and last is the maximum, counts sum to the sample count |
| custom `-h 0.1,0.5,1` | explicit bounds plus the `#INF` overflow row, structure matches |
| `-?`, `-? i`, `-? t`, `-? l`, `-? b` | each byte-identical |
| no-argument general help | byte-identical |
| banner (stderr) | byte-identical |
| line endings | CRLF, matches |
| unknown host `badhost.invalid` | stderr `Invalid destination address` plus `Host not found`, exit 127, matches |
| no destination, bad switch | usage, exit 127, matches |

The round-trip times and the ephemeral local port are the only differences on the
ping paths, and both are inherently per-run. No external host was contacted, the TCP
listener was a short-lived loopback listener opened for the test, and every process
and listener launched for validation was stopped afterward.
