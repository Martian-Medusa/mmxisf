# M4 progress: PFI metadata fidelity and interoperability

- Status: IN_PROGRESS
- Started: 2026-09-13
- Specification baseline: pinned XISF 1.0 sections 8.4, 11.1, 11.6, and 11.13
- Publication status: private repository; no tag or release

## First implementation slice

- Metadata now distinguishes XISF-unit, direct-image, and standalone lexical
  scope.
- Attribute, direct character-data, and data-block property forms are explicit.
- `uid`, `format`, `comment`, raw block location, and vector/matrix extents are
  preserved in the public document model.
- FITS keyword values and comments remain exact and unnormalized for the PFI
  adapter boundary.
- The inspector and macOS viewer expose scope and value form; block-backed
  properties show their location instead of being presented as decoded text.
- Malformed numeric property extents and malformed block locations fail closed.

## Evidence available now

- Synthetic direct-image scalar Property with value, format, comment, and uid:
  PASS locally.
- Synthetic quoted FITS value and comment preservation: PASS locally.
- Synthetic standalone inline vector Property with length and block location:
  PASS locally.
- XISF-unit String character data and lexical scope: PASS locally.
- Malformed vector extent rejection: PASS locally.
- Release unit suite and macOS viewer build: PASS locally.

## Still required for M4

- Complete type-specific Property grammar for scalar, complex, String,
  TimePoint, vector, and matrix serializations.
- Resolve `Reference` associations without losing their original lexical
  provenance.
- Define and test the PFI-facing metadata projection, including conservative
  FITS raw/stripped handling and duplicate-keyword order.
- Exercise the metadata parity matrix against independently produced files and
  the local non-redistributed PFI corpus.
- Record an explicit PFI multi-image accept/reject result for each input while
  the standalone library continues to enumerate all images.
