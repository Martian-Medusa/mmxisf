# M2 progress: PFI scalar pixels and RGB preview

- Status: IMPLEMENTATION_COMPLETE; INTEROPERABILITY_ACCEPTANCE_PENDING
- Started: 2026-09-13
- Specification baseline: pinned XISF 1.0 sections 8.5, 10.3, 10.4 and 11.5
- Publication status: private repository; no tag or release

## Implemented in the first M2 slice

- The document model represents every standard XISF sample format without
  collapsing valid but unsupported formats into an unknown value.
- Exact uncompressed attachment reads cover the PFI scalar set: UInt8, UInt16,
  UInt32, Float32 and Float64.
- UInt64 and complex formats remain inspectable but decoding fails explicitly;
  CIELab remains inspectable but conversion is outside M2.
- RGB images require at least three nominal channels. Additional alpha channels
  are preserved and ignored by the preview.
- The reader preserves the serialized byte order and Planar/Normal layout; it
  performs no silent conversion or precision loss.
- Callers can explicitly request native-endian and Planar or Normal output.
  Combined endian/layout transformation preserves the scalar type and uses a
  bounded 8 MiB staging buffer for attachments; caller-owned delivery writes
  directly to the requested destination representation.
- Embedded Image blocks decode incrementally from whitespace-tolerant Base64 or
  lowercase hexadecimal `Data` content. Invalid characters, incomplete or
  noncanonical padding, duplicate/missing Data children, nested elements, and
  text outside Data fail closed.
- All declared attachment ranges are inventoried, sorted, checked for bounds
  and overlap, and excluded from bounded zero-filled unused-space validation.
  Standard attachment syntax on extension elements participates in the same
  inventory.
- The macOS PoC viewer renders Gray and RGB attachments in both Planar and
  Normal layouts, interprets little- and big-endian scalar samples, and derives
  auto-stretch statistics from all nominal RGB channels.
- Missing integer `bounds` remain missing in the document model. The viewer
  applies the specification's default representable range only in its display
  calculation.

## Evidence available now

- Warning-clean Release build: PASS locally on macOS.
- Unit suite: 4/4 PASS, including a pure C++ preview test for Planar and Normal
  RGB channel indexing, big-endian UInt32, little-endian Float64, and RGB
  stretch sampling.
- Hand-derived bitwise scalar oracle: PASS for UInt8, UInt16, UInt32, Float32,
  and Float64 source-to-native conversion, including preservation of the exact
  IEEE-754 byte patterns.
- Existing private PixInsight corpus: 9/9 files and 11/11 uncompressed Float32
  Gray image attachments decode successfully with exact declared byte counts.
- Deterministic ASan/UBSan mutation smoke: 20,000 cases PASS.
- ASan/UBSan unit suite: 4/4 PASS.
- Independent installed-package consumer: 1/1 PASS locally.
- First M2 scalar/RGB/metadata slice cross-platform CI: PASS in run
  `34772859588` on Linux, Windows, and macOS, including the installed consumer,
  20,000 deterministic sanitizer mutations, and 20,000 coverage-guided
  libFuzzer runs.
- Complete M2 change-set cross-platform CI: PASS in run `34774526481` on
  Linux, Windows, and macOS, including installed-package consumers, 20,000
  deterministic sanitizer mutations, and 20,000 coverage-guided mutations.
- Strict ad-hoc bundle signature validation: PASS.
- Embedded Expat dependency resolution through `@rpath`: PASS.
- Human launch/open/render test of a real RGB producer file: PASS. The exact
  producer settings are not yet captured, so this is compatibility evidence,
  not a claim for every RGB storage/sample combination.
- Metadata viewer layout: a permanently visible, titled right-hand pane now
  exposes document summaries, image descriptors, XISF Properties, and FITS
  keywords with column headers, row count, scrolling, selectable cells, and
  full-value tooltips.
- Human visibility/readability test of the revised metadata pane: PASS.

## Still required to close M2

- Independent or producer-generated fixtures for every claimed scalar,
  storage, byte-order, and color combination.
- Full-precision parity evidence against an independent pixel oracle.
M3 work can proceed while the two independent-producer acceptance items remain
open. This status does not promote synthetic evidence into interoperability
evidence.
