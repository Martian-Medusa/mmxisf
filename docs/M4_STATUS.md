# M4 progress: PFI metadata fidelity and interoperability

- Status: IN_PROGRESS
- Started: 2026-09-13
- Specification baseline: pinned XISF 1.0 sections 8.4, 11.1, 11.6, and 11.13
- Publication status: private repository; no tag or release

## First implementation slice

- Metadata now distinguishes XISF-unit, direct-image, and standalone lexical
  scope.
- Attribute, direct character-data, and data-block property forms are explicit.
- `uid`, `format`, `comment`, raw block location, and vector/matrix extents are
  preserved in the public document model.
- FITS keyword values and comments remain exact and unnormalized for the PFI
  adapter boundary.
- The inspector and macOS viewer expose scope and value form; block-backed
  properties show their location instead of being presented as decoded text.
- Ordered metadata bindings resolve forward `Reference` elements to shared
  Property/FITSKeyword entries while preserving lexical scope, duplicates, and
  direct-versus-reference provenance.
- Property categories enforce their structural serialization contracts:
  scalar/complex and TimePoint attributes, String character data or block,
  vector length plus block, and matrix dimensions plus block.
- Property identifiers and FITS keyword names are checked against their
  respective XISF/FITS ASCII grammars, and Property identifiers must remain
  unique within each resolved unit or image association.
- Malformed numeric property extents and malformed block locations fail closed.
- Scalar, Boolean, complex, and TimePoint attribute values are validated by
  bounded linear scans without coercing or rewriting their source text.
- Integer properties additionally enforce exact declared-width ranges through
  128 bits, including full-width prefixed two's-complement bit patterns.
- Image descriptors now expose the fixed serialized origin/traversal, nominal
  Gray/RGB/CIELab channel order, optional display orientation, and decoded
  checksum-verification status without transforming scientific pixels.
- Inline Property blocks nested in an embedded Image are distinguished from the
  Image's direct character data, so valid metadata does not trip the
  outside-`Data` rejection rule.
- Block-backed String, vector, and matrix Properties are readable from
  attachment, inline Base64, and inline hexadecimal locations. The typed extent
  is checked before delivery; matrices remain row-major and complex values swap
  endian order per scalar component.
- Property blocks reuse the bounded zlib, LZ4/LZ4HC, Zstandard, byte-shuffle,
  compression-subblock, SHA-1/256/512, and cancellation pipeline. Independent
  256 MiB serialized and decoded defaults prevent metadata arrays from silently
  inheriting the 2 GiB image budget.

## Evidence available now

- Synthetic direct-image scalar Property with value, format, comment, and uid:
  PASS locally.
- Synthetic quoted FITS value and comment preservation: PASS locally.
- Synthetic standalone inline vector Property with length and block location:
  PASS locally.
- Exact inline F64Vector, big-endian attachment F64Vector, zlib plus checksum,
  Zstandard plus byte shuffle, size-mismatch, invalid-index, and mid-read
  cancellation cases: PASS locally.
- XISF-unit String character data and lexical scope: PASS locally.
- Malformed vector extent rejection: PASS locally.
- Interleaved direct FITS keywords and repeated forward references retain exact
  image-binding order: PASS locally.
- A FITS keyword reference targeting XISF-unit metadata is rejected: PASS
  locally.
- Positive scalar, complex, String, TimePoint, vector, and matrix forms plus a
  negative structural-form matrix: PASS locally.
- Invalid Property identifiers, invalid FITS names, and duplicate image
  Property identifiers: PASS locally.
- Synthetic two-image input preserves two image descriptors, distinct ordered
  bindings, and equal Property identifiers in separate image scopes: PASS
  locally. PFI's one-image rejection remains adapter-owned and documented.
- Metadata inspection with the new scope/value/binding model: 9/9 private local
  PixInsight corpus files PASS; files remain outside the repository.
- Release unit suite and macOS viewer build: PASS locally.
- Positive and negative lexical matrices cover Boolean alphabetic/numeric
  forms, decimal and prefixed integers, floating special values and exponents,
  complex components, calendar validity, fractional seconds, and optional time
  zones: PASS locally.
- Deterministic and coverage-guided fuzz seed corpora include valid Boolean,
  integer, floating-point, complex, and TimePoint Property values.
- All eight XISF 1.0 orientation values, absent versus explicit identity,
  invalid orientation rejection, channel-order mapping, and checksum provenance:
  PASS locally.
- A private PixInsight 1.9.3/XISF module 1.1.2 embedded UInt8 RGB resource with
  many inline and attached block-backed properties now passes metadata
  inspection and full Zstandard decode. Its exact source and decoded-pixel
  identities are recorded in `SOURCES.md` and `M3_PERFORMANCE.md`; bytes remain
  outside the repository.
- Property decode over the four pinned private current-PixInsight files is
  4/4 PASS: 33/33 block-backed values (30, 0, 2, and 1 per file), covering
  inline/attachment, String/UI8/F64 vector/matrix, and Zstandard-shuffled data.
  The astrometric file includes exact 2x2 linear WCS and two-element reference
  coordinate blocks required by the PFI projection. This is private-producer
  compatibility evidence, not an independent-producer claim.

## Still required for M4

- Project the newly decoded WCS vectors/matrices through the PFI adapter and
  compare its conservative FITS stripping with the current host result.
- Exercise the metadata parity matrix against independently produced files and
  the local non-redistributed PFI corpus.
- Record an explicit PFI multi-image accept/reject result for each input while
  the standalone library continues to enumerate all images.
