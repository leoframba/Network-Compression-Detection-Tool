# Paper mapping

Source paper:

> Vahab Pournaghshband, Alexander Afanasyev, Peter Reiher.
> *End-to-End Detection of Compression of Traffic Flows by Intermediaries.*
> https://www.cs.usfca.edu/vahab/resources/compression_detection.pdf

## Core idea (Section III)

| Paper | This repo |
|-------|-----------|
| Send low-entropy then high-entropy fixed-size trains | `fill_low_entropy` / `fill_high_entropy` + `send_udp_packet_train` |
| Compare `ΔtH − ΔtL` to threshold `τ` | `compression_detected()` in `common/timing.c` |
| No clock sync required (relative delays only) | Server uses local monotonic clock; standalone uses pcap timestamps |

## Implementation choices (Section IV)

| Paper parameter | Paper value | Repo default | Config key |
|-----------------|-------------|--------------|------------|
| Low-entropy payload | all zeros | all zeros | — |
| High-entropy payload | `/dev/random` | `/dev/urandom` (fallback PRNG) | — |
| Packet size | ~1100 bytes | 1100 | `UdpPayloadSize` |
| Inter-packet spacing | 100 µs | 100 µs | `InterPacketUs` |
| Packets per train | 6000 | 6000 | `UdpPackCount` |
| Threshold `τ` | ~100 ms | 100 ms | `ThresholdMs` |
| Transport | UDP trains | UDP trains | — |

## Cooperative vs responsive (Section IV)

| Paper | Repo |
|-------|------|
| Cooperative: TCP before/after train to exchange params and timings | `ncd-client` + `ncd-server` |
| Responsive: ICMP echo at head/tail; sender times replies | `ncd-probe` uses **TCP SYN → RST** markers + libpcap instead of ICMP |

The SYN/RST approach is documented as an intentional alternative: many hosts answer closed-port SYNs with RST without requiring ICMP echo permission or a custom agent.

## What we do not claim

- Detection of header compression / ROHC / dictionary-only RE (paper also excludes non-entropy compression).
- Bandwidth estimation accuracy (paper notes detection does not require precise capacity estimates).
- Production readiness on arbitrary Internet paths without multi-trial validation.

## Suggested experiments

1. Lab path **without** compression → expect delta ≪ 100 ms.
2. Path through a compressing tunnel / link-layer compressor → expect delta > τ.
3. Repeat with `--trials 24` to mimic the paper’s diurnal measurement idea at smaller scale.
