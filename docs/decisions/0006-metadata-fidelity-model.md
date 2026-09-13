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

The reader validates the declared Property category and its structural form,
identifier syntax, FITS keyword-name syntax, and per-association Property
identifier uniqueness. This structural validation does not normalize the
stored representation; lexical numeric/time validation is performed as a
separate check over the same preserved text.

Scalar lexical validation is a bounded linear scan. Boolean values accept the
alphabetic and numeric XISF forms (`true`, `false`, `0`, and `1`). Decimal
integers accept an optional sign and the interoperable zero representation;
binary, octal, and hexadecimal prefixes follow the XISF grammar. Unsigned
decimal properties reject a negative sign. Floating-point values accept the
specified decimal/exponent grammar and the exact `NaN`, `+Inf`, and `-Inf`
tokens, and complex components use that same grammar. Integer magnitudes are
also checked against the exact declared 8-, 16-, 32-, 64-, or 128-bit range
without relying on a platform-specific 128-bit C++ type. Prefixed integer
serializations are bounded as complete two's-complement bit patterns, matching
the signed hexadecimal example in XISF 1.0. Floating-point checks remain
lexical and do not coerce the value to a host floating type.

TimePoint validation accepts the interoperable ISO 8601 extended/RFC 3339
calendar-date profile with optional fractional seconds and optional `Z` or
numeric time-zone offset. A missing zone retains the specification's UTC
default semantics. Calendar dates, clock fields, and offsets are checked while
the original attribute text remains unchanged.

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
