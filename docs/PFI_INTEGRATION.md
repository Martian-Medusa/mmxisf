# PFI integration plan

## Current boundary

PFI currently asks PixInsight `FileFormat`/`FileFormatInstance` for image
descriptions, FITS keywords, and XISF image properties, and uses `ImageWindow`
to open pixels. The C++ audit already requires image loaders to remain outside
the pure scientific core. `mmxisf` fits that boundary as an infrastructure
dependency, not a replacement for PFI's measurement model.

## Adapter contract

PFI should own a small adapter that maps library output into its canonical input
types:

```text
mmxisf Document/ImageInfo  -> PFI SourceFrameMetadata
mmxisf pixel plane/view    -> PFI ImagePlaneView
mmxisf diagnostics         -> PFI import diagnostics/provenance
```

The adapter must preserve:

- image count and explicit selection/rejection of multi-image input;
- width, height, channel count, sample type, bounds, and channel order;
- the serialized top-left, top-to-bottom/left-to-right coordinate convention,
  separately from the optional display `orientation` and from FITS/GUI frames;
- XISF property identifiers and values;
- FITS keyword name, raw/stripped value, and comment;
- file identity, checksum result, library version, and decoding options;
- unavailable/invalid/ambiguous states without zero or guessed substitutes.

`ImageInfo` and `RawImage` now carry the serialized origin/traversal, nominal
channel order, and optional orientation as closed enums. The adapter must retain
these values as provenance. It must not apply the optional display transform to
PFI's scientific pixel plane. A returned owning image reports whether a
declared checksum was verified; checksum failure never returns pixels.

## Adoption sequence

1. Add an interface in PFI that both the existing PixInsight host reader and a
   future `mmxisf` reader can implement.
2. Run metadata-only differential tests on the same corpus.
3. Run bitwise pixel-plane comparisons on integer formats and tolerance/bitwise
   tests appropriate to floating storage without applying display transforms.
4. Exercise PSF measurements with identical decoded pixels; differences are an
   I/O defect until proven otherwise, not permission to retune algorithms.
5. Validate separate standalone and PCL builds, lifecycle, cancellation, batch
   throughput, and diagnostics.
6. Enable `mmxisf` by default only after native PixInsight corpus parity and a
   documented rollback gate.

PFI initially supports exactly one image description per input file. The
library should enumerate multiple images correctly, while the PFI adapter keeps
the existing fail-closed policy until PFI intentionally changes it.

## Metadata projection

For the selected image index, the PFI adapter iterates
`Document::metadata_bindings()` in order and selects image-scope bindings with
that exact index. `metadata_index` identifies the immutable source entry.
Repeated FITS bindings remain repeated and in order; a reference is not copied
or rewritten into a synthetic direct entry.

The adapter projects fields as follows:

| PFI field | mmxisf source | Rule |
| --- | --- | --- |
| XISF property id | `MetadataEntry::name` | exact, case-sensitive |
| XISF property value | `MetadataEntry::value` | only attribute or character-data forms accepted initially |
| FITS name | `MetadataEntry::name` | exact validated name |
| FITS raw value | `MetadataEntry::value` | exact XML-decoded attribute text, including quotes |
| FITS stripped value | adapter-derived | apply the existing PFI policy; retain raw value beside it |
| comment | `MetadataEntry::comment` | exact; absence is not fabricated |
| provenance | entry scope plus binding | retain direct/reference and source image index |

Block-backed properties remain explicit but unavailable to the initial PFI
projection until the library implements their typed decoding. An unsupported
form must produce a diagnostic/unavailable state, never an empty substitute.

Before any pixel read, the adapter checks `Document::images().size()`. A count
other than one returns PFI's existing unsupported-multi-image result while the
same file remains fully inspectable through the standalone API and viewer.

## Minimum metadata parity set

The current PFI reader consumes the following XISF properties when present:

- `Observation:Time:Start`
- `Instrument:ExposureTime`
- `Instrument:FrameExposureTime`
- `Instrument:Filter:Name`
- `Instrument:Sensor:Temperature`
- `Observation:Center:Alt`
- `Observation:Center:Az`
- `Observation:Center:HourAngle`
- `Observation:PierSide`

It also consumes selected FITS keywords, including observation time, exposure,
filter, focuser position, sensor temperature, rotator context, altitude,
azimuth, hour angle, and pier side. Mapping semantics remain PFI-owned so the
general library does not acquire application-specific interpretations.

## Non-goals during the switch

- no PSF estimator or scientific result changes;
- no unification of XISF, FITS, detector, GUI, camera, or tilter coordinates;
- no inferred WCS or camera orientation;
- no metadata normalization beyond the existing PFI contract;
- no simultaneous UI redesign or unrelated C++ porting in the I/O parity unit.
