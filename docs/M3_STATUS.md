# M3 progress: codecs, shuffle, checksums, and budgets

- Status: IN_PROGRESS
- Started: 2026-09-13
- Specification baseline: pinned XISF 1.0 sections 10.5 and 10.6
- Publication status: private repository; no tag or release

## First implementation slice

- `zlib:<uncompressed-size>` decoding for attachment and embedded Image blocks.
- `zlib+sh:<uncompressed-size>:<item-size>` decoding with exact reverse byte
  shuffle.
- Optional compression subblocks are parsed with bounded count, checked totals,
  nonzero lengths, and per-subblock shuffle divisibility.
- Declared uncompressed size must equal the byte count derived independently
  from image geometry and sample format.
- Serialized input size, decoded output size, sample count, channel count, and
  decompression ratio are bounded before codec invocation or allocation.
- Zlib streams must terminate exactly after consuming the declared compressed
  subblock and producing its declared uncompressed size; trailing bytes,
  truncation, corruption, and size mismatches fail closed.
- The decoded result continues through explicit Planar/Normal and source/native
  endian transformations without sample conversion.
- Embedded compression attributes are read from the `Data` child as required by
  the specification; placing them on an embedded `Image` is rejected.
- The macOS viewer bundle embeds non-system zlib alongside Expat and is signed
  only after dependency install names are rewritten.

## Evidence available now

- Exact static zlib byte vectors for attachment and embedded RGB: PASS.
- Exact zlib+shuffle UInt16 RGB oracle, including Planar/big-endian to
  Normal/native transformation: PASS.
- Two independently compressed zlib subblocks concatenated into one image:
  PASS.
- Malformed descriptor, uncompressed-size mismatch, subblock-total mismatch,
  unknown codec, invalid stream, and decompression-ratio rejection: PASS.
- Warning-clean Release build and 4/4 unit suite: PASS locally on macOS.
- ASan/UBSan 4/4 unit suite: PASS locally on macOS.
- Deterministic ASan/UBSan mutation smoke, 20,000 cases: PASS.
- Installed-package consumer: PASS with zlib discovered through the exported
  CMake package.
- Strict ad-hoc viewer bundle signature and `@rpath` resolution for Expat and
  zlib: PASS.

## Still required to close M3

- LZ4 and LZ4HC-compatible decoding, with and without byte shuffle.
- SHA-1, SHA-256, and SHA-512 verification over serialized block bytes before
  any decompression attempt.
- Negative checksum-order evidence proving a failed digest never reaches a
  codec.
- Cross-platform CI and coverage-guided fuzzing for the complete M3 change set.
- Independent-producer compressed fixtures and performance/memory measurements
  on representative PFI mono and RGB images.
