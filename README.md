# Network Compression Detection Tool

End-to-end detector for **intermediary network compression**, based on the technique in:

> Vahab Pournaghshband, Alexander Afanasyev, and Peter Reiher.
> *End-to-End Detection of Compression of Traffic Flows by Intermediaries.*
> [[PDF](https://www.cs.usfca.edu/vahab/resources/compression_detection.pdf)]

Middleboxes sometimes compress traffic without telling end hosts. This tool probes a path with two UDP packet trains — **low-entropy** (highly compressible) and **high-entropy** (incompressible) — and compares train durations. If the high-entropy train takes significantly longer, compression is likely present on the path.

```
ΔtH − ΔtL > τ   →   compression detected
```

Default threshold τ is **100 ms**, matching the paper’s guidance for typical OS timer resolution.

## Features

| Mode | Binary | Description |
|------|--------|-------------|
| **Cooperative** | `ncd-client` / `ncd-server` | Client and server cooperate over TCP; server measures UDP train arrival times |
| **Standalone** | `ncd-probe` | Single-host probe using raw TCP SYN markers + libpcap RST timing (responsive-host style) |

Shared library code handles JSON config, payload generation (`/dev/urandom`), timing math, and CLI flags.

## Architecture

```
Cooperative:
  Client ──TCP config──► Server
  Client ──UDP low train──► Server  (times first→last arrival)
  Client ──UDP high train─► Server
  Server ──TCP results──► Client  →  verdict

Standalone:
  Probe ──SYN──► target  (RST captured via pcap)
  Probe ──UDP low train──► target
  Probe ──SYN──► target
  … same for high-entropy …
  Probe compares RST-pair deltas → verdict
```

See [docs/architecture.md](docs/architecture.md) and [docs/paper-notes.md](docs/paper-notes.md) for details and paper mapping.

## Requirements

- **C11 compiler** (`gcc` or `clang`)
- **POSIX sockets**
- **Linux recommended** for the standalone probe (raw sockets, `SO_BINDTODEVICE`)
- **libpcap** (+ `libpcap-dev`) for `ncd-probe`
- **Root / `CAP_NET_RAW`** for `ncd-probe`

Cooperative client/server build and run on macOS and Linux.

### Install libpcap (for standalone)

```bash
# Debian/Ubuntu
sudo apt-get install libpcap-dev

# Fedora
sudo dnf install libpcap-devel

# macOS
brew install libpcap
```

## Build

```bash
make          # ncd-client + ncd-server
make full     # also builds ncd-probe (needs libpcap)
make test     # unit tests
make clean
```

Binaries land in `bin/`.

## Quick start (cooperative)

On the **server** host:

```bash
./bin/ncd-server --config configs/example.json
```

On the **client** host (edit `serverIp` in the config first):

```bash
cp configs/example.json configs/my-run.json
# set "serverIp" to the server's address
./bin/ncd-client --config configs/my-run.json --verbose
```

Example output:

```
Low-entropy train:  45 ms
High-entropy train: 52 ms
Delta (high - low): 7 ms
Threshold:          100 ms
Result: No compression detected.
```

Machine-readable output:

```bash
./bin/ncd-client --config configs/my-run.json --json
```

Multiple trials:

```bash
./bin/ncd-client --config configs/my-run.json --trials 5
```

## Standalone probe

```bash
sudo ./bin/ncd-probe --config configs/example.json --interface eth0 --verbose
```

The target must respond to TCP SYNs with RSTs on the configured head/tail ports (typical for closed ports).

## Configuration

See [`configs/example.json`](configs/example.json). Important fields:

| Key | Default | Meaning |
|-----|---------|---------|
| `serverIp` | — | Target / server address |
| `UdpPayloadSize` | 1100 | Probe payload size (paper uses large packets) |
| `UdpPackCount` | 6000 | Packets per train |
| `InterPacketUs` | 100 | Inter-packet gap in microseconds |
| `InterTime` | 15 | Seconds between low and high trains |
| `ThresholdMs` | 100 | Detection threshold τ |
| `TcpPortNum` | 8100 | Cooperative control-plane port |
| `destPortUdp` | 8765 | UDP train destination port |
| `destPortTcpHead` / `Tail` | 8000 / 8080 | Standalone SYN marker ports |
| `Interface` | auto | NIC for standalone mode |
| `Trials` | 1 | Repeated measurements |
| `DataThreshold` | 0.0 | Start timing after this fraction of the train (0 = first packet, paper-style) |

## Project layout

```
common/           Shared config, timing, entropy, UDP train helpers, cJSON
cooperative/      ncd-client, ncd-server
standalone/       ncd-probe (raw sockets + pcap)
configs/          Example JSON configs
docs/             Architecture + paper mapping
scripts/demo.sh   Build + smoke checks
tests/            Unit tests
```

## Limitations

- Detects **entropy-based** link/IP compression, not TCP/IP header compression or pure dictionary RE.
- Sensitive to severe cross-traffic and jitter; use `--trials` for more confidence.
- Standalone mode is **Linux-oriented** and needs privileges.
- A positive result means “timing consistent with compression,” not a cryptographic proof.

## Credits

- Detection methodology: Pournaghshband, Afanasyev, Reiher
- Raw socket examples: [P.D. Buchan](https://www.pdbuchan.com/rawsock/rawsock.html)
- libpcap patterns: [DevDungeon](https://www.devdungeon.com/content/using-libpcap-c)
- JSON parsing: [cJSON](https://github.com/DaveGamble/cJSON)

## License

MIT — see [LICENSE](LICENSE).
