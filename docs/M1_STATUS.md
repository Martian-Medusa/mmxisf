# M1 checkpoint: parser, inspector, and macOS viewer PoC

- Started: 2026-09-13
- Checkpoint: 2026-09-13
- Status: IN_PROGRESS
- Library version: 0.1.0 (pre-release API)

## Implemented at this checkpoint

- Bounded `XISF0100` preamble and XML-header reader.
- Strict namespace-aware Expat SAX boundary with UTF-8/well-formedness checks,
  DOCTYPE rejection, and explicit header/depth/node/attribute/metadata limits.
- Fail-closed signature, reserved-byte, header-range, attachment-range,
  geometry, byte-order, and pixel-storage validation.
- Immutable document-facing model for image descriptors, Property entries, and
  FITS keywords with document/image scope.
- Structured `Result<T>` / `Error` API without third-party public types.
- `mmxisf-inspect` metadata-only command-line tool.
- Exact raw reads for uncompressed local Image attachments in the narrow PoC
  profile: UInt8, UInt16, Float32; 2-D Gray is rendered by the viewer.
- Optional native AppKit application with file-open integration, a metadata
  table, robust-percentile auto stretch, and a linear-to-strong stretch slider.
- Self-contained macOS application bundle with embedded Expat and ad-hoc local
  signing. AppKit remains outside the library target.

## Evidence

- CMake Release build: PASS.
- CTest: 3/3 PASS (`version`, `reader`, planning consistency).
- Synthetic reader coverage: valid preamble/XML/metadata/raw pixels, DOCTYPE
  rejection, invalid root rejection, and fail-closed compressed decode.
- Local private corpus metadata inspection: 9/9 files PASS, including one
  three-image unit and a 3.4 MiB XML header.
- Native launch/open/render: PASS on a private 6248 x 4176, Float32, Gray,
  uncompressed attachment; 129 metadata entries enumerated; 100% auto stretch
  visibly renders the linear frame.
- Bundle dependency inspection: Expat resolves through
  `@rpath/libexpat.1.dylib` inside the application.
- Strict code-signature verification: PASS for the ad-hoc signed local bundle.
- Warning-clean build: PASS with `-Wall -Wextra -Wpedantic -Werror` for C++ and
  Objective-C++ targets.
- AddressSanitizer + UndefinedBehaviorSanitizer unit run: 3/3 PASS.

Private astronomy files were read locally and were not copied into this
repository or its generated bundle.

## Deliberate PoC limits

- Viewer renders the first image only and only one-channel Gray data.
- No compression, shuffle, or checksum verification yet; decode fails closed.
- Embedded, inline Image data, external units, and network resources are not
  decoded.
- Metadata values are preserved by the core up to the configured bound; the UI
  truncates only its display of very large values.
- The bundle is arm64/local, ad-hoc signed, and not notarized for public
  distribution.
- The reader API is pre-1.0 and has no ABI stability promise.

## Remaining M1 closure work

- Expand negative/boundary tests across every configurable parser limit.
- Add explicit XISF parent/child grammar validation beyond the current root and
  Image constraints.
- Complete seekable `ByteSource` and cancellation boundaries from ADR 0004.
- Add sanitizer CI, expand the malformed-header corpus, and start a parser fuzz
  target.
- Verify build/package consumption on Linux and Windows before claiming M1 as
  cross-platform complete.
- Review the pre-1.0 model/API against the M2 decoder needs before freezing the
  0.1 source contract.

The macOS viewer brought forward a narrow raw-attachment slice from M2. It does
not change the clean core boundary and should add little roadmap cost; turning
the PoC into a distributable multi-format viewer would be a separate product
scope.
