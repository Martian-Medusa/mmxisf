# ADR 0014: bounded ICC profile block inspection and decode

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-14
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

XISF 1.0 section 11.7 serializes ICC color profiles as unaltered XISF data
blocks. Profiles can be direct children of an image or standalone root objects
shared with images through `uid` and `Reference`. They are always big-endian,
must not declare `byteOrder`, and can use the generic compression and checksum
descriptors. Generic attachment-range inventory did not expose this semantic
association or provide a safe byte-reading API.

## Decision

`Document::icc_profiles()` exposes ordered `IccProfileInfo` descriptors and
`Document::icc_profile_bindings()` exposes direct and `Reference`-resolved
image associations. The reader accepts attachment and inline Base64/Base16
profiles. External locations remain inspectable but are never resolved.

`Reader::read_icc_profile()` returns exact decompressed bytes without endian or
color transformation. It uses the shared checksum-before-decompression and
bounded codec pipeline. Dedicated count, binding, serialized-byte, and
decoded-byte limits are independent from image and Property limits.

After decoding, the reader checks the 128-byte ICC header boundary, its
big-endian declared size, the `acsp` signature, and the XISF-required embedded
profile flag. This is structural screening, not full ICC tag-table validation,
profile authentication, or color-management execution.

## Consequences

Inspectors and adapters can disclose which profile belongs to which image and
can retrieve integrity-checked bytes without invoking platform color services.
The macOS viewer lists descriptors and associations but does not apply the
profile to scientific pixels. Full ICC semantic validation, color transforms,
writer support, and external-resource resolution remain outside this decision.
