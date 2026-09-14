# M7 progress: broader image sample coverage

- Status: EXTENDED_SAMPLE_AND_EXTENSION_INSPECTION_IMPLEMENTED;
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

## Still required for M7

- Obtain independently produced UInt64 and complex fixtures from another
  producer, with provenance and exact pixel hashes.
- Implement any additional metadata object families selected for the 1.0
  monolithic profile.
