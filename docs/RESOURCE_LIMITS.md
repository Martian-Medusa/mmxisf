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
| serialized bytes per Property block | 256 MiB | Bounds attachment/inline staging independently of image pixels |
| decoded bytes per Property block | 256 MiB | Covers large astrometric arrays while preventing unbounded allocation |
| cumulative decoded bytes | 4 GiB | Bounds multi-image and metadata blocks |
| encoded inline/embedded bytes | 256 MiB per block | Avoid huge XML-resident payloads; the lower XML-header limit is also authoritative |
| validated unused file space | 64 MiB cumulative | Bound zero-padding scans during open |
| compressed subblocks | 65,536 | Supports large data while bounding descriptors/tasks |
| writer compression subblock bytes | 16 MiB | Bounds codec and byte-shuffle scratch independently of full image size |
| decompression ratio | 65,536:1 | Measured current-producer sparse embedded RGB requires about 32,506:1; absolute decoded-byte and sample caps remain authoritative |
| Zstandard window | 256 MiB | Bounds codec history allocation independently of decoded image size; configurable only as a power of two |
| diagnostic records | 1,000 | Prevent error amplification |

All byte constants are powers of two. The public API will expose named presets
(`inspection`, `pfi_desktop`, and opt-in `large_image`) plus individual values.
No preset is evidence that the host has sufficient memory.

Before M2 is accepted, measure actual PFI fixtures and revise these values.
Limits that reject a valid file must return a dedicated resource-limit error,
not a generic malformed-file result.

`max_encoded_block_bytes` is enforced on non-whitespace encoded characters.
Decoded embedded image bytes are bounded by `max_decoded_image_bytes`; decoded
inline Property bytes are bounded by `max_decoded_property_bytes`. With the
default profile, the 16 MiB XML-header limit is reached before the larger
per-block encoded ceiling; applications must raise the header and relevant
decoded-block limits deliberately for larger inline payloads.

`max_unused_space_bytes` bounds the cumulative gaps before, between, and after
inventoried attachment ranges. Every scanned byte must be zero. The attachment
inventory includes standard elements and extension elements that use the
standard `attachment:position:size` syntax, preventing legitimate extension
payloads from being mistaken for unused space.

Writer compression subblocks are rounded down to a whole number of samples and
further capped by the selected codec's input type. A configured size smaller
than one sample and a zero subblock-count budget are invalid arguments. Running
out of the configured count or serialized-byte budget is a resource-limit
failure; the writer never silently falls back to a larger scratch allocation or
uncompressed output.
