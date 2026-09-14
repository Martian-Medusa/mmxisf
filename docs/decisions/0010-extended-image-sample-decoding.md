# ADR 0010: extended image sample decoding

- Status: accepted
- Date: 2026-09-14
- SemVer impact: additive supported-input behavior; minor-version candidate
  before the first public release; no public type or function signature change

## Context

The document model already preserved all standard XISF image sample-format
identifiers, while decoded images were limited to the scalar PFI profile.
`UInt64`, `Complex32`, and `Complex64` therefore failed before byte delivery
even though the block, codec, checksum, budget, layout, and cancellation
pipelines are representation-preserving and do not interpret numeric values.

Complex byte-order conversion cannot reverse the complete complex sample. It
must reverse the bytes of the real and imaginary scalar components separately.
Treating an eight-byte `Complex32` value like one UInt64 value would swap both
component byte order and component position.

## Decision

The standalone reader decodes the exact serialized bytes for `UInt64`,
`Complex32`, and `Complex64` images through the existing local monolithic
pipeline. Sample sizes are respectively 8, 8, and 16 bytes. Byte shuffle keeps
the complete XISF sample as its item; native-endian conversion uses 8-byte
UInt64 components, 4-byte Complex32 components, and 8-byte Complex64
components.

No numeric conversion, magnitude/phase calculation, normalization, or complex
display mapping is performed. The macOS PoC explicitly reports complex preview
as unavailable while retaining metadata inspection. PFI's adapter continues to
reject complex images because its scientific analysis profile is scalar.

## Consequences

- Existing source-preserving reads are unchanged.
- Native-endian and Planar/Normal transforms now cover all standard XISF image
  sample formats apart from the separately gated CIELab color conversion.
- This expands supported input behavior without changing public declarations;
  it is classified as an additive minor-version candidate before 1.0.
- Synthetic bitwise tests are necessary but do not replace independent
  producer evidence for complex and UInt64 images.
