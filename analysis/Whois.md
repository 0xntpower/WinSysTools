# Whois

Console recreation of the Sysinternals **Whois** utility (v1.21), a WHOIS client
over TCP port 43. Source at `src/Whois/Whois.cpp`, project `src/Whois/Whois.vcxproj`.
Original at `targets/sysinternals/whois64.exe` (x64, PDB `whois64.pdb`).

This is a CLI console app, not a WTL GUI tool, and not a PsTool.

## Engines

IDA idalib was unavailable during this session (all four worker slots held by
other binaries), so the reverse engineering ran on **rea** (Ghidra 12.1.2 backend)
for the clean pseudocode plus local **radare2 6.1.6** for xrefs, the data tables,
and the short helper routines. Snapshot at `analysis/snapshots/whois.snap`.

## What it does

Given a domain name, the tool queries an initial WHOIS server over port 43,
reads the raw response, extracts a referral to the next server from that
response, and repeats until the chain ends. Each query sends `<name>\r\n` and
reads to connection close.

## Key functions

| Address | Role |
|---------|------|
| `0x1400033a0` | `main`: EULA and banner gate, argument parse, domain normalization, server selection, referral loop |
| `0x1400031e0` | connect and query: `gethostbyname`, TCP `connect` to port 43, `send`, `recv` loop into a growing buffer |
| `0x1400031a0` | usage printer, returns 1 |
| `0x140003120` | Winsock error printer, `FormatMessageA` over `WSAGetLastError` to stderr |
| `0x140001050` | banner, scans argv for `-nobanner` or `/nobanner`, prints the version banner to stderr |
| `0x140002220` | EULA check over `HKCU\Software\Sysinternals\Whois`, value `EulaAccepted` |

## Protocol (`0x1400031e0`)

1. Print `Connecting to %s...` to stdout.
2. `gethostbyname(server)`, build `sockaddr_in` with `sin_port = htons(43)`
   (the original uses `lea ecx, [rbp+0x29]` with `rbp = 2`, which is 43).
3. `socket`, `connect`, `send(query)`, then a `recv` loop in 0x1000-byte chunks
   appended to a reallocating buffer that is null terminated.
4. Return success when any data arrived. Any failure prints the Winsock error
   and returns false, which ends the run with exit code 0.

## Server selection and query

- First positional is the domain and must contain a dot, else usage. Second
  positional is an explicit server. A third positional is usage.
- If the domain parses as a dotted quad with `sscanf("%d.%d.%d.%d")`, the tool
  reverse resolves it with `gethostbyaddr` and uses the host name.
- The name is uppercased. The last label is matched against a 7-entry table at
  `0x140078190`: `COM ORG NET BIZ GOV EDU INFO`. For those known gTLDs the input
  is reduced to its registrable two labels, so a host or subdomain is stripped.
  ccTLD and unknown inputs are queried whole.
- Server, when no explicit server is given, is `<TLD>.whois-servers.net`.
- Query is `<name>/e\r\n` when the TLD is `JP` and not a known gTLD, else
  `<name>\r\n`. The `/e` requests English output from the Japanese registry.

## Referral following (`main`)

For each response, in order:

1. If it contains `To single out one record`, re-query the same server with
   `=<name>\r\n` (the Verisign exact-match form).
2. Else if it contains `No match`, print `No whois information found.` and stop.
3. Else search for the next-server marker, in this order: the `Whois Server: `
   that follows a `Domain Name: <name>` line, then `WHOIS Server:`, then
   `domain:`, then `Domain ID:`, then `Registrant:`, and finally the whole
   response as a fallback.

The next server is read as marker position plus a fixed 14 bytes, up to the
newline. That fixed offset is what lands on the value in both the
`Whois Server: ` and the `domain:` with padding layouts. The loop stops when
the referral points back to the current server with a plain-domain query, which
prevents an endless loop.

Output routing: the connect line, the server banner, the response bodies, and
the usage all go to stdout. The startup banner and the Winsock error go to
stderr. In verbose mode each hop prints `Server %s returned the following for %s`
and the full body. In non-verbose mode each hop prints the body from the marker
only, which is why the comment header lines above the referral are dropped.

## Known quirks reproduced

- The `domain:` marker plus the fixed plus-14 offset makes the IANA record for a
  domain like `example.com` resolve the domain name itself as the next server,
  so the final hop connects to the domain and times out. The rebuild reproduces
  this exactly.
- A `.jp` query returns a block of English notice lines, and the same marker
  logic mis-reads one of those lines as the next server. The rebuild reproduces
  the identical garbled next-hop.

## Build

v143, stdcpp20, Console, Unicode, static CRT, links `ws2_32` and `advapi32`.
Release hardening and optimization come from `Directory.Build.targets`, not set
per project. Built from PowerShell with VS 2022 Community MSBuild.

- Debug x64: clean, `src/Whois/x64/Debug/Whois.exe`.
- Release x64: clean apart from the shared `/Ob2` to `/Ob3` override warning that
  every tool in the tree gets, `src/Whois/x64/Release/Whois.exe`.

## Validation

The data source is live and every response carries volatile fields such as query
timestamps, and the final hops depend on live network reachability, so a full
byte diff is not the goal. Validation is **structural plus referral-chain
parity**, run against `whois64.exe` on the same inputs. No registrant data is
recorded here.

Byte-identical, including exit codes:

- No arguments: banner on stderr and usage on stdout both identical.
- `/?` usage identical on both streams.
- `-nobanner /?`: banner suppressed, usage identical.
- Too many positionals: usage identical.
- Single-label domain with no dot: usage identical.

Referral chain parity on the stable query `example.com` under `-v`:

```
Connecting to COM.whois-servers.net...
Server COM.whois-servers.net returned the following for EXAMPLE.COM
Connecting to whois.iana.org...
Server whois.iana.org returned the following for EXAMPLE.COM
Connecting to EXAMPLE.COM...
```

The rebuild produced the identical sequence of servers at the identical line
positions, and the identical Winsock timeout message on stderr for the final
hop.

Body printing parity on `example.com`, both verbose and non-verbose: the full
outputs matched the original byte for byte except for the single registry
`Last update of whois database` timestamp line, which advances a few seconds
between the two runs. Sizes matched (3873 bytes verbose, 3530 non-verbose).

`.jp` path parity: `-v example.jp` built the `EXAMPLE.JP/e` query and produced
the identical chain and the identical mis-parsed next hop, confirming the `/e`
branch and the referral quirk bug for bug.

## Deliberate improvements

The original uses fixed 112-byte name and server buffers. The rebuild uses
`std::string` throughout, so an over-long input keeps full fidelity instead of
truncating. The referral extraction keeps the original's fixed plus-14 offset so
chain behavior matches.
