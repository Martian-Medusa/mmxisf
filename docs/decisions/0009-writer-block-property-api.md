# ADR 0009: explicit block-backed Property writer records

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-14
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

PFI-compatible astrometric metadata uses typed vectors and matrices that cannot
be represented faithfully as direct XML values. The reader already preserves
their type, extent, byte order, formatting string, block location, and exact
bytes. The writer needs the corresponding operation without accepting raw XML,
performing implicit numeric conversion, or losing ordering relative to direct
Properties and FITS keywords.

## Decision

Extend `MetadataWriteEntry` with an explicit `MetadataWriteValueForm` and
block-only fields. A `data_block` entry:

- must be a supported vector or matrix Property;
- declares exactly one nonzero vector length or one pair of nonzero matrix
  dimensions;
- borrows an immutable byte span only until `write_file` returns;
- treats those bytes as already encoded in the declared little- or big-endian
  element representation and performs no conversion;
- can apply zlib, LZ4, LZ4HC, or Zstandard compression, optional element-width
  byte shuffle, deterministic subblocks, and SHA-1/256/512 integrity using the
  same bounded block pipeline as images;
- rejects a direct value, FITS kind, unsupported type, incomplete extent,
  checked-arithmetic overflow, span mismatch, and resource-limit excess before
  creating a destination;
- is serialized as an attachment after all image attachments, in metadata
  encounter order, with the same deterministic alignment and no-overwrite
  transaction used by images.

The existing metadata overload remains source-compatible because all new
fields are trailing and default to the direct form. `WriteSummary` reports
Property attachment locations in block-entry encounter order. Independent
per-Property and cumulative decoded and serialized byte budgets are public
writer options. Unsupported requests cannot be expressed accidentally through
raw descriptor strings.

## Consequences

PFI adapters can emit exact binary WCS vectors and matrices without a PCL or Qt
dependency. Callers remain responsible for producing correctly typed bytes;
the writer proves extent agreement but does not reinterpret values. Big-endian
Property bytes remain exact in source order and can be normalized explicitly
by the reader. The API is additive but changes pre-1.0 ABI layout, so it is
recorded before public stability is claimed.
