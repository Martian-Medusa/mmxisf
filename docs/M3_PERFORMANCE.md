# M3 local decode performance checkpoint

- Date: 2026-09-13
- Commit: `1a9acf6`
- Build: CMake Release, arm64
- Host: Apple M2 (8 cores), 16 GB RAM, macOS 26.5.2
- Scope: local, warm filesystem cache; not a published performance claim

## Method

Each row is five separate invocations of `mmxisf-inspect --decode` with output
discarded. Elapsed time includes process startup, bounded header parsing,
metadata traversal, and image decoding. Peak resident memory is the macOS
`RUSAGE_CHILDREN` high-water mark. Fixtures come from the private local
PixInsight corpus and are not redistributed.

| Fixture profile | Serialized bytes | Decoded bytes | Median elapsed | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| One 6248x4176 Float32 Gray uncompressed attachment | 104,399,360 | 104,366,592 | 34.9 ms | 106,741,760 bytes |
| Three 6248x4176 Float32 Gray uncompressed attachments, decoded sequentially | 313,143,808 | 313,099,776 total | 75.5 ms | 106,889,216 bytes |

Fixture SHA-256 identities are, respectively,
`174a54836647ac1f0d56fb1d310ca1510b8fcfdbc717c8832d7cf216ff303c89` and
`3f07c4ed6dbe442d421a940399f31a6ccaa59c6161edf577271553378e477a57`.

The three-image case retains approximately one decoded 100 MiB plane at a
time; peak RSS does not grow with the aggregate decoded size in this sequential
CLI path. This is evidence for the bounded lifetime of per-image buffers, not a
general memory ceiling for callers that retain multiple returned images.

## Current PixInsight/Zstandard compatibility checkpoint

The following rows are single warm-cache invocations of
`mmxisf-inspect --decode-sha256`, not five-run medians or performance targets.
They exercise files produced or distributed by current PixInsight versions and
record exact decoded-pixel anchors for later differential comparison. All files
are private local fixtures and remain outside the repository.

| Fixture profile | Serialized bytes | Decoded bytes | Elapsed | Maximum RSS | Decoded pixel SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| 6064x4040 UInt8 RGB, embedded Zstandard block | 8,886,153 | 73,495,680 | 0.01 s | 75,890,688 bytes | `2e6d466e61a80051be89459e7d84a3eacf0a4a6d504c4d3871ac884be5d39a54` |
| 8192x8192 UInt16 Gray, attached Zstandard block | 5,869,539 | 134,217,728 | 0.12 s | 276,545,536 bytes | `5941611e3d86462924c7451065a0296b05674f42469204804838a91f889f6875` |
| 3699x5841 Float64 RGB, attached block | 518,910,448 | 518,540,616 | 0.41 s | 520,962,048 bytes | `64a8e4a6c9fc8907b236900bf9edb61a303abaf34973a6de50fe115a1c7d9807` |
| 6217x4150 Float32 RGB, attached block | 309,997,280 | 309,606,600 | 0.21 s | 311,738,368 bytes | `1312421d1a8497245896b43a717a1d0b5a8f532f03bd6da774574c248f7b3196` |

These measurements demonstrate successful decode and provide local regression
anchors. They do not establish cold-cache behavior, cross-platform performance,
or equivalence with PixInsight's decoded sample buffer. The unusually compact
8192x8192 file also demonstrates why the independent absolute output limit and
bounded Zstandard window remain necessary even when a valid input has a high
decompression ratio.

## Remaining performance evidence

- representative zlib and LZ4 inputs from an independent producer;
- cold-cache I/O separation from decode cost;
- repeatable current RGB/Zstandard measurements;
- CI or dedicated-host thresholds before any SLA is stated.
