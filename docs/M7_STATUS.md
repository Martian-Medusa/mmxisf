# M7 progress: broader image sample coverage

- Status: EXTENDED_SAMPLE_AND_CORE_INSPECTION_IMPLEMENTED;
  EXTERNAL_SAMPLE_ORACLE_PENDING
- Started: 2026-09-14
- Specification baseline: pinned XISF 1.0 section 11.5.1
- Publication status: private repository; no tag or release

## Implemented scope

- Exact raw image reads for UInt64, Complex32, and Complex64 in addition to the
  PFI scalar profile.
- Source-preserving byte delivery through uncompressed and existing bounded
  compressed/checksummed block paths.
- Opt-in native-endian conversion that reverses each real/imaginary component
  independently for complex samples.
- Existing Planar/Normal layout conversion without numeric reinterpretation.
- Explicit complex-preview rejection in the macOS PoC; metadata inspection is
  retained and no misleading magnitude image is synthesized.
- SHA3-256 and SHA3-512 block integrity verification and writer output through
  the existing checksum-before-decompression and bounded spool paths.
- Namespace-aware semantic inventory of non-XISF extension elements in document
  order, with parent/image links, normalized attributes, and direct text.
- Independent finite defaults for extension elements, cumulative attributes,
  and copied semantic bytes; limit violations fail as `resource_limit`.
- Explicitly no raw-XML, prefix, CDATA-boundary, comment, processing-instruction,
  or byte-identical writer round-trip claim.
- Validated, attribute-preserving inspection for `RGBWorkingSpace`,
  `DisplayFunction`, `ColorFilterArray`, and `Resolution`, including direct and
  `Reference`-resolved image associations.
- Ancillary inspection preserves exact XML-decoded parameter text, never
  synthesizes specification defaults, and never applies a color or display
  transform to scientific pixels.
- Ordered ICC profile descriptors and direct/`Reference`-resolved image
  associations, with attachment and inline Base64/Base16 reads.
- ICC bytes share checksum-before-decompression and bounded codec paths, remain
  big-endian and untransformed, and receive limited header/signature/embedded-
  flag screening rather than a full color-management interpretation.
- Validated UInt8/UInt16 Gray/RGB Thumbnail descriptors, direct/referenced
  main-image associations, and exact attachment/embedded reads with independent
  count, binding, dimension, serialized-byte, and decoded-byte limits.
- Bounded inspection of `Structure`, `Field`, `Table`, `Row`, and `Cell`, with
  ordered schemas/data, exact text and block descriptors, direct/referenced
  image associations, document-local standalone Structure resolution, and
  structural/type-form/cardinality validation under dedicated limits.
- Table Cell block payloads remain inspect-only: no external resolution,
  decoding, integrity claim, or typed numeric convenience API is implied.

PFI remains intentionally scalar-only and must reject complex images at the
adapter boundary. This milestone expands the standalone reader, not the PFI
scientific contract.

## Evidence available now

- Exact source-byte decode for UInt64, Complex32, and Complex64 synthetic
  attachments.
- Bitwise big-endian-to-native oracles for two samples of each new format.
- Complex32 conversion reverses two 4-byte components independently;
  Complex64 conversion reverses two 8-byte components independently.
- A big-endian Complex32 zlib+shuffle fixture exercises decompression,
  unshuffle, and component-wise native-endian conversion in one path.
- Existing resource budgets, checksum-before-decompression, cancellation, and
  layout paths remain shared with the scalar decoder.
- The documented `XISF.write` API of independent package `xisf` 0.9.7 was
  probed with NumPy `uint64`, `complex64`, and `complex128` arrays. It rejected
  all three as unimplemented sample formats, so no external oracle is claimed.
- Independent package `xisf` 0.9.7 accepted a writer-produced
  Zstandard+shuffle+SHA3-256 descriptor and returned an exact 2x2x3 UInt16 RGB
  value matrix. The committed fixture is reverified by mmxisf, including its
  digest; independent digest verification itself is not inferred.
- Synthetic nested extension tests cover two namespaces, qualified and
  unqualified attributes, document order, direct mixed text, parent linkage,
  image association, and each dedicated resource limit.
- Synthetic ancillary tests cover all four selected families, standalone and
  direct image placement, shared `uid` references, malformed required/numeric/
  vector/CFA fields, and all dedicated resource limits. The existing private
  PixInsight corpus remains 9/9 metadata-open PASS after stricter validation.
- Synthetic ICC tests cover attached and zlib-compressed inline profiles,
  SHA-256 verification, exact decoded bytes, direct/referenced associations,
  forbidden `byteOrder`, invalid placement/content, external read rejection,
  header screening, and dedicated resource limits.
- Synthetic Thumbnail tests cover UInt8/UInt16, Gray/RGB/alpha channel rules,
  source representation, attachment and compressed embedded blocks, checksum,
  direct/referenced associations, forbidden forms, cancellation, and limits.
- One native PixInsight-produced 400 x 267 UInt8 Gray attachment decodes to
  106,800 bytes. Its decoded SHA-256
  `6bb8c5ff6acb91b5f7bd24afadda339a70a95d0b5d8f911d37f2870f6715c1a5`
  matches an independent raw file-range hash; redistribution remains unknown.
- Synthetic Table tests cover standalone/referenced and inline Structures,
  direct/referenced image associations, scalar/String/TimePoint/vector forms,
  declared/actual shape agreement, field uniqueness, invalid references and
  children, and every dedicated resource limit.

## Still required for M7

- Obtain independently produced UInt64 and complex fixtures from another
  producer, with provenance and exact pixel hashes.
- Obtain an independently produced `Structure`/`Table` fixture with provenance;
  typed Cell payload decoding remains deliberately deferred until a consumer
  requirement justifies its API and evidence cost.
