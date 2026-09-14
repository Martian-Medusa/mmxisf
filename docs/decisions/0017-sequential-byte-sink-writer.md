# ADR 0017: sequential caller-owned writer sink

- Status: Accepted
- Date: 2026-09-14
- SemVer impact: additive pre-1.0 source API; no ABI stability promise

## Context

The deterministic writer originally owned a filesystem destination and could
provide a strong no-overwrite atomic commit. Embedders also need to target
memory, application-managed storage, or another sequential destination without
making the library depend on a platform I/O type. A generic sink cannot promise
rollback after it has accepted bytes, and multi-subblock compression still
needs bounded temporary storage before the header layout is known.

## Decision

Add a minimal public `ByteSink` with partial-write and explicit flush results,
plus `Writer::write_to` overloads matching the existing image and metadata
profiles. The writer loops until every requested byte is accepted, rejects zero
progress and impossible counts, propagates sink and flush failures, and observes
cancellation between bounded chunks.

The sink is caller-owned and may contain a prefix when any write, flush, or
cancellation failure is returned. Transactionality, rollback, closing, and
durability beyond a successful `flush()` remain caller responsibilities.
`Writer::write_file` continues to own a sibling temporary file and retains its
stronger atomic no-overwrite commit contract.

For a compressed block that spans multiple subblocks, a sink caller supplies an
exact `SinkWriteOptions::scratch_file_stem`. Internal suffixes distinguish image
and Property spools. Existing paths are never overwritten and all writer-owned
spools are removed on success or failure. If no spool is needed, the stem may be
empty and no filesystem access occurs.

## Consequences

PFI and standalone consumers can integrate the same deterministic serializer
with their own storage layer without receiving ownership of an internal stream.
Byte-for-byte equivalence with `write_file` is a compiled contract, including a
multi-subblock compressed case and deliberately short sink writes. Tests also
cover write failure, zero progress, an impossible accepted-byte count, flush
failure, pre-cancellation, missing scratch configuration, stale scratch
protection, and cleanup.

The abstraction is sequential by design. Seeking, asynchronous writes,
concurrent calls on one sink, automatic retry after failure, and transaction
coordination are outside this API.
