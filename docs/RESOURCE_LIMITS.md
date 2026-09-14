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
| extension elements | 4,096 cumulative | Bound semantic records for non-XISF namespaces |
| extension attributes | 65,536 cumulative | Bound attribute-object overhead across extension records |
| extension semantic strings | 4 MiB cumulative | Bound copied namespace, name, value, and direct-text bytes |
| ancillary core objects | 4,096 cumulative | Bound RGB working-space, display-function, CFA, and resolution records |
| ancillary attributes | 65,536 cumulative | Bound attribute-object overhead across ancillary records |
| ancillary image bindings | 100,000 cumulative | Bound direct and `Reference`-resolved associations |
| ancillary semantic strings | 4 MiB cumulative | Bound copied uid, namespace, name, and value bytes |
| ICC profiles | 4,096 cumulative | Bound profile descriptors and inline-storage slots |
| ICC image bindings | 100,000 cumulative | Bound direct and `Reference`-resolved associations |
| serialized bytes per ICC profile | 64 MiB | Bound attachment/inline staging before integrity and codec work |
| decoded bytes per ICC profile | 64 MiB | Bound returned profile allocation before decompression |
| thumbnails | 256 cumulative | Bound image-like preview descriptors and embedded-storage slots |
| thumbnail image bindings | 100,000 cumulative | Bound direct and `Reference`-resolved associations |
| thumbnail width or height | 4,096 pixels | Finite compatibility ceiling above the specification's 1,024-pixel recommendation |
| serialized bytes per thumbnail | 128 MiB | Bound attachment/embedded staging before integrity and codec work |
| decoded bytes per thumbnail | 128 MiB | Bound returned preview allocation before decompression |
| table structures | 256 cumulative | Bound standalone and inline Structure records |
| tables | 256 cumulative | Bound Table records and per-table row containers |
| table fields | 4,096 cumulative | Bound heterogeneous schema width across Structures |
| table rows | 100,000 cumulative | Bound row-vector overhead across Tables |
| table cells | 1,000,000 cumulative | Bound heterogeneous cell descriptors |
| table image bindings | 100,000 cumulative | Bound direct and `Reference`-resolved associations |
| table semantic text | 16 MiB cumulative | Bound copied ids, types, captions, headers, formats, values, and inline payload text |
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
| streamed source/output row | 64 MiB per buffer | Bounds low-copy row assembly and Normal-to-planar splitting |
| streamed compression subblock | 64 MiB per compressed or decoded buffer | Prevents one declared subblock from defeating row-delivery memory bounds |
| writer compression subblock bytes | 16 MiB | Bounds codec and byte-shuffle scratch independently of full image size |
| writer Property bytes | 256 MiB per block / 512 MiB cumulative, decoded and serialized independently | Bounds caller-provided and compressed typed vector/matrix attachments independently of images |
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

The extension semantic-string budget counts each copied namespace URI, local
name, parent name/namespace, attribute value, and direct text byte. It does not
replace the element or attribute record-count limits. Prefixes, comments,
processing instructions, CDATA boundaries, and raw source spelling are not
copied into the document model.

The ancillary semantic-string budget counts each retained `uid` plus each
attribute namespace URI, local name, and XML-decoded value. Object, attribute,
and binding counts remain independent limits. General header, node, depth, and
per-element attribute limits apply first.

ICC profile limits are independent from image and Property limits. Inline
decoded bytes are charged against the serialized ICC limit while parsing;
compressed output is checked against the decoded ICC limit before codec work.
The general XML-header and encoded-character limits remain authoritative.

Thumbnail limits are independent from main-image limits. Geometry restrictions
also cap channels at two for Gray and four for RGB. The default dimension cap
does not redefine the XISF recommendation of at most 1024 pixels; applications
can tighten it to 1024 when strict producer policy is desired.

Table limits are cumulative across a document and independent from ordinary
Property metadata limits. General XML header/node/depth/attribute caps remain
authoritative. Inline Cell block text is retained only for inspection and is
charged to the table text budget; no Table Cell block is decoded or resolved by
the current profile.

Row-read limits are per call and checked before the first callback. The row
limit covers both a serialized source row and one planar output row. The
subblock limit applies independently to compressed and decoded staging;
byte-shuffled input can additionally require a shuffle buffer of the same
bounded size. These values are not a single total peak-memory budget. A
declared checksum is verified with a separate fixed 8 MiB maximum hash buffer
before delivery.

Writer compression subblocks are rounded down to a whole number of samples and
further capped by the selected codec's input type. A configured size smaller
than one sample and a zero subblock-count budget are invalid arguments. Running
out of the configured count or serialized-byte budget is a resource-limit
failure; the writer never silently falls back to a larger scratch allocation or
uncompressed output.
For Property blocks the same rule uses the full declared element width,
including both components of a complex element. Property decoded and serialized
budgets are independent from image budgets.
