# Architecture

## Layering

```text
consumer (PFI, CLI, another application)
                |
        stable public API
                |
 document model + typed metadata + image descriptors
                |
 block planner -> checksum -> decompress -> unshuffle -> endian/sample view
                |
 bounded byte source             secure XML adapter
                \_____________________/
                   XISF container
```

No consumer or UI type crosses into the library. Codec and XML libraries remain
private implementation details unless an unavoidable API/ABI constraint is
documented.

## Proposed modules

| Module | Responsibility | Key invariant |
| --- | --- | --- |
| `container` | Preamble, header span, version | No unbounded read or allocation |
| `xml` | Secure parsing and syntax mapping | No DTD, entity, or network resolution |
| `model` | Immutable document/image/metadata descriptions | Missing remains missing |
| `block` | Locations, range validation, byte sources | Every range proven inside its source |
| `codec` | Decompression and byte unshuffle | Output size and ratio bounded before allocation |
| `pixel` | Layout, endianness, sample views/conversion | No silent precision or channel-order change |
| `checksum` | Declared digest verification | Failed integrity never yields trusted pixels |
| `writer` | Deterministic XML and block planning | Same normalized input yields same bytes |

## Public API shape

The M1 pre-release API now supports:

- `Reader::open_file(path, limits)` and `Reader::open_source(source, limits)`
  returning metadata and image descriptors without decoding pixels;
- an injectable seekable `ByteSource` with stable lifetime;
- `Reader::read_image_into(index, destination, stop_token)` for caller-owned
  memory and a convenience owning `read_image` path;
- `Reader::read_image_rows(index, sink, options, stop_token)` for ephemeral
  planar channel rows with explicit channel/row coordinates, row/subblock
  staging limits, checksum-before-callback, and caller-owned rollback;
- explicit `ImageReadOptions` for source-preserving or native-endian output and
  source/Planar/Normal layout, without sample-type conversion;
- `Reader::read_property_block(metadata_index, options, stop_token)` for
  bounded, integrity-checked String/vector/matrix block bytes with optional
  native-endian conversion;
- `Document::icc_profiles()` plus `Reader::read_icc_profile(index, stop_token)`
  for bounded, byte-exact attachment/inline ICC blocks and direct/referenced
  image associations, without color-management execution;
- `Document::thumbnails()` plus `Reader::read_thumbnail(index, stop_token)` for
  the bounded UInt8/UInt16 Gray/RGB image-like profile, retaining exact source
  representation and main-image association;
- `Document::table_structures()`, `Document::tables()`, and
  `Document::table_bindings()` for bounded structural inspection of table
  fields, rows, cells, serialization forms, and direct/referenced image
  associations without implicit typed coercion or external block access;
- bounded chunk reads with cooperative cancellation;
- exact UInt64 and complex image bytes, with component-wise native-endian
  conversion and no magnitude/phase interpretation;
- a bounded, namespace-aware semantic inventory for non-XISF XML extension
  elements, including parent/image links, attributes, and direct text without
  a raw-XML round-trip claim;
- validated attribute-preserving inspection of RGB working spaces, display
  functions, color-filter arrays, and resolution objects, with direct and
  `Reference`-resolved image bindings and no synthesized defaults;

The pre-release writer now provides:

- `Writer::write_file` for one or more attached little-endian Planar PFI-scalar
  Gray/RGB images, ordered direct metadata, and typed vector/matrix Property
  attachments;
- one bounded compression/checksum/subblock/spool pipeline shared by image and
  Property attachments, plus deterministic XML/layout planning, explicit
  volatile provenance, finite budgets, and cooperative cancellation;
- one internal Property type/category/layout registry shared by parsing,
  reading, and writing so aliases and element widths cannot drift by subsystem;
- byte-identical sequential output through a caller-owned `ByteSink`, including
  partial-write handling and explicit flush errors; generic sinks retain
  caller-owned rollback semantics while `write_file` keeps the atomic contract.

Remote locations must not trigger network access. A future network resolver is
an explicit opt-in consumer capability with scheme allowlists, budgets, cache,
and credential policy.

## Resource limits

Every read operation accepts a policy object with finite defaults for XML bytes,
element count/depth, images, metadata items, dimensions, total decoded bytes,
single-block bytes, decompression ratio, and diagnostic detail. Library code
uses checked multiplication/addition before conversions to `size_t` or stream
offset types. Attachment layout transforms use at most an 8 MiB staging buffer;
they do not allocate a second full decoded frame.

Extension-domain construction has independent cumulative limits for element
records, attribute records, and copied namespace/name/value/text bytes. The
header, XML node, depth, and per-element attribute limits still apply, so the
extension inventory cannot bypass the general parser budget.

Ancillary core objects use a separate bounded record/attribute/binding model.
The model retains exact XML-decoded parameter strings after validation; it does
not apply display or color-space transformations and does not expose parsed
floating-point values as authoritative scientific measurements.

ICC profile records and image bindings have independent limits. Decoding uses
the shared integrity/compression path, preserves the big-endian byte stream,
and performs only bounded header screening. External profile locations remain
descriptive and cannot trigger file-system or network access.

Thumbnail descriptors and bindings are separate from main images so a consumer
cannot accidentally treat preview pixels as scientific samples. Local reads
share the image codec/integrity machinery but use independent count,
dimension, serialized-byte, and decoded-byte limits.

Table structures, tables, fields, rows, cells, bindings, and retained text use
independent cumulative limits. The parser resolves only document-local
standalone Structure references, verifies exact row/column cardinality and
field types, and leaves cell block payloads inspect-only. This keeps hostile
tables from bypassing general XML limits or causing file/network access.

Owning/caller-buffer reads stage compressed input once so its checksum can be
verified before any codec call. Unshuffled codec output is written directly to
the caller's buffer. For shuffled data, scratch memory is limited to one
declared compression subblock. A requested complete-buffer storage-layout
transformation of compressed data still requires one decoded source-
representation buffer.

The row API instead verifies a declared checksum through a bounded first pass,
then decodes one declared subblock at a time and assembles complete planar
channel rows. It hashes the exact delivery pass again before returning success,
so a source change cannot silently detach the summary from delivered bytes.
Normal source rows are split by channel without a full-frame layout buffer. Its
row and subblock buffers are independently bounded; shuffled input can require
multiple buffers and does not imply a total-memory ceiling.

## Concurrency

Parsed documents are immutable. Reader operations use per-call state and may run
concurrently when the underlying byte source supports it. The built-in file
source serializes seek/read operations around one stable handle. Global mutable
codec or parser state is prohibited. Cancellation is currently cooperative
before each bounded source read; row/tile boundaries arrive with streaming
pixel delivery.

## Compatibility and ABI

Source compatibility follows SemVer from the first public release. ABI is not
promised before 1.0. Prefer opaque implementation storage or a C-compatible
facade only after actual downstream needs are measured; do not freeze an ABI
prematurely. C++20 is the planned baseline because PFI's current native toolchain
already requires it.
