# Writer API

The pre-release writer produces deterministic monolithic XISF 1.0 files. It is
standalone C++20 and does not depend on PFI, PixInsight, PCL, or Qt.

## Supported profile

- one or more attached two-dimensional Gray or RGB images;
- Planar, little-endian UInt8, UInt16, UInt32, Float32, or Float64 pixels;
- explicit finite increasing bounds for floating-point images;
- direct image-scoped String and TimePoint Properties;
- direct image-scoped FITS keywords;
- direct XISF-unit String and TimePoint Properties;
- fixed caller-supplied creation time and creator application.

The current writer rejects big-endian and Normal/interleaved output, numeric or
block-backed Properties, references, raw XML, compression, shuffle, and
checksums. Reader support for a feature does not imply writer support.

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

## Failure and filesystem contract

Geometry arithmetic, image and metadata counts, per-image and cumulative byte
budgets, header size, identifiers, XML UTF-8, and metadata scope are validated
before the destination is created. Output is written to a sibling temporary
file, flushed, closed, and committed through a no-overwrite hard link. An
existing destination or stale `.mmxisf-tmp` sibling is never replaced.

Cancellation is cooperative between bounded write chunks and immediately
before commit. A cancelled or failed write removes its incomplete temporary
file and does not produce a successful destination. Filesystems that do not
support hard links fail explicitly in the current profile.

`WriteSummary::image_blocks` reports every attached block. The legacy
`image_block` field aliases the first block for source compatibility with the
single-image foundation.
