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

The exact names are deferred to an ADR, but the API should support:

- `Reader::inspect(source, limits)` returning metadata and image descriptors
  without decoding pixels;
- `Reader::read_image(index, destination, options)` for caller-owned memory;
- a bounded row/tile callback for low-copy analysis;
- `Writer::write(document, image_sources, sink, options)` with deterministic
  options and an explicit provenance policy;
- structured error codes with byte/XML context safe to show in PFI diagnostics;
- injectable byte sources and sinks for files, memory, and tests.

Remote locations must not trigger network access. A future network resolver is
an explicit opt-in consumer capability with scheme allowlists, budgets, cache,
and credential policy.

## Resource limits

Every read operation accepts a policy object with finite defaults for XML bytes,
element count/depth, images, metadata items, dimensions, total decoded bytes,
single-block bytes, decompression ratio, and diagnostic detail. Library code
uses checked multiplication/addition before conversions to `size_t` or stream
offset types.

## Concurrency

Parsed documents are immutable. Reader operations use per-call state and may run
concurrently when the underlying byte source supports it. Global mutable codec
or parser state is prohibited. Cancellation is cooperative at block and row/tile
boundaries.

## Compatibility and ABI

Source compatibility follows SemVer from the first public release. ABI is not
promised before 1.0. Prefer opaque implementation storage or a C-compatible
facade only after actual downstream needs are measured; do not freeze an ABI
prematurely. C++20 is the planned baseline because PFI's current native toolchain
already requires it.
