# M6 progress: deterministic monolithic writer

- Status: MULTI_SCALAR_IMPLEMENTED; EXTERNAL_ORACLE_PASS; CROSS_PLATFORM_PENDING
- Started: 2026-09-14
- Specification baseline: pinned XISF 1.0 section 7.1
- Publication status: private repository; no tag or release

## Foundation scope

- Public `ImageWriteView`, `WriterOptions`, `WriteSummary`, and
  `Writer::write_file` API, independent of PFI/PCL/UI types.
- One or more attached little-endian Planar Gray or RGB images using UInt8,
  UInt16, UInt32, Float32, or Float64 samples and exact non-owning pixel spans.
- Required finite increasing bounds for floating-point images, plus explicit
  per-image, image-count, and cumulative decoded-byte budgets.
- Required deterministic XISF creation time and creator application metadata.
- Canonical XML field order, XML 1.0 UTF-8 validation/escaping, fixed zero
  padding, power-of-two attachment alignment, and fixed-point block planning.
- Checked geometry arithmetic and finite header/image budgets before creating a
  file.
- Chunked pixel delivery with cooperative cancellation.
- A sibling temporary file is linked into a previously absent destination only
  after a successful flush. Existing destination and stale temporary paths are
  never overwritten; failed/cancelled writes remove incomplete temporary data.

## Evidence available now

- Warning-as-error Release build and 6/6 tests: PASS locally on macOS.
- Repeated equivalent Gray writes are byte-identical and reopen through
  `mmxisf` with exact descriptors, required metadata, and pixels. The original
  single-image RGB oracle hash remains unchanged after the multi-image API
  expansion.
- Planar little-endian UInt16 RGB output reopens with exact source bytes.
- Invalid time, malformed UTF-8, unsupported sample/storage profile, mismatched
  byte count, invalid alignment, header budget, existing destination, stale
  temporary path, and pre-cancelled write cases fail explicitly.
- A separately configured consumer builds against the installed package and
  sees the writer API.
- One generated 2x2 UInt16 RGB file was read through the documented
  public API of independent package `xisf` 0.9.7 as shape `(2, 2, 3)`, dtype
  `uint16`, with all 12 samples exactly equal. Its file SHA-256 was
  `951279e808a160c7028405f30cbffaa71ba1266dba5c245dbb9adf0c4294be10`.
  The exact Base64-transported file is committed under `tests/interop`, its
  deterministic source hash is asserted by the writer test, and the manifest
  records the independent result and exact pixel hash.
- A separate big-endian probe reopened correctly in `mmxisf` but the same
  independent consumer interpreted its samples as little-endian. The writer
  therefore rejects big-endian output until broader interoperability evidence
  exists; the reader's tested big-endian support is unchanged.
- A deterministic four-image file covers UInt8 Gray, UInt32 Gray, Float32 RGB,
  and Float64 Gray. Independent package `xisf` 0.9.7 enumerated all four and
  returned the exact dtypes, shapes, bounds, and source values through its
  documented public API. The committed Base64 fixture has SHA-256
  `c72c577d090e49d4966b1dda8d20e9948a9d23e5ba1fae3688c3ae34ddeb42ba`;
  compiled tests assert its file identity and all per-image pixel hashes.

## Still required for M6

- Complete Linux/macOS/Windows, sanitizer, fuzz, and installed-package gates.
- Add caller-declared metadata.
- Add requested compression/shuffle/checksum output profiles.
- Add a sink abstraction after actual file-writer behavior establishes its
  ownership and failure requirements.
