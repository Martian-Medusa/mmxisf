# ADR 0007: explicit image semantics and decode provenance

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-13
- SemVer impact: additive public API change; minor-version candidate before the
  first public release, no ABI compatibility promise

## Context

PFI and other scientific consumers must distinguish the serialized pixel
coordinate convention from an optional display orientation. They also need a
library-provided channel-order contract and an unambiguous indication that a
declared checksum was verified before an owning decoded image was returned.
Inferring these facts from strings in each consumer would duplicate format
logic and invite silent coordinate or provenance errors.

XISF defines the serialized two-dimensional origin as top-left and stores pixel
coordinates from top to bottom and left to right. This naming convention does
not by itself assert the physical orientation of the represented image. The
optional `orientation` attribute describes a display transformation and must
not be applied automatically for image-processing tasks that depend on the
physical disposition of pixels.

## Decision

- `ImageInfo` exposes fixed serialized `PixelOrigin` and `PixelTraversal`
  values, plus a `NominalChannelOrder` derived from the validated `colorSpace`.
- The exact eight XISF 1.0 `orientation` values map to a closed
  `ImageOrientation` enum. Absence remains `std::nullopt`, distinct from the
  explicitly declared identity value `0`.
- `Reader::read_image` copies those descriptors into `RawImage` but never
  rotates, flips, color-converts, normalizes, or otherwise changes samples for
  display.
- `RawImage::checksum_verification` is `verified` only when the source image
  declared a supported checksum and the read completed after successful
  verification. It is `not_declared` otherwise. A mismatch or unsupported
  declaration returns an error and therefore never yields a `RawImage`.
- `read_image_into` retains its allocation-free result shape. A successful call
  combined with the inspected `ImageInfo::checksum` communicates the same
  fail-closed verification rule without adding a second result object.

## Consequences

PFI can consume coordinate, channel, orientation, and checksum semantics without
parsing XISF strings or applying a visual transform to scientific data. The
additive fields change aggregate layout and are therefore not an ABI guarantee;
the project remains pre-1.0. A future display helper may apply orientation only
through an explicit opt-in API.
