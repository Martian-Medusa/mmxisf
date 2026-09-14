# Writer API

The pre-release writer produces deterministic monolithic XISF 1.0 files. It is
standalone C++20 and does not depend on PFI, PixInsight, PCL, or Qt.

## Supported profile

- one or more attached two-dimensional Gray or RGB images;
- Planar, little-endian UInt8, UInt16, UInt32, Float32, or Float64 pixels;
- explicit finite increasing bounds for floating-point images;
- direct image-scoped String, TimePoint, Boolean, signed/unsigned integer,
  real, and complex Properties;
- direct image-scoped FITS keywords;
- direct XISF-unit Properties from the same profile;
- attached typed vector and matrix Properties in little- or big-endian source
  byte order;
- zlib, LZ4, LZ4HC, and Zstandard compression, optionally with byte shuffle;
- bounded, sample-aligned compression subblocks for large images;
- SHA-1, SHA-256, SHA-512, SHA3-256, and SHA3-512 checksums over exact
  serialized block bytes;
- fixed caller-supplied creation time and creator application.

The current writer rejects big-endian image and Normal/interleaved output,
references, and raw XML.
Reader support for any other feature does not imply writer support.

The same profile can be emitted to a caller-owned sequential `ByteSink` with
`Writer::write_to`. A sink may accept short writes; the writer completes them
in bounded chunks and propagates write or flush failures. The sink is not
closed, committed, or rolled back by the library. On failure it may contain a
prefix of the XISF unit, so callers needing atomic publication must provide a
transactional sink or use `write_file`.

## Minimal use

```cpp
#include <mmxisf/writer.hpp>

#include <array>

std::array<std::byte, 8> pixels{
    std::byte{0x01}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
    std::byte{0x03}, std::byte{0x00}, std::byte{0x04}, std::byte{0x00}};

mmxisf::ImageWriteView image;
image.id = "light";
image.width = 2;
image.height = 2;
image.channels = 1;
image.sample_format = mmxisf::SampleFormat::uint16;
image.color_space = "Gray";
image.pixels = pixels;

mmxisf::WriterOptions options;
options.creation_time = "2026-09-14T00:00:00Z";
options.creator_application = "Example 1.0";

auto result = mmxisf::Writer::write_file("light.xisf", image, options);
if (!result) {
  // result.error().code and result.error().message are explicit.
}
```

Pixel spans are borrowed and must remain valid until `write_file` returns. No
sample conversion is performed: bytes must already match the declared
little-endian Planar profile.

Compression and integrity are selected per image:

```cpp
image.compression = mmxisf::CompressionCodec::zstd;
image.byte_shuffle = true;
image.checksum = mmxisf::ChecksumAlgorithm::sha256;
```

Byte shuffle is valid only with compression and uses the declared sample width.
Checksums cover the serialized attachment bytes, so the reader can reject a
damaged block before decompression. Compression output is deterministic for a
fixed dependency/toolchain set; dependency upgrades may legitimately change
the compressed byte stream while preserving the decoded image.

Compressed images are divided into independently compressed, sample-aligned
subblocks. `WriterOptions::compression_subblock_bytes` defaults to 16 MiB, so
shuffle and codec scratch memory do not scale to a complete large image.
`max_compression_subblocks` bounds the amount of descriptor and loop work.
Files that fit in one subblock retain the single-block representation. The
writer checks cancellation between subblocks and emits the XISF `subblocks`
descriptor only when more than one is required.

Multi-subblock compressed bytes are staged in bounded sibling spool files and
copied into the final temporary XISF only after the exact serialized size and
header layout are known. This avoids retaining a complete compressed image in
RAM in addition to the caller's source pixels and per-subblock scratch. Spools
are removed after success, cancellation, or failure. A stale spool is never
overwritten and causes an explicit I/O error.

## Multiple images and metadata

Use the span overload to write several images and declarative metadata. A
missing `image_index` associates a Property with the XISF unit. FITS keywords
must name a valid image index.

```cpp
std::array<mmxisf::ImageWriteView, 2> images{first, second};
std::array metadata{
    mmxisf::MetadataWriteEntry{
        .image_index = 0,
        .name = "Instrument:Filter:Name",
        .type = "String",
        .value = "L"},
    mmxisf::MetadataWriteEntry{
        .kind = mmxisf::MetadataWriteKind::fits_keyword,
        .image_index = 0,
        .name = "EXPTIME",
        .value = "30.0",
        .comment = "seconds"},
    mmxisf::MetadataWriteEntry{
        .name = "XISF:CreatorModule",
        .type = "String",
        .value = "example-writer"}};

auto result = mmxisf::Writer::write_file("set.xisf", images, metadata,
                                          options);
```

Metadata order is deterministic and preserved within each association. The
writer always emits the required `XISF:CreationTime` and
`XISF:CreatorApplication` properties first in unit metadata; callers cannot
replace them through the metadata array.

Direct scalar Properties support the XISF Boolean type; Int8/16/32/64/128 and
UInt8/16/32/64/128 families and their standard aliases; and Float32/64/128
families and aliases. Values retain their caller-provided lexical form after
XML escaping, but must satisfy the reader's Boolean, integer range/base, or
floating-point grammar before any file is created. Complex32/64/128 and
`Complex` use a parenthesized pair of valid real components. No numeric value is
silently clamped, rounded, reformatted, or inferred.

Block-backed vector and matrix Properties use the same ordered metadata array.
Set `value_form` to `data_block`, declare either `length` or `rows` and
`columns`, and provide exact typed bytes through `block_bytes`. The bytes are
borrowed until `write_file` returns and are never converted. Their size must
match the declared element type and extent exactly. `byte_order` defaults to
little endian and `format` is retained as declarative metadata. Property blocks
use the same compression, optional byte shuffle, checksum, bounded-subblock,
and cleanup-guarded spool behavior as image blocks. Shuffle operates on the
declared Property element width, including a complete complex element.
Compiled coverage exercises all 40 supported vector/matrix types and aliases;
the library still returns raw bytes rather than reinterpreting astronomy values.

Property attachments follow image attachments and preserve metadata encounter
order. `WriteSummary::property_blocks` reports their locations in block-entry
encounter order. `max_property_bytes` and `max_cumulative_property_bytes`
independently bound this data before a destination is created.
The corresponding serialized limits are `max_serialized_property_bytes` and
`max_cumulative_serialized_property_bytes`.

## Caller-owned sinks and scratch storage

Uncompressed and single-subblock writes need no filesystem access when using a
`ByteSink`. Multi-subblock compression must know the complete serialized block
sizes before final header emission, so the caller supplies a unique scratch
stem when that profile is used:

```cpp
mmxisf::SinkWriteOptions sink_options;
sink_options.scratch_file_stem = "/private/controlled/job-42";
auto result = mmxisf::Writer::write_to(
    sink, images, metadata, options, sink_options);
```

The library appends private per-block suffixes, refuses to overwrite any stale
scratch file, and removes the files it created on success, cancellation, or
failure. An empty stem fails before output only when a multi-subblock spool is
actually required. Scratch confidentiality, directory permissions, and unique
stem selection are caller responsibilities.

## Failure and filesystem contract

Geometry arithmetic, image and metadata counts, decoded and serialized
per-image/cumulative byte budgets, compression subblock size/count, header
size, identifiers, XML UTF-8, and metadata scope are validated before the
destination is created. Output is written to a sibling temporary file, flushed,
closed, and committed through a no-overwrite hard link. An existing destination
or stale `.mmxisf-tmp` sibling is never replaced.

Cancellations are cooperative between bounded write chunks and immediately
before commit. A cancelled or failed `write_file` removes its incomplete
temporary file and does not produce a successful destination. Filesystems that
do not support hard links fail explicitly in the current profile.

`WriteSummary::image_blocks` reports every attached block. The legacy
`image_block` field aliases the first block for source compatibility with the
single-image foundation.
