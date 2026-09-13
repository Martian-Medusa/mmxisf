# M6 local writer performance checkpoint

- Date: 2026-09-14
- Build: CMake Release, arm64, warnings-as-errors
- Host: Apple M2 (8 cores), 16 GB RAM, macOS 26.5.2
- Scope: local synthetic stress profile and warm filesystem cache; not a
  published performance claim

## Repeatable profile

The build-tree `mmxisf-writer-benchmark` creates one deterministic 6064x4040
Planar UInt8 RGB image (73,495,680 decoded bytes). Its bounded pseudo-random
noise plus low-frequency gradient is intentionally less compressible than the
earlier modular-ramp tests. The writer uses Zstandard level 3, byte shuffle,
SHA-256, the default 16 MiB compression subblocks, and an attached data block.

Run it against a nonexistent destination:

```sh
./build-m3-warnings/mmxisf-writer-benchmark /private/tmp/writer-benchmark.xisf
```

The reported interval excludes deterministic source-array generation. It
includes subblock compression, spool writes, the checksum pass over the spool,
final-file assembly, flush/close, and no-overwrite commit. Peak RSS below was
measured for the whole process with macOS `/usr/bin/time -l`, so it includes the
73,495,680-byte input vector.

## Result

Five separate warm-cache invocations produced these writer intervals:

| Run | Elapsed | Throughput |
| ---: | ---: | ---: |
| 1 | 0.264 s | 265.007 MiB/s |
| 2 | 0.279 s | 250.986 MiB/s |
| 3 | 0.264 s | 265.214 MiB/s |
| 4 | 0.308 s | 227.693 MiB/s |
| 5 | 0.288 s | 243.073 MiB/s |
| **Median** | **0.279 s** | **250.986 MiB/s** |

- serialized attachment: 49,369,370 bytes;
- complete file: 49,373,466 bytes;
- maximum RSS in a separate instrumented run: 125,009,920 bytes;
- maximum RSS overhead above the input image: 51,514,240 bytes (49.13 MiB);
- whole-file SHA-256, identical in all five runs:
  `6897d239eca0042cc98c71e92be597344148cd2083a6316a711e2d3cba967c33`;
- decoded pixel SHA-256 after `mmxisf-inspect --decode-sha256`:
  `9539b574af18d10ad4c942ffd84a3235647b874f18c54621af338450e109592f`.

Before file-backed spooling, the same profile reached 246,857,728 bytes maximum
RSS because the complete compressed block and vector-growth reallocations
coexisted with the input and codec scratch. Spooling reduced that observation
by 121,847,808 bytes (49.36%) while preserving identical output bytes.

## Gate interpretation

The local pre-adoption writer gate is at least 250 MiB/s median warm-cache
throughput and no more than 64 MiB peak-RSS overhead above one caller-owned
input buffer for a representative image of at least 70 MiB. This checkpoint
passes both thresholds, but throughput clears the target narrowly. It is a
host-specific regression gate, not a portable SLA or evidence for slow/cold
storage. Cross-platform CI proves compilation and functional behavior; future
dedicated-host measurements are required before public performance claims.
