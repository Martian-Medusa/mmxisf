# ADR 0016: bounded Structure and Table inspection

- Status: Accepted
- Date: 2026-09-14
- SemVer impact: additive pre-1.0 source API; no ABI stability promise

## Context

XISF 1.0 sections 8.4.4.7, 11.2, and 11.3 define heterogeneous table
properties through `Structure`/`Field` schemas and ordered `Table`/`Row`/`Cell`
data. Tables can be image children or standalone objects referenced by images.
Their unbounded heterogeneous shape and Cell block forms need an explicit trust
boundary before public inspection can be claimed.

## Decision

Expose immutable ordered `TableStructureInfo`, `TableFieldInfo`, `TableInfo`,
`TableRowInfo`, and `TableCellInfo` records plus direct/reference-aware image
bindings. Resolve a Table's `Reference` only to a root standalone `Structure`;
inline Structures remain private to their owning Table.

Validate required placement and identifiers, non-Table field types, unique
field ids, exactly one inline or referenced Structure, optional declared shape,
row width, scalar and TimePoint syntax, and vector/matrix Cell extent forms.
Preserve XML-decoded character text and inline/external/attachment block
descriptors without numeric coercion. Accept String Cells serialized with a
`value` attribute as the narrow specification-example compatibility exception
recorded in SN-005; retain the original form so consumers can distinguish it.

Use independent finite limits for structures, tables, fields, rows, cells,
bindings, and cumulative copied text. General XML and attachment-range checks
remain authoritative.

## Consequences

Consumers can inventory table metadata and safely decide whether a table is
relevant without executing external I/O or interpreting heterogeneous values.
Cell block payload decoding, checksum/decompression claims for those payloads,
writer support, and typed convenience accessors remain deferred. PFI does not
project table values into its scientific metadata contract.
