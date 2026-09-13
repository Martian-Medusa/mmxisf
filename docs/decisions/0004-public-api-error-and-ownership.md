# ADR 0004: public error, ownership, and cancellation contract

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-13

## Context

The library will run inside PFI and unrelated applications, where malformed
input is normal operational data rather than an exceptional programming error.
The API must support lazy inspection, large images, cancellation, and structured
diagnostics without exposing third-party implementation types.

## Decision

- Data and format failures return a move-aware `Result<T>` containing either a
  value or an `Error`; they do not use exceptions as normal control flow.
- `Error` contains a stable category/code plus bounded optional byte offset,
  element/attribute identity, image/block index, and human message. Payload
  bytes and large metadata values are never copied into errors.
- Allocation failures and unexpected dependency exceptions are caught at public
  boundaries and mapped to explicit internal/resource errors where safe.
- `Document` is immutable after parsing and owns normalized descriptors and
  metadata. It does not implicitly own decoded pixel buffers.
- A `Reader` owns or shares an abstract seekable `ByteSource`. Convenience file
  open functions use a stable file handle, not repeated path opens.
- Image reads support caller-owned `std::span<std::byte>` and a bounded chunk
  sink. Typed views preserve sample type; no bounds or color normalization is
  implicit.
- Long operations accept `std::stop_token` and return `cancelled` only at
  documented safe boundaries.
- Third-party parser, codec, and crypto types remain private. ABI stability is
  not promised before 1.0.

## Consequences

Consumers can integrate without exception-policy coupling, and PFI can surface
precise diagnostics. `Result<T>` design and naming may evolve before 1.0, but
the semantic contract requires an ADR to change.
