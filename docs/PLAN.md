# Implementation plan

## Outcome

Build an independent XISF 1.0 implementation whose core can be embedded in PFI
without PixInsight/PCL and can also be installed as a normal CMake package. The
first usable milestone is a production-grade reader for PFI's real input
profile. Broad writer and long-tail format coverage follow without blocking PFI.

## Product boundaries

The library owns container parsing, XML-to-domain mapping, data-block access,
decompression, byte unshuffling, checksum verification, typed pixel delivery,
metadata preservation, and serialization. It does not own astronomy analysis,
PSF measurement, calibration, UI, file selection, or physical interpretation.

The initial PFI profile is deliberately narrower than the entire XISF grammar:

- monolithic local files;
- one selected 2-D image, while enumerating and explicitly rejecting or
  selecting among multi-image files;
- grayscale and RGB, planar storage, scalar integer and floating samples used
  by the accepted fixture corpus;
- attached, inline, and embedded data locations where required by the audited
  specification matrix;
- XISF properties and FITS keyword records needed by PFI, plus lossless access
  to unknown metadata;
- uncompressed and supported compressed blocks with byte shuffling and checksum
  verification.

Exact sample types, codecs, checksum algorithms, property types, and optional
elements must be frozen in a machine-readable conformance matrix during M0.
Ecosystem implementations are reconnaissance, not normative sources.

## Work streams

### 1. Specification and conformance contract

1. Archive the exact official XISF 1.0 specification revision or hash when its
   redistribution terms permit it; otherwise record URL, access date, and a
   derived requirements matrix without copying prose.
2. Convert every normative construct into a conformance row: parse, preserve,
   decode, encode, reject, or defer.
3. Resolve ambiguities with independently generated files and documented
   PixInsight behavior, never by copying another implementation.
4. Freeze the PFI v0 profile and a later full-format profile separately.

Exit: reviewed conformance matrix, clean-room log, fixture provenance schema,
and no unresolved ambiguity on any PFI-required row.

Status: completed in M0 on 2026-09-13. The only recorded cross-section ambiguity
is resolved conservatively in `SPECIFICATION_NOTES.md`; future first-party
errata can supersede it.

### 2. Safe container and XML layer

1. Parse the fixed preamble and bounded UTF-8 XML header.
2. Disable external entities, DTD/network access, and implicit external
   resource loading.
3. Validate version, namespace, element/attribute syntax, and numeric fields.
4. Build immutable domain objects while retaining unknown extension data in a
   bounded representation.
5. Validate all offset/size ranges and overlaps before block reads.

Exit: header inspection works without allocating pixel storage; malformed and
resource-exhaustion cases fail with structured errors.

### 3. Block and pixel pipeline

1. Implement local attached, inline, and embedded block sources according to
   the frozen matrix.
2. Provide checked arithmetic for geometry and byte counts.
3. Add decompression adapters and byte-unshuffle stages behind small interfaces.
4. Verify declared checksums before exposing trusted output.
5. Convert byte order without changing sample values or channel order.
6. Support both caller-owned buffers and bounded streaming/tile callbacks where
   the storage mode permits it.

Exit: required PFI pixel formats match independent full-precision or bitwise
oracles; large uncompressed frames do not require duplicate full-frame buffers.

### 4. Metadata model

1. Preserve XISF property identity, declared type, dimensions, value, comment,
   and source location.
2. Preserve FITS keyword name, raw value, stripped value, and comment as
   distinct fields where the source provides them.
3. Add typed convenience accessors that never replace missing/invalid values
   with defaults.
4. Cover ICC profile, CFA, thumbnail, resolution, display function, RGB working
   space, and other accepted image children per the matrix.

Exit: PFI can reproduce its existing metadata inputs without fabricated values;
round-trip tests show which constructs are lossless and which are canonicalized.

### 5. Deterministic writer

1. Design a builder API that requires dimensions, sample type, color space,
   storage layout, and metadata explicitly.
2. Generate canonical, deterministic XML for equivalent inputs.
3. Plan block sizes and alignment before writing; support seekable output first.
4. Add compression only when requested and preserve the chosen codec/settings
   as provenance.
5. Reopen every generated fixture with both `mmxisf` and an independent oracle.

Exit: monolithic files round-trip and open in supported external consumers;
repeat builds of the same input are byte-identical when creation timestamps and
other volatile fields are fixed.

### 6. Hardening and distribution

1. Continuous fuzzing of preamble, XML mapping, block descriptors,
   decompression, shuffle, and typed conversion.
2. Sanitizer jobs, 32/64-bit arithmetic tests, big-endian simulation or targeted
   byte-order tests, and failure-injection tests.
3. Cross-platform CI, installed-package consumer test, static/shared builds, and
   symbol visibility checks.
4. API documentation, examples, security policy, contribution guide, SBOM, and
   dependency/license inventory.

Exit: public beta gate is satisfied with no known high-severity parser issue and
all claimed conformance rows backed by fixtures.

## Core implementation choices to decide in M0

- XML parser after security and license evaluation (pugixml is the leading
  candidate, not yet locked).
- Error ABI: exceptions, status/result object, or layered APIs.
- Buffer ownership, memory mapping, and cancellation contracts.
- Codec linkage policy: system packages, FetchContent, or vendored release
  archives. Vendoring is not the default.
- Public ABI promise and minimum supported compiler matrix.
- Final project/repository name and trademark review.

## Definition of done for PFI adoption

- PFI-required conformance profile is green.
- Results match PixInsight-loaded reference pixels and metadata on a versioned,
  hashed corpus from at least PixInsight and one acquisition application.
- Corrupt/truncated/oversized inputs fail safely and diagnostically.
- Peak memory and throughput meet explicit budgets on representative mono and
  RGB frames.
- PFI's image I/O adapter can switch between the current host loader and
  `mmxisf` in parity tests; no scientific algorithm changes are bundled with the
  switch.
- Native PixInsight/PCL and standalone builds are validated independently.
