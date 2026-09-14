# ADR 0013: validated ancillary core-object inspection

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-14
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

XISF 1.0 sections 11.8 through 11.11 define four attribute-based ancillary
objects associated with images: `RGBWorkingSpace`, `DisplayFunction`,
`ColorFilterArray`, and `Resolution`. The reader previously recognized their
core names and unique identifiers, but discarded their parameters and did not
expose direct or `Reference`-based image associations. This was insufficient
for the conformance matrix's inspect-only claim and could hide scientifically
relevant producer context.

`ICCProfile` and `Thumbnail` are not part of this slice. Both own image-like or
arbitrary binary data blocks and need explicit block, inline/embedded content,
and decode-policy work rather than an attribute-only shortcut.

## Decision

`Document::ancillary_objects()` exposes the four selected object families in
document order as `AncillaryObject` records. The records preserve the object
kind, optional `uid`, direct containing-image provenance, and every
namespace-aware XML attribute as exact XML-decoded text.

The parser validates direct-child placement, required fields, finite numeric
syntax, RGB normalized three-component vectors, five four-component display
vectors, CFA dimensions/pattern alphabet and cardinality, positive resolution,
and `inch`/`cm` units. It does not synthesize sRGB, identity-display, or 72-dpi
objects when elements are absent, and it does not replace source strings with
parsed floating-point values.

`Document::ancillary_bindings()` records both direct and `Reference`-resolved
image associations. References continue to use the XISF-unit-wide core `uid`
namespace and cannot resolve outside the current document.

Dedicated finite limits bound ancillary object records, attribute records,
binding records, and copied semantic bytes in addition to the general XML
limits.

## Consequences

Inspectors and adapters can disclose the presence, source parameters, and
association of these image-context objects without fabricating defaults or
performing color/display transformations. Numeric convenience views, writer
support, ICC profile bytes, and thumbnail pixels require later, explicit APIs.
