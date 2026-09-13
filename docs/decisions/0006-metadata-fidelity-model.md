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

An ordered `MetadataBinding` sequence represents each direct or referenced
association with an XISF unit or image. It stores the metadata index, target
scope/image, and whether the occurrence came from a `Reference`. Repeated
references remain repeated and direct/reference occurrences remain in source
order, which is required for FITS keyword lists.

The existing `value` member remains the exact XML-decoded attribute or direct
character data for the supported forms. A block-backed property is identified
explicitly and is not presented as a decoded property value. FITS keyword
values remain in their source-compatible representation, including quotes;
FITS stripping and PFI-specific parsing are adapter responsibilities.

Reference resolution does not rewrite the entry's lexical scope: a standalone
metadata element with a `uid` remains standalone while its binding records each
resolved association. References to non-metadata core elements remain part of
the general XISF graph and do not generate metadata bindings. Decoding of
block-backed property values is a separate increment.

## Consequences

Consumers can distinguish absence, emptiness, storage form, and scope without
heuristics. The metadata viewer can show block-backed values as locations
instead of misleading text and can label referenced rows. The public model
grows before ABI stability; this is permitted before 1.0 but is recorded
explicitly here.
