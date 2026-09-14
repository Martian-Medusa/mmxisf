# M7 local row-delivery memory checkpoint

- Date: 2026-09-14
- Build: CMake Release, arm64, warnings-as-errors
- Host: Apple M2 (8 cores), 16 GB RAM, macOS 26.5.2
- Scope: one warm-cache synthetic comparison; not a portable SLA

## Profile

The existing writer benchmark produced one 6064x4040 Planar UInt8 RGB image:
73,495,680 decoded bytes, Zstandard level 3, byte shuffle, SHA-256, and five
declared compression subblocks. The deterministic file was 49,373,466 bytes
with SHA-256
`6897d239eca0042cc98c71e92be597344148cd2083a6316a711e2d3cba967c33`.

`mmxisf-inspect --decode-sha256` reported the established complete decoded
pixel SHA-256
`9539b574af18d10ad4c942ffd84a3235647b874f18c54621af338450e109592f`.
The new `--decode-rows` path delivered 12,120 planar rows and its incremental
callback-order SHA-256 matched that value exactly.

## Observation

Both commands were measured as separate processes with macOS
`/usr/bin/time -l` against the same warm local file:

| Path | Real time | Maximum RSS |
| --- | ---: | ---: |
| owning `--decode` | 0.15 s | 144,031,744 bytes |
| callback `--decode-rows` with incremental SHA-256 | 0.20 s | 64,028,672 bytes |

The row path reduced this maximum-RSS observation by 80,003,072 bytes
(76.30 MiB, 55.55%) while verifying the declared checksum before callbacks and
again over the delivery pass. Its maximum RSS remained below the decoded-frame
size even though byte-shuffled decompression uses independently bounded
compressed, decoded, shuffle, hash, and row buffers.

## Gate interpretation

This is evidence that the callback implementation avoids retaining a complete
decoded frame on a representative PFI-sized compressed image. It is not a
throughput target, a cold-storage measurement, or a cross-platform memory
claim. Dedicated non-macOS measurements and native PFI integration remain
separate acceptance gates.
