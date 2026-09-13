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

## Remaining performance evidence

- representative real RGB input;
- representative zlib and LZ4 inputs from an independent producer;
- cold-cache I/O separation from decode cost;
- repeatable CI or dedicated-host thresholds before any SLA is stated.

