# ADR 0015: bounded Thumbnail image profile

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-14
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

XISF 1.0 section 11.12 defines `Thumbnail` as an image-like core object
associated directly or by `Reference` with a main image. It restricts samples
to UInt8/UInt16, color to Gray/RGB, permits at most one alpha channel, forbids
`bounds`, CFA children, and nested thumbnails, and recommends dimensions no
larger than 1024 pixels. Discarding these objects prevents callers from
presenting fast producer-authored previews or auditing their association.

## Decision

`Document::thumbnails()` exposes ordered `ThumbnailInfo` records containing the
source raster descriptor and provenance. `Document::thumbnail_bindings()`
records direct and `Reference`-resolved associations with main images.

The parser requires exactly two dimensions plus channels, UInt8 or UInt16,
Gray with one/two channels or RGB with three/four channels, no `bounds`, and an
attachment, embedded, or inspect-only external location. Inline locations,
nested thumbnails, CFA children, and thumbnail-to-thumbnail references fail
closed. A configurable 4096-pixel per-axis default treats the specification's
1024-pixel recommendation as a recommendation while retaining a finite policy.

`Reader::read_thumbnail()` returns exact source-order and source-layout pixels
for local attachment or embedded blocks. It reuses checksum-before-
decompression and bounded codecs. It performs no resampling, orientation,
color, alpha, endian, or ICC transformation.

## Consequences

Consumers can choose whether and how to display a thumbnail without confusing
it with scientific image data. Dedicated count, binding, dimension,
serialized-byte, and decoded-byte limits isolate thumbnails from main-image
budgets. External resolution, output-layout conversion, thumbnail writing, and
automatic preview selection remain future decisions.
