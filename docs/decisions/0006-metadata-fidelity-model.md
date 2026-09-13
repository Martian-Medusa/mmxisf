# ADR 0006: preserve metadata serialization and lexical scope

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-13
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

PFI consumes both XISF properties and FITS compatibility keywords. A flat
name/type/value tuple is insufficient because an absent value attribute, an
empty value, character data, and a referenced data block are distinct XISF
serializations. The lexical owner also matters: unit metadata, direct image
metadata, and standalone objects cannot be conflated before `Reference`
associations are resolved.

## Decision

`MetadataEntry` preserves, without application interpretation:

- the element kind and lexical scope (`XISF unit`, `Image`, or standalone);
- the direct image index when the element is an `Image` child;
- `uid`, identifier/name, declared type, value, comment, and format text;
- whether the value came from an attribute, character data, or a data block;
- the raw block location and optional vector/matrix extents.

The existing `value` member remains the exact XML-decoded attribute or direct
character data for the supported forms. A block-backed property is identified
explicitly and is not presented as a decoded property value. FITS keyword
values remain in their source-compatible representation, including quotes;
FITS stripping and PFI-specific parsing are adapter responsibilities.

Reference resolution and decoding of block-backed property values are separate
increments. Until those increments land, standalone metadata with a `uid` is
not silently treated as image metadata merely because a matching `Reference`
exists elsewhere.

## Consequences

Consumers can distinguish absence, emptiness, storage form, and scope without
heuristics. The metadata viewer can show block-backed values as locations
instead of misleading text. The public struct grows before ABI stability; this
is permitted before 1.0 but is recorded explicitly here.
