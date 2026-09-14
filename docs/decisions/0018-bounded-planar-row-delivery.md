# ADR 0018: bounded planar row delivery

- Status: Accepted
- Date: 2026-09-14
- SemVer impact: additive pre-1.0 source API; no ABI stability promise

## Context

PFI and other analysis consumers should not have to allocate a complete decoded
image merely to process it row by row. The existing owning and caller-buffer
APIs remain useful when random pixel access or a storage-layout conversion is
required, but they cannot express bounded transient ownership. A streaming API
also needs an explicit integrity and partial-delivery contract: a callback
cannot be rolled back by the library after it has consumed a row.

## Decision

Add `Reader::read_image_rows` with a caller-owned `ImageRowSink`. Every callback
receives one ephemeral planar channel row identified by exact channel and row
indices. Planar sources are visited channel-major. Normal/interleaved sources
are visited source-row-major and split into channel rows, allowing both storage
modes to reach the same PFI-friendly planar callback shape without retaining a
complete frame. Source or native byte order is selected per call; sample type,
precision, channel order, traversal, and orientation semantics are unchanged.

The caller sets independent maximum source-row/output-row and compression-
subblock staging sizes. Uncompressed blocks require row-sized staging only.
Compressed blocks are decoded one declared subblock at a time and rows spanning
subblocks are assembled in a bounded row buffer. Byte-shuffled input may use a
compressed buffer, decoded buffer, shuffle buffer, and row buffers, each
independently bounded; the API makes no single-buffer peak-memory claim.

If a checksum is declared, the complete serialized block is verified through a
bounded first pass before the first callback. The exact serialized bytes read
during delivery are hashed again and must match before a successful summary is
returned, closing the two-pass source-mutation gap. An initial checksum mismatch
yields no pixels. A later source mutation, codec or source failure discovered
after prior rows, cancellation, or a sink error can still leave consumer-owned
partial state. The sink owns transactionality and rollback, and row bytes must
not be retained beyond the callback without copying.

## Consequences

Large PFI-supported images can be analyzed with memory proportional to a row
and declared compression subblock rather than the decoded frame. Checksummed
attached blocks are read twice by design: once for integrity and once for row
delivery. This favors a clear trust boundary and bounded memory over minimum
I/O. The existing `read_image` and `read_image_into` APIs remain unchanged for
owning, random-access, or arbitrary Planar/Normal output needs.

The first API deliberately emits whole rows rather than arbitrary tiles. Tile
reordering, parallel callbacks, asynchronous delivery, numeric conversion,
display orientation, and retaining callback spans are outside this contract.
