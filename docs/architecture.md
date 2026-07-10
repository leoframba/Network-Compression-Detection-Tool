# Architecture

## Overview

This project implements end-to-end detection of intermediary compression using active probing. Two UDP trains of equal uncompressed size are sent back-to-back (with a configurable intermission): one filled with zeros (low entropy), one filled with random bytes (high entropy). Compression on the path shrinks the low-entropy train more, so it tends to arrive with a shorter duration. If

```
duration(high) − duration(low) > threshold
```

the tool reports that compression was detected.

## Cooperative mode

```
┌────────────┐         TCP (config)          ┌────────────┐
│ ncd-client │ ─────────────────────────────► │ ncd-server │
│            │         UDP trains             │            │
│            │ ════ low / high entropy ═════► │  measures  │
│            │         TCP (results)          │  arrivals  │
│            │ ◄───────────────────────────── │            │
└────────────┘                                └────────────┘
```

1. **Pre-probing:** Client opens TCP to `TcpPortNum`, sends the JSON config, disconnects.
2. **Probing:** Client sends low-entropy UDP train, waits `InterTime`, sends high-entropy train.
3. **Post-probing:** Server accepts a new TCP connection and returns `[low_ms, high_ms, error_flag]`.
4. **Verdict:** Client applies the threshold test (and optional multi-trial majority).

The server times from the first received packet to the last (paper-style). Optional `DataThreshold` (e.g. `0.95`) can ignore early packets if desired.

## Standalone mode

```
┌────────────┐   SYN (head) + UDP train + SYN (tail)   ┌──────────┐
│ ncd-probe  │ ───────────────────────────────────────► │  target  │
│            │ ◄──────── RST replies (pcap) ─────────── │          │
└────────────┘                                          └──────────┘
```

No cooperating agent is required on the far end. The probe:

1. Crafts raw IPv4/TCP SYN packets (needs root).
2. Places SYN markers at the head and tail of each UDP train.
3. Captures RST responses with libpcap.
4. Uses the two RST-pair intervals as proxies for train duration.

This is a **responsive-host** design in the spirit of the paper’s non-cooperative approach (the paper uses ICMP echo; this tool uses SYN/RST).

## Shared modules (`common/`)

| Module | Role |
|--------|------|
| `config.c` | JSON load/validate via cJSON |
| `entropy.c` | Zero fill + `/dev/urandom` payloads |
| `timing.c` | Deltas, threshold test, human/JSON output |
| `udp_train.c` | Sequenced UDP train sender |
| `cli.c` | Shared `--config` / `--threshold` / `--json` flags |
| `vendor/cJSON` | Single vendored JSON library |

## Trust boundaries

- Cooperative mode trusts the remote server’s timing reports.
- Standalone mode trusts local pcap timestamps and that RSTs correspond to the probe’s SYNs (BPF filter on source host).
- Neither mode inspects intermediary state; detection is purely end-to-end timing.
