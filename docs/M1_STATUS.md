# M1 checkpoint: parser, inspector, and macOS viewer PoC

- Started: 2026-09-13
- Checkpoint: 2026-09-13
- Status: LOCAL_MACOS_PASS / CROSS_PLATFORM_IN_PROGRESS
- Library version: 0.1.0 (pre-release API)

## Implemented at this checkpoint

- Bounded `XISF0100` preamble and XML-header reader.
- Strict namespace-aware Expat SAX boundary with chunked UTF-8/well-formedness
  checks, DOCTYPE rejection, and explicit header/depth/node/attribute/metadata
  limits.
- Fail-closed signature, reserved-byte, header-range, attachment-range,
  geometry, byte-order, and pixel-storage validation.
- Immutable document-facing model for image descriptors, Property entries, and
  FITS keywords with document/image scope.
- Structured `Result<T>` / `Error` API without third-party public types.
- Public seekable `ByteSource` boundary with a stable, mutex-protected file
  handle implementation and correct handling of partial reads.
- Caller-owned `read_image_into()` path plus bounded 8 MiB read chunks and
  cooperative `std::stop_token` cancellation boundaries.
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
- Synthetic reader coverage (63 checks): valid preamble/XML/metadata/raw pixels, DOCTYPE
  rejection, invalid root/namespace/core grammar, mandatory attributes,
  preamble and block ranges, every exposed resource-limit class, cancellation,
  caller buffers, partial ByteSource reads, and fail-closed compressed decode.
- Pinned-spec spot audit: PASS for Image geometry/channel semantics, optional
  `colorSpace="Gray"` default, case-sensitive `Planar`/`Normal` storage values,
  floating-point bounds, mandatory Metadata properties, Metadata uniqueness,
  and FITSKeyword placement.
- Interoperability exception: the private PixInsight corpus declares
  `XISF:CreationTime` as `String` instead of the specified `TimePoint`; both
  declarations are accepted without coercion and the discrepancy is recorded
  in `SPECIFICATION_NOTES.md`.
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
- Deterministic ASan/UBSan mutation smoke: 20,000 header cases PASS.
- Independent installed-package consumer using `find_package(mmxisf 0.1)`:
  1/1 PASS locally; the same gate is wired into every CI platform job.
- M2 entry API review: PASS for a pre-release spike. Image descriptors retain
  geometry/channel semantics, numeric bounds, raw block/compression/checksum
  declarations, and exact byte delivery without UI or Expat types.
- Full coverage-guided libFuzzer harness: IMPLEMENTED, but NOT_TESTED locally;
  Apple Command Line Tools on this host lacks `libclang_rt.fuzzer_osx.a`.
- Linux/macOS/Windows dependency-aware CI and Linux sanitizer-smoke jobs:
  ACTIVE in the private GitHub repository.

### Cross-platform CI attempt 1

GitHub Actions run `34769084877` on commit `5a1c71c` provided the first external
platform evidence:

- Ubuntu build, tests, install, and installed-package consumer: PASS.
- Linux Clang ASan/UBSan 20,000-case mutation smoke: PASS.
- macOS 14 / Xcode 15.4 build: FAIL because that standard library lacks
  `std::stop_token` and floating-point `std::from_chars`.
- Windows build: PASS; reader test: FAIL at process teardown with an open-file
  cleanup failure consistent with Windows file-sharing semantics.

The follow-up changes select the current macOS/Xcode baseline, remove the
floating-point `from_chars` dependency, and defer fixture deletion until all
stable reader handles have been released. This failed attempt remains recorded
separately and is not counted as cross-platform PASS.

### Cross-platform CI attempt 2

GitHub Actions run `34769307609` on commit `73b41fa` verified the portability
fixes separately:

- Ubuntu build, tests, install, and installed-package consumer: PASS.
- Linux Clang ASan/UBSan 20,000-case mutation smoke: PASS.
- Windows build, tests, install, and installed-package consumer: PASS.
- macOS 15 build: FAIL because the runner's default Xcode 16.4 standard library
  does not provide `std::stop_token`.

The follow-up selects the runner's installed Xcode 26.3 explicitly. Attempt 2
remains partial evidence and is not counted as cross-platform PASS.

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

- Complete table-driven boundary coverage for remaining XML text, attachment,
  and checked-arithmetic combinations.
- Continue XISF grammar validation beyond the implemented root, Metadata,
  Image, Property, and FITSKeyword placement/mandatory-attribute rules.
- Run the prepared coverage-guided fuzz target on a Clang toolchain that ships
  libFuzzer and retain a minimized regression corpus for every finding.
- Run the prepared CI and verify build/package consumption on Linux and Windows
  before claiming M1 as cross-platform complete.
- Revisit the source contract after the first codec/checksum implementation;
  do not freeze ABI or 0.1 API names before that evidence.

The macOS viewer brought forward a narrow raw-attachment slice from M2. It does
not change the clean core boundary and should add little roadmap cost; turning
the PoC into a distributable multi-format viewer would be a separate product
scope.

## Second implementation checkpoint

The continuation pass corrected the `geometry` contract: the final item is the
channel count, so a 1-D image can no longer be mistaken for a 2-D frame. It also
removed path reopen behavior, added the public source/buffer/cancellation
boundaries, tightened core-element placement and mandatory attributes, and
established both a libFuzzer entry point and a locally executable sanitizer
mutation smoke.
Floating-point bounds are now parsed as finite increasing ranges, including
valid signed/whitespace forms, and propagated to the viewer's linear display
range. The same spec audit corrected the optional Gray color-space default and
the canonical case-sensitive pixel-storage literals.
