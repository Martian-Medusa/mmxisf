# M6 progress: deterministic monolithic writer

- Status: REVISION1_IMPLEMENTED; EXTERNAL_ORACLE_PASS;
  NATIVE_PIXINSIGHT_REVISION1_SUBBLOCKS_PASS;
  NATIVE_PIXINSIGHT_CURRENT_PROPERTIES_PENDING
- Started: 2026-09-14
- Specification baseline: pinned XISF 1.0 section 7.1
- Publication status: public repository; immutable `v0.1.0-rc.1` remains the
  historical document-1.00 prerelease; Revision 1 changes are not released

## Foundation scope

- Public `ImageWriteView`, `WriterOptions`, `WriteSummary`, and
  `Writer::write_file` API, independent of PFI/PCL/UI types.
- One or more attached little-endian Planar Gray or RGB images using UInt8,
  UInt16, UInt32, Float32, or Float64 samples and exact non-owning pixel spans.
- Required finite increasing bounds for floating-point images, plus explicit
  per-image, image-count, and cumulative decoded-byte budgets.
- Required deterministic XISF creation time and creator application metadata.
- Declarative direct metadata records for image-scoped Properties and FITS
  keywords plus XISF-unit Properties; raw XML is never accepted.
- Direct Boolean, signed/unsigned integer through 128-bit, real floating-point,
  and complex Properties with lexical/range validation and exact text
  preservation.
- Deterministic attached vector/matrix Properties with exact typed extents,
  little- or big-endian source bytes, formatting metadata, independent budgets,
  compression, element shuffle, checksums, bounded subblocks/spooling, and block
  layout reporting.
- Per-image zlib, LZ4, LZ4HC, and Zstandard compression, optional byte shuffle,
  and SHA-1/256/512 plus SHA3-256/512 checksums over exact serialized
  attachment bytes.
- Deterministic compression subblocks with a 16 MiB default, bounded count,
  whole-block byte shuffle before subblock division as required by Revision 1,
  bounded per-subblock codec buffers, and cancellation checkpoints.
- Independent decoded and serialized per-image/cumulative byte budgets.
- Canonical XML field order, XML 1.0 UTF-8 validation/escaping, fixed zero
  padding, power-of-two attachment alignment, and fixed-point block planning.
- Checked geometry arithmetic and finite header/image budgets before creating a
  file.
- Chunked pixel delivery with cooperative cancellation.
- Multi-subblock compressed output is staged through bounded sibling spool
  files instead of retaining the complete serialized image in RAM.
- A sibling temporary file is linked into a previously absent destination only
  after a successful flush. Temporary and spool files use OS-level exclusive
  creation, and cleanup owns only paths the writer successfully created.
  Existing destination and stale temporary paths are never overwritten;
  failed/cancelled writes remove incomplete temporary data.
- A caller-owned sequential `ByteSink` can receive identical serializer output
  with bounded partial-write loops, explicit flush/error propagation, and no
  implicit close or rollback. Multi-subblock calls require an explicit scratch
  stem with the same stale-file and cleanup guarantees.

## Evidence available now

- Warning-as-error static and shared Release builds and 8/8 tests: PASS
  locally on macOS. Fresh installed-package consumers pass for both linkages.
- AddressSanitizer/UndefinedBehaviorSanitizer 8/8 tests, deterministic 20,000
  case fuzz smoke, and warning-free generated API reference: PASS locally.
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
- A separate deterministic metadata fixture preserves XML-sensitive String
  text, a canonical TimePoint, a FITS value/comment pair, XISF-unit scope, and
  exact UInt16 pixels in both `mmxisf` and independent package `xisf` 0.9.7.
  Its serialized SHA-256 is
  `e9a64e68b495aed77da38ce900e490878ef5539a407d6aa10e9a23d562d279f8`.
- A scalar metadata matrix covers every supported Boolean, signed/unsigned
  integer, real, and complex type/alias. Boundary values include Int8
  minimum and UInt64 maximum; malformed floating syntax and UInt8 overflow fail
  before file creation. Independent package `xisf` 0.9.7 returns five
  representative Boolean, integer, real, and complex values through its public
  metadata API with exact types and typed values.
- A deterministic writer file with an image-scoped 2x2 F64Matrix and an
  XISF-unit big-endian UI16Vector reopens with exact source bytes in `mmxisf`.
  Independent package `xisf` 0.9.7 returned matrix values `[[1,2],[3,4]]`,
  vector values `[513,1027]`, the declared Zstandard+shuffle descriptor,
  SHA-256 checksum, format, byte order, and both exact attachment descriptors
  through its public API. The observed file is 12,292 bytes with SHA-256
  `4da1d1bef566e6cbe626522cf938d738db667e7b2648a26bc1a2c376eb125abb`.
- A compiled 40-type matrix writes and rereads every standard integer, real,
  complex, alias, and byte-array vector/matrix element width through the public
  API, preserving input order and exact bytes. This is a reader/writer drift
  guard, not independent semantic validation of every numeric type.
- Property classification and binary element layout now come from one internal
  registry shared by XML validation, block reading, and writing; the 40-type
  matrix guards the registry at the public round-trip boundary.
- A five-image codec fixture covers all four writer codecs, shuffled and
  unshuffled paths, all three checksums, and an uncompressed checksummed block.
  `mmxisf` verifies each digest before decompression and recovers one exact pixel
  hash; independent package `xisf` 0.9.7 accepts every descriptor and returns
  identical UInt16 pixels. The anchored file SHA-256 is
  `78911e120d89a6718765053d82e85fffd5f1c6da0739ff275f3085d7d2e7eaa5`.
- An eight-subblock Zstandard+shuffle+SHA-256 writer case is byte-deterministic,
  reopens in `mmxisf`, verifies its checksum before decompression, and recovers
  all 512 source bytes exactly. Zero/undersized subblock sizes and exhausted
  subblock-count budgets fail explicitly. Its exact Revision 1 serialization
  is committed with SHA-256
  `cb247d96a3b475d3189809f760552b41534f9b855df798e9e924db14f1c04f64`;
  compiled tests bind all eight subblock pairs, the checksum, and decoded pixel
  hash. PixInsight 1.9.5 arm64 natively recovered all 256 exact samples from
  this source-bound fixture. The retained PASS establishes native acceptance of
  this corrected Revision 1 path only.
- A separate eight-subblock Zstandard+shuffle+SHA3-512 case exercises the
  bounded spool-file digest path and reopens with exact bytes and explicit
  verified-integrity state.
- The repeatable 73,495,680-byte UInt8 RGB Zstandard+shuffle+SHA-256 benchmark
  produced identical bytes in 5/5 runs, reached 250.986 MiB/s median writer
  throughput, and used 125,009,920 bytes maximum RSS. File-backed spooling cut
  peak RSS by 121,847,808 bytes versus the pre-spool observation and limits
  overhead above the caller buffer to 49.13 MiB. See
  [M6_WRITER_PERFORMANCE.md](M6_WRITER_PERFORMANCE.md); this is a local
  regression gate, not a portable SLA.
- Short-write and multi-subblock sink outputs are byte-identical to atomic file
  outputs. Sink write, zero-progress, impossible write count, flush,
  cancellation, missing scratch, and stale scratch cases fail with the
  documented partial-output boundary.
- Sixteen simultaneous writers targeting one output yield exactly one complete,
  reopenable file, fifteen explicit I/O failures, and no leaked temporary. This
  compiled regression guards the exclusive-create/no-overwrite contract.
- A committed 12,292-byte source-bound native-validation fixture (SHA-256
  `e4cff5baa69d4cb952d95b1299fff63a3d6f24e2456a8546088487effdf8866d`)
  contains one 2x2 UInt16 Gray image plus image-scoped `F64Matrix`,
  `UI16Vector`, and String Properties. The compiled interoperability test
  verifies its exact matrix/vector source bytes, checksum state, metadata
  bindings, and pixel hash in Release, ASan/UBSan, and ThreadSanitizer builds.
  A source-hashed PJSR script under `tests/pixinsight` requires exact native
  values and working-sample pixels. Its predecessor, differing only by the now
  invalid hyphenated Image identifier, ran in a disposable PixInsight 1.9.4
  arm64 process without Computer Use. PixInsight recovered the exact 2x2
  matrix, vector `[513,1027]`, String value, UInt16 Gray representation, and
  working-sample pixel SHA-256. Sanitized historical evidence is retained in
  `docs/quality-runs/2026-09-14-pixinsight-writer-properties-macos-arm64.json`.
  The current source identity still requires a fresh native run; the historical
  observation is not current-fixture qualification.

## Still required for M6

- Run the remaining source-bound PixInsight block-Property validator on the
  current fixture, then repeat macOS/Linux, native Windows/MSVC, and
  coverage-guided fuzzing against one frozen RC2 candidate. Hosted CI is not
  required for intermediate commits.
- Repeat writer measurements on dedicated non-macOS hosts before assigning any
  portable performance claim.
