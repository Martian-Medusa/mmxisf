# M3 progress: codecs, shuffle, checksums, and budgets

- Status: IMPLEMENTATION_COMPLETE_LOCAL; CI_AND_INTEROPERABILITY_PENDING
- Started: 2026-09-13
- Specification baseline: pinned XISF 1.0 sections 10.5 and 10.6
- Publication status: private repository; no tag or release

## First implementation slice

- `zlib:<uncompressed-size>` decoding for attachment and embedded Image blocks.
- `zlib+sh:<uncompressed-size>:<item-size>` decoding with exact reverse byte
  shuffle.
- `lz4` and `lz4hc` decoding through the same safe LZ4 block decoder, including
  their `+sh` forms and compression subblocks.
- Optional compression subblocks are parsed with bounded count, checked totals,
  nonzero lengths, and per-subblock shuffle divisibility.
- Declared uncompressed size must equal the byte count derived independently
  from image geometry and sample format.
- Serialized input size, decoded output size, sample count, channel count, and
  decompression ratio are bounded before codec invocation or allocation.
- Zlib streams must terminate exactly after consuming the declared compressed
  subblock and producing its declared uncompressed size; trailing bytes,
  truncation, corruption, and size mismatches fail closed.
- LZ4 subblocks are bounded by both the project resource policy and the codec's
  signed-int API limit; negative codec results and output-size mismatches fail
  closed.
- SHA-1 (`sha-1` and `sha1`), SHA-256 (`sha-256` and `sha256`), and SHA-512
  (`sha-512` and `sha512`) descriptors are validated and computed with OpenSSL
  EVP. Digests must have the exact length and lowercase hexadecimal encoding.
- Checksums cover the serialized block bytes and are verified before any codec
  call. A mismatch has its own `checksum_mismatch` error code.
- The decoded result continues through explicit Planar/Normal and source/native
  endian transformations without sample conversion.
- Embedded compression attributes are read from the `Data` child as required by
  the specification; placing them on an embedded `Image` is rejected.
- The macOS viewer bundle embeds non-system Expat, zlib, LZ4, and OpenSSL Crypto
  and is signed only after dependency install names are rewritten.

## Evidence available now

- Exact static zlib byte vectors for attachment and embedded RGB: PASS.
- Exact zlib+shuffle UInt16 RGB oracle, including Planar/big-endian to
  Normal/native transformation: PASS.
- Two independently compressed zlib subblocks concatenated into one image:
  PASS.
- Static LZ4 and LZ4HC-compatible RGB vectors, LZ4 byte shuffle, and two
  concatenated LZ4 subblocks: PASS.
- Static SHA-1, SHA-256, and SHA-512 vectors, including both SHA-1 aliases:
  PASS.
- Deliberately corrupted compressed bytes with a valid digest for the original
  block return `checksum_mismatch`, rather than a codec error: PASS for the
  checksum-before-decompression invariant.
- Malformed descriptor, uncompressed-size mismatch, subblock-total mismatch,
  unknown codec, invalid stream, and decompression-ratio rejection: PASS.
- Warning-clean Release build and 4/4 unit suite: PASS locally on macOS.
- ASan/UBSan 4/4 unit suite: PASS locally on macOS.
- Deterministic ASan/UBSan mutation smoke, 20,000 cases: PASS.
- Installed-package consumer: PASS with Expat, zlib, LZ4, and OpenSSL Crypto
  discovered through the exported CMake package.
- Strict ad-hoc viewer bundle signature and `@rpath` resolution for Expat,
  zlib, LZ4, and OpenSSL Crypto: PASS.
- Coverage-guided seed corpus now includes valid embedded zlib+SHA-256 and LZ4
  files, in addition to the minimal metadata-only seed.

## Still required to close M3

- Cross-platform CI and coverage-guided fuzzing for the complete M3 change set.
- Independent-producer compressed fixtures and performance/memory measurements
  on representative PFI mono and RGB images.
