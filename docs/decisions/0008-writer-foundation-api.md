# ADR 0008: deterministic writer foundation API

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-14
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

The reader and metadata model are mature enough to start the monolithic writer
without coupling it to PFI. Deterministic output requires callers to supply
volatile provenance explicitly, and untrusted dimensions require the same
checked-arithmetic and resource-limit posture as reading.

The first writer slice was deliberately narrow enough to validate layout and
external consumer behavior before multiple images expanded the serialization
surface. That evidence now supports the additive second slice below.

## Decision

Add a standalone `Writer::write_file` API over PFI-independent records:

- `ImageWriteView` owns descriptors and borrows an immutable pixel span for the
  duration of the call;
- `WriterOptions` requires a fixed canonical UTC creation time and explicit
  creator application, plus finite header/image budgets and attachment
  alignment;
- `WriteSummary` returns exact file/header/block layout;
- the single-image overload remains source-compatible and an additive span
  overload accepts one or more images;
- attached output accepts little-endian Planar UInt8, UInt16, UInt32, Float32,
  or Float64 Gray/RGB images; floats require finite increasing bounds;
- image count, per-image bytes, and cumulative image bytes have independent
  caller-configurable limits;
- a declarative metadata record supports direct image String/TimePoint
  Properties and FITS keywords plus XISF-unit String/TimePoint Properties;
- the same record supports direct Boolean, bounded signed/unsigned integer, and
  real floating-point Properties while preserving valid lexical forms;
- metadata identifiers, scope, uniqueness, value budgets, time syntax, and XML
  text are validated before file creation; callers cannot inject raw XML;
- per-image zlib, LZ4, LZ4HC, or Zstandard compression can apply XISF byte
  shuffle before compression, and SHA-1/256/512 checksums cover the serialized
  bytes that are written;
- large compressed images use deterministic, sample-aligned subblocks with
  caller-configurable byte/count limits, per-subblock shuffle scratch, and
  cancellation checkpoints;
- multi-subblock serialized bytes use cleanup-guarded sibling spool files, so
  block planning does not require a second full-image in-memory buffer;
- decoded and serialized bytes have independent per-image and cumulative
  resource limits;
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

The first two slices establish deterministic monolithic block planning and exact
multi-image PFI-scalar Gray/RGB round trips without claiming a general writer.
The original single-image byte hash remains stable. An independent consumer
preserved all four types in the multi-image oracle but did not honor a
big-endian writer probe, so big-endian output fails explicitly until broader
external evidence is available. Compressed output is deterministic for a fixed
codec/dependency set, but dependency upgrades can change a valid compressed byte
stream; decoded pixel identity remains the cross-version oracle. Numeric and
block-backed Properties, references, caller-provided sinks, and replacement
policy are follow-up gates. Unsupported requests fail explicitly instead of
being coerced into the narrow profile.

The independent `xisf` 0.9.7 consumer enumerates writer subblock descriptors
but does not assemble them in its public image API. This is a third-party
consumer limitation observed by black-box testing, not evidence to remove a
standard XISF feature. Subblock output therefore remains gated on native
PixInsight interoperability before release.
