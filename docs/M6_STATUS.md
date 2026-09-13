# M6 progress: deterministic monolithic writer

- Status: FOUNDATION_IMPLEMENTED; CROSS_PLATFORM_AND_COMMITTED_ORACLE_PENDING
- Started: 2026-09-14
- Specification baseline: pinned XISF 1.0 section 7.1
- Publication status: private repository; no tag or release

## Foundation scope

- Public `ImageWriteView`, `WriterOptions`, `WriteSummary`, and
  `Writer::write_file` API, independent of PFI/PCL/UI types.
- Exactly one attached Planar UInt16 Gray or RGB image with explicit byte order
  and an exact non-owning pixel span.
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
  `mmxisf` with exact descriptors, required metadata, and pixels.
- Planar big-endian UInt16 RGB output reopens with exact source bytes.
- Invalid time, malformed UTF-8, unsupported sample/storage profile, mismatched
  byte count, invalid alignment, header budget, existing destination, stale
  temporary path, and pre-cancelled write cases fail explicitly.
- A separately configured consumer builds against the installed package and
  sees the writer API.
- One locally generated 2x2 UInt16 RGB file was read through the documented
  public API of independent package `xisf` 0.9.7 as shape `(2, 2, 3)`, dtype
  `uint16`, with all 12 samples exactly equal. Its file SHA-256 was
  `d380a1bec5714427b45e7d351ee62b587721efd637400eaacfc61d4d24df8020`.
  This is an initial black-box checkpoint; the generator fixture is not yet
  committed.

## Still required for M6

- Commit a deterministic writer-output fixture and its external-consumer
  provenance.
- Complete Linux/macOS/Windows, sanitizer, fuzz, and installed-package gates.
- Add multiple images and caller-declared metadata.
- Add requested compression/shuffle/checksum output profiles.
- Add a sink abstraction after actual file-writer behavior establishes its
  ownership and failure requirements.
