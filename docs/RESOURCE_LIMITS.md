# Resource-limit policy

Limits are explicit API inputs with finite defaults. An application may tighten
or intentionally raise them, but cannot disable checked arithmetic or structural
validation. Values below are the draft PFI desktop profile for M1/M2 tests.

| Limit | Draft PFI default | Reason |
| --- | ---: | --- |
| XML header bytes | 16 MiB | Far above normal headers while bounding DOM/SAX state |
| XML nesting depth | 64 | Prevent pathological recursion |
| XML nodes | 250,000 | Bound CPU and model construction |
| attributes per element | 256 | Bound attribute scanning and duplicates |
| inspected Property text value | 8 MiB | Preserve large processing history while bounding copies |
| images per unit | 64 | PFI accepts one but must safely enumerate inputs |
| metadata objects and bindings | 100,000 each | Preserve large legitimate metadata sets while bounding repeated references |
| image axes | 8 inspect / 2 decode | Recognize N-D, decode PFI 2-D only |
| channels | 64 inspect / 16 decode | Covers expected mono/RGB/alpha use without unbounded planes |
| samples per image | 536,870,912 | Geometry cap independent of sample size |
| decoded bytes per image | 2 GiB | Covers representative desktop astronomy images |
| serialized bytes per image | 2 GiB | Bounds compressed-input staging and address-space conversion |
| cumulative decoded bytes | 4 GiB | Bounds multi-image and metadata blocks |
| encoded inline/embedded bytes | 256 MiB per block | Avoid huge XML-resident payloads; the lower XML-header limit is also authoritative |
| validated unused file space | 64 MiB cumulative | Bound zero-padding scans during open |
| compressed subblocks | 65,536 | Supports large data while bounding descriptors/tasks |
| decompression ratio | 8,192:1 | Secondary defense; decoded-byte cap remains authoritative |
| diagnostic records | 1,000 | Prevent error amplification |

All byte constants are powers of two. The public API will expose named presets
(`inspection`, `pfi_desktop`, and opt-in `large_image`) plus individual values.
No preset is evidence that the host has sufficient memory.

Before M2 is accepted, measure actual PFI fixtures and revise these values.
Limits that reject a valid file must return a dedicated resource-limit error,
not a generic malformed-file result.

`max_encoded_block_bytes` is enforced on non-whitespace encoded characters.
Decoded embedded bytes are also bounded by `max_decoded_image_bytes`. With the
default profile, the 16 MiB XML-header limit is reached before the larger
per-block encoded ceiling; applications must raise both limits deliberately for
larger embedded payloads.

`max_unused_space_bytes` bounds the cumulative gaps before, between, and after
inventoried attachment ranges. Every scanned byte must be zero. The attachment
inventory includes standard elements and extension elements that use the
standard `attachment:position:size` syntax, preventing legitimate extension
payloads from being mistaken for unused space.
