# ADR 0008: deterministic writer foundation API

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-14
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

The reader and metadata model are mature enough to start the monolithic writer
without coupling it to PFI. Deterministic output requires callers to supply
volatile provenance explicitly, and untrusted dimensions require the same
checked-arithmetic and resource-limit posture as reading.

The first writer slice must be narrow enough to validate layout and external
consumer behavior before compression, arbitrary metadata, or multiple images
expand the serialization surface.

## Decision

Add a standalone `Writer::write_file` API over PFI-independent records:

- `ImageWriteView` owns descriptors and borrows an immutable pixel span for the
  duration of the call;
- `WriterOptions` requires a fixed canonical UTC creation time and explicit
  creator application, plus finite header/image budgets and attachment
  alignment;
- `WriteSummary` returns exact file/header/block layout;
- the foundation accepts exactly one attached little-endian Planar UInt16 Gray
  or RGB image;
- geometry, color/channel agreement, pixel byte count, alignment, XML text,
  arithmetic, and resource budgets are validated before file creation;
- output is written to a sibling temporary path and renamed only after all
  bytes flush successfully; existing destinations or stale temporary paths are
  never overwritten;
- cancellation removes an incomplete temporary file and never yields a
  successful summary.

Canonical XML attribute order, fixed zero padding, explicit descriptors, and a
fixed provenance input make equivalent writes byte-identical. This is a source
API addition before ABI stability; the project version is not promoted until a
release gate is accepted.

## Consequences

The first slice establishes deterministic monolithic block planning and exact
Gray/RGB round trips without claiming a general writer. A first independent
consumer preserved little-endian output but did not honor a big-endian writer
probe, so big-endian output fails explicitly until broader external evidence is
available. Compression, checksums, multiple images, arbitrary metadata,
caller-provided sinks, and replacement policy are follow-up gates. Unsupported
requests fail explicitly instead of being coerced into the narrow profile.
