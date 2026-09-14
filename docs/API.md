# mmxisf C++ API reference

This reference is generated from the installed public headers for mmxisf
0.1.0. The API is pre-release: source compatibility follows SemVer after the
first public release, while ABI stability is not promised before 1.0.

## Core workflow

1. Open a seekable source with `mmxisf::Reader::open_file` or
   `mmxisf::Reader::open_source` and explicit `mmxisf::ReaderOptions` limits.
2. Inspect the immutable `mmxisf::Document` without decoding pixels.
3. Read an image into an owning `mmxisf::RawImage` or a correctly sized
   caller-owned span. Use `mmxisf::ImageReadOptions` when native byte order or a
   specific Planar/Normal layout is required.
4. Read block-backed metadata with `mmxisf::Reader::read_property_block`.
5. Write deterministic local monolithic files with
   `mmxisf::Writer::write_file` and explicit `mmxisf::WriterOptions` budgets.

Every fallible operation returns `mmxisf::Result<T>`. Test the result before
calling `value()` or `error()`; those accessors follow `std::variant` semantics
and do not silently manufacture a fallback value.

## Ownership and lifetime

- `mmxisf::Reader` owns its parsed document and is move-only.
- `Reader::open_source` retains a shared pointer to the supplied
  `mmxisf::ByteSource`; the source must honor random-access read semantics for
  the reader's lifetime.
- `mmxisf::RawImage` and `mmxisf::RawPropertyBlock` own their returned bytes.
- `Reader::read_image_into` writes only to the supplied span and reports the
  exact decoded byte count.
- Writer image and Property spans are borrowed only for the duration of the
  synchronous `Writer::write_file` call.
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

The writer accepts a deliberately bounded profile documented in
`docs/WRITER.md`. It rejects unsupported layouts and does not overwrite an
existing destination. Output is committed only after successful flush; failed
or cancelled writes remove their incomplete temporary files.

## Resource and trust boundary

XISF input is untrusted. `mmxisf::ReaderOptions` and
`mmxisf::WriterOptions` have finite defaults and are part of the correctness
contract, not performance hints. Applications may tighten them for their own
workloads. A resource-limit failure, checksum mismatch, unsupported feature,
or malformed structure is reported explicitly and yields no trusted pixels.
External paths and URLs are not resolved by the current profile.

## Cancellation and concurrency

Decode and write calls accept `std::stop_token` and check it at bounded work
boundaries. Cancellation is cooperative and returns
`mmxisf::ErrorCode::cancelled`.

Parsed documents are immutable. Read operations use per-call state and may run
concurrently when the supplied `ByteSource` supports concurrent calls. The
built-in file source serializes access to its stable file handle. Concurrent
writes to the same destination are intentionally rejected by the no-overwrite
contract.

## Further contracts

- `docs/RESOURCE_LIMITS.md` defines the limit model.
- `docs/THREAT_MODEL.md` defines the security boundary.
- `docs/PFI_INTEGRATION.md` defines the narrow PFI adapter contract.
- `docs/WRITER.md` defines the current writer profile.
- `docs/conformance/xisf-1.0-matrix.json` is the versioned conformance matrix.
