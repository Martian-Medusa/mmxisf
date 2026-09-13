# Initial local corpus survey

- Survey date: 2026-09-13
- Scope: nine existing XISF files under the local PFI known-issues corpus
- Operation: preamble and XML-header inspection only; no pixel decoding
- Redistribution status: unknown; files remain outside this repository

## Observed ranges

| Property | Observation |
| --- | --- |
| File sizes | about 100-299 MiB |
| Header lengths | 10,834-3,485,162 bytes |
| Image geometry | 6248 x 4176, one channel |
| Sample format | Float32 |
| Pixel storage | Planar default |
| Color space | Gray |
| Pixel compression | none |
| Pixel block bytes | 104,366,592 |
| Images per file | one in eight files; three in one file |
| XISF properties | 7-93 |
| FITS keyword elements | 85-339 |
| Thumbnail | present in one file |

## Planning consequences

- A 16 MiB PFI header limit has more than four times margin over the observed
  maximum, while still being finite.
- A 2 GiB decoded-image limit has substantial margin over the observed frame.
- Multiple-image enumeration is a real local requirement even though PFI may
  continue to reject such input for analysis.
- Metadata-only inspection and lazy pixel reads are valuable; opening the
  header must not allocate a full image.
- No compressed, RGB, normal-storage, big-endian, inline, or embedded pixel
  example exists in this nine-file sample, so those rows remain `NOT_TESTED`.

This is coverage evidence only. It does not authorize committing the files or
claim interoperability.
