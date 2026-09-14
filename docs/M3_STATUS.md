# M3 progress: codecs, shuffle, checksums, and budgets

- Status: COMPLETE_FOR_PFI_PREREQUISITES
- Started: 2026-09-13
- Specification baseline: pinned XISF 1.0 sections 10.5 and 10.6
- Publication status: private repository; no tag or release

## First implementation slice

- `zlib:<uncompressed-size>` decoding for attachment and embedded Image blocks.
- `zlib+sh:<uncompressed-size>:<item-size>` decoding with exact reverse byte
  shuffle.
- `lz4` and `lz4hc` decoding through the same safe LZ4 block decoder, including
  their `+sh` forms and compression subblocks.
- `zstd` and `zstd+sh` decoding through the bounded Zstandard API for current
  PixInsight interoperability. This is recorded as an extension to the pinned
  2017 XISF 1.0 baseline, not silently folded into the old conformance claim.
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
- Zstandard frames must produce the exact independently derived output size;
  codec errors, corruption, and size mismatches fail closed.
- SHA-1 (`sha-1` and `sha1`), SHA-256 (`sha-256` and `sha256`), SHA-512
  (`sha-512` and `sha512`), SHA3-256, and SHA3-512 descriptors are validated
  and computed with OpenSSL
  EVP. Digests must have the exact length and lowercase hexadecimal encoding.
- Checksums cover the serialized block bytes and are verified before any codec
  call. A mismatch has its own `checksum_mismatch` error code.
- The decoded result continues through explicit Planar/Normal and source/native
  endian transformations without sample conversion.
- Embedded compression attributes are read from the `Data` child as required by
  the specification; placing them on an embedded `Image` is rejected.
- The macOS viewer bundle embeds non-system Expat, zlib, LZ4, Zstandard, and
  OpenSSL Crypto and is signed only after dependency install names are rewritten.

## Evidence available now

- Exact static zlib byte vectors for attachment and embedded RGB: PASS.
- Exact zlib+shuffle UInt16 RGB oracle, including Planar/big-endian to
  Normal/native transformation: PASS.
- Two independently compressed zlib subblocks concatenated into one image:
  PASS.
- Static LZ4 and LZ4HC-compatible RGB vectors, LZ4 byte shuffle, and two
  concatenated LZ4 subblocks: PASS.
- Static Zstandard RGB and Zstandard+shuffle UInt16 vectors plus corrupt-frame
  rejection: PASS locally.
- Static SHA-1, SHA-256, SHA-512, SHA3-256, and SHA3-512 vectors, including
  both SHA-1 aliases:
  PASS.
- A committed mmxisf writer fixture with Zstandard+shuffle+SHA3-256 verifies
  its digest and source-order pixels in compiled tests. Independent package
  `xisf` 0.9.7 accepted the exact descriptor and returned all 12 RGB values;
  its own digest-verification behavior is not claimed.
- Deliberately corrupted compressed bytes with a valid digest for the original
  block return `checksum_mismatch`, rather than a codec error: PASS for the
  checksum-before-decompression invariant.
- Malformed descriptor, uncompressed-size mismatch, subblock-total mismatch,
  unknown codec, invalid stream, and decompression-ratio rejection: PASS.
- Warning-clean Release build and 5/5 unit suite: PASS locally on macOS.
- ASan/UBSan 5/5 unit suite: PASS locally on macOS.
- Deterministic ASan/UBSan mutation smoke, 20,000 cases: PASS.
- Installed-package consumer with Expat, zlib, LZ4, Zstandard, and OpenSSL
  Crypto discovered through the exported CMake package: PASS locally and on
  Linux, macOS, and Windows in CI run `34781659044`.
- Strict ad-hoc viewer bundle signature and `@rpath` resolution for Expat,
  zlib, LZ4, Zstandard, and OpenSSL Crypto: PASS.
- Coverage-guided seed corpus now includes valid embedded zlib+SHA-256 and LZ4
  files, in addition to the minimal metadata-only seed.
- CI run `34775853605`: PASS on Linux, macOS, and Windows, including build,
  tests, install, and a separately configured installed-package consumer.
- The same CI run passed both the deterministic ASan/UBSan mutation smoke and
  the coverage-guided libFuzzer campaign at 20,000 cases each.
- A local warm-cache Release checkpoint decoded one private PixInsight
  6248x4176 Float32 Gray attachment in a 34.9 ms median and three such images
  sequentially in a 75.5 ms median. Peak RSS remained approximately one
  decoded 100 MiB plane in both cases; methodology and limitations are recorded
  in [M3_PERFORMANCE.md](M3_PERFORMANCE.md).
- Four current PixInsight files, including embedded/attached Zstandard,
  UInt8/UInt16/Float32/Float64, Gray/RGB, and 73-518 MiB decoded outputs, decode
  successfully with exact pixel-buffer SHA-256 anchors. Single-run timing and
  memory measurements are recorded separately from the earlier median sample.
- Follow-up CI run `34781659044` at commit `5218074`: PASS for Linux, macOS,
  Windows, installed-package consumers, deterministic 20,000-case sanitizer
  mutation, and coverage-guided 20,000-run libFuzzer gates.
- A committed independent-producer matrix decodes exact externally serialized
  zlib+sh, LZ4+sh, LZ4HC+sh, and Zstandard+sh blocks. It verifies every required
  scalar family and both Gray and RGB through exact decoded-byte identities.
- Five repeatable Release decodes of the current 6064x4040 UInt8 RGB embedded-
  Zstandard fixture produced the same 73,495,680-byte pixel SHA-256 in 40 ms
  each. Peak RSS was 77,807,616-77,856,768 bytes, at most 4,361,088 bytes above
  the owning decoded buffer.
- The observed run clears the local pre-adoption gate of at least 250 MiB/s
  warm-cache throughput and no more than 64 MiB RSS overhead above one decoded
  buffer. This is a host-specific regression budget, not a portable SLA.

## M3 acceptance boundary

The codec, resource-limit, integrity, fuzz, package, interoperability, and local
performance prerequisites are complete. Dedicated-host or cold-cache public
performance claims remain deferred until a product SLA requires them; they do
not block PFI integration validation.
