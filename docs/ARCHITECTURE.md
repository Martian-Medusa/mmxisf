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
- explicit `ImageReadOptions` for source-preserving or native-endian output and
  source/Planar/Normal layout, without sample-type conversion;
- `Reader::read_property_block(metadata_index, options, stop_token)` for
  bounded, integrity-checked String/vector/matrix block bytes with optional
  native-endian conversion;
- bounded chunk reads with cooperative cancellation;

Later milestones still need:

- a bounded row/tile callback for low-copy analysis;
- `Writer::write(document, image_sources, sink, options)` with deterministic
  options and an explicit provenance policy;
- injectable byte sinks for files, memory, and tests.

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

Compressed input is staged once so its checksum can be verified before any
codec call. Unshuffled codec output is written directly to the caller's buffer.
For shuffled data, scratch memory is limited to one declared compression
subblock. A requested storage-layout transformation of compressed data still
requires one decoded source-representation buffer in the current M3 slice;
streaming that transform is a later optimization, not a hidden low-memory claim.

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
