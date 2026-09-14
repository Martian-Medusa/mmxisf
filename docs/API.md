# mmxisf C++ API reference

This reference is generated from the installed public headers for mmxisf
0.1.0. The API is pre-release: source compatibility follows SemVer after the
first public release, while ABI stability is not promised before 1.0.

## Core workflow

1. Open a seekable source with `mmxisf::Reader::open_file` or
   `mmxisf::Reader::open_source` and explicit `mmxisf::ReaderOptions` limits.
2. Inspect the immutable `mmxisf::Document` without decoding pixels.
   `Document::extension_elements()` exposes a bounded, namespace-aware semantic
   inventory of non-core XML extensions.
3. Read an image into an owning `mmxisf::RawImage`, a correctly sized
   caller-owned span, or a caller-owned `mmxisf::ImageRowSink`. Use
   `mmxisf::ImageReadOptions` when a specific Planar/Normal layout is required;
   row delivery is always planar and can preserve source or native byte order.
4. Read block-backed metadata with `mmxisf::Reader::read_property_block` and
   ICC profile bytes with `mmxisf::Reader::read_icc_profile`, or a producer
   thumbnail with `mmxisf::Reader::read_thumbnail`.
5. Write deterministic local monolithic files with
   `mmxisf::Writer::write_file`, or use `mmxisf::Writer::write_to` with a
   caller-owned sequential `mmxisf::ByteSink`, and explicit
   `mmxisf::WriterOptions` budgets.

Every fallible operation returns `mmxisf::Result<T>`. Test the result before
calling `value()` or `error()`; those accessors follow `std::variant` semantics
and do not silently manufacture a fallback value.

## Ownership and lifetime

- `mmxisf::Reader` owns its parsed document and is move-only.
- `Reader::open_source` retains a shared pointer to the supplied
  `mmxisf::ByteSource`; the source must honor random-access read semantics for
  the reader's lifetime.
- `mmxisf::RawImage`, `mmxisf::RawPropertyBlock`, and
  `mmxisf::RawIccProfile` own their returned bytes.
- `Reader::read_image_into` writes only to the supplied span and reports the
  exact decoded byte count.
- `Reader::read_image_rows` borrows each `ImageRowView::bytes` span only for the
  synchronous callback. The sink copies any row data it needs to retain and
  owns rollback after partial delivery.
- Writer image and Property spans are borrowed only for the duration of the
  synchronous `Writer::write_file` or `Writer::write_to` call. A `ByteSink`
  remains caller-owned and is neither closed nor rolled back by the library.
- References returned by `Reader::document()` remain valid until the reader is
  destroyed or moved from.

## Representation contract

The default reader path preserves serialized sample type, precision, byte
order, channel layout, pixel traversal, and metadata text. Conversions of byte
order and Planar/Normal storage are opt-in and never change sample precision.
UInt64, Complex32, and Complex64 are delivered as exact raw bytes; complex
native-endian conversion swaps the real and imaginary components independently
without interpreting their numeric values.
Display orientation is descriptive and is not applied to scientific pixels.
Checksums are verified before compressed bytes are passed to a decoder.

Row delivery emits one complete planar channel row per callback. Planar sources
arrive channel-major; Normal sources arrive source-row-major and are split into
channel rows. A declared checksum is verified with bounded staging before the
first callback and the delivery pass is hashed again before success. Later
codec/source/mutation/cancellation/sink failure may leave rows already accepted
by the caller, so consumers needing atomic state must stage or roll back their
own results.

The writer accepts a deliberately bounded profile documented in
`docs/WRITER.md`. It rejects unsupported layouts and does not overwrite an
existing destination. Output is committed only after successful flush; failed
or cancelled writes remove their incomplete temporary files.

`Writer::write_to` emits the same bytes through a caller-owned sequential
`ByteSink`. Partial writes are completed, zero progress and sink/flush failures
fail explicitly, and a multi-subblock write requires a caller-selected scratch
stem. Unlike `write_file`, a generic sink cannot be rolled back: it may retain a
prefix after failure and the caller owns transaction and close semantics.

## Resource and trust boundary

XISF input is untrusted. `mmxisf::ReaderOptions` and
`mmxisf::WriterOptions` have finite defaults and are part of the correctness
contract, not performance hints. Applications may tighten them for their own
workloads. A resource-limit failure, checksum mismatch, unsupported feature,
or malformed structure is reported explicitly and yields no trusted pixels.
External paths and URLs are not resolved by the current profile.

`ImageRowReadOptions` separately bounds source/output row staging and each
compressed or decoded subblock buffer. Byte-shuffled delivery can require more
than one independently bounded buffer; the configured value is not a total
peak-memory limit.

Extension records preserve namespace/local names, normalized attributes,
direct character data, parent association, and containing-image association.
They intentionally do not preserve XML prefixes, CDATA boundaries, comments,
processing instructions, attribute syntax, or byte-identical source text. The
inventory is inspection data, not a writer round-trip representation.

`Document::ancillary_objects()` separately exposes validated
`RGBWorkingSpace`, `DisplayFunction`, `ColorFilterArray`, and `Resolution`
elements. Exact XML-decoded attribute strings are retained, while
`Document::ancillary_bindings()` distinguishes direct and `Reference`-resolved
image associations. Absence remains absence: the library does not materialize
specification defaults such as sRGB, the identity display function, or 72 dpi.
These records are descriptive and never transform decoded scientific pixels.

`Document::icc_profiles()` exposes attachment, inline, or external block
descriptors, while `Document::icc_profile_bindings()` preserves direct and
`Reference`-resolved image associations. `Reader::read_icc_profile()` supports
local attachment and inline Base64/Base16 bytes, verifies declared integrity
before decompression, and never performs an endian or color transform. It
checks the ICC size field, `acsp` signature, and embedded-profile flag; this is
not full ICC semantic validation or profile authentication.

`Document::thumbnails()` exposes validated UInt8/UInt16 Gray/RGB image-like
descriptors, and `Document::thumbnail_bindings()` preserves direct and
referenced main-image associations. `Reader::read_thumbnail()` returns exact
source-layout/source-endian pixels from local attachment or embedded blocks.
It does not resample, reorient, convert, stretch, or color-manage them.

`Document::table_structures()` and `Document::tables()` expose a bounded,
ordered inspection model for `Structure`, `Field`, `Table`, `Row`, and `Cell`.
Field identifiers/types, declared and actual shapes, cell serialization forms,
inline or external block descriptors, exact XML-decoded text, and direct or
`Reference`-resolved image associations are retained. Scalar and TimePoint cell
syntax, vector/matrix extents, row width, structure references, and property-id
uniqueness are validated. Cell data blocks are descriptors only in this
profile: they are not decoded or integrity-verified, and external locations
never trigger I/O.

## Cancellation and concurrency

Decode and write calls accept `std::stop_token` and check it at bounded work
boundaries. Cancellation is cooperative and returns
`mmxisf::ErrorCode::cancelled`.

Parsed documents are immutable. Read operations use per-call state and may run
concurrently when the supplied `ByteSource` supports concurrent calls. The
built-in file source serializes access to its stable file handle. Concurrent
writes to the same destination are intentionally rejected by the no-overwrite
contract. The maintained ThreadSanitizer gate exercises concurrent owning and
row reads through one built-in file-backed `Reader` plus concurrent writers to
one destination. A caller-supplied `ByteSource`, `ByteSink`, or `ImageRowSink`
retains responsibility for its own concurrent-call contract.

## Further contracts

- `docs/PUBLIC_API_AUDIT.md` records the current source/error/resource review
  and its exact-candidate recheck boundary.
- `docs/support-profile-0.1.0.json` is the machine-checked standalone-library
  boundary. It classifies every conformance row and explicitly excludes the
  optional viewer and PFI adapter.
- `docs/RESOURCE_LIMITS.md` defines the limit model.
- `docs/THREAT_MODEL.md` defines the security boundary.
- `docs/PFI_INTEGRATION.md` defines the narrow PFI adapter contract.
- `docs/ROW_READER.md` defines bounded row delivery and partial-result rules.
- `docs/WRITER.md` defines the current writer profile.
- `docs/conformance/xisf-1.0-matrix.json` is the versioned conformance matrix.
