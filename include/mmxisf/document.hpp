// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mmxisf {

enum class SampleFormat {
  uint8,
  uint16,
  uint32,
  uint64,
  float32,
  float64,
  complex32,
  complex64,
  unsupported
};
enum class PixelStorage { planar, normal };
enum class ByteOrder { little, big };
enum class BlockKind { attachment, embedded, inline_data, external, unknown };

struct BlockLocation {
  BlockKind kind{BlockKind::unknown};
  std::uint64_t offset{0};
  std::uint64_t size{0};
  std::string raw;
};

struct MetadataEntry {
  enum class Kind { property, fits_keyword };

  Kind kind{Kind::property};
  std::optional<std::size_t> image_index;
  std::string name;
  std::string type;
  std::string value;
  std::string comment;
};

struct ImageInfo {
  std::string id;
  std::vector<std::uint64_t> geometry;
  SampleFormat sample_format{SampleFormat::unsupported};
  std::string sample_format_name;
  std::string color_space;
  std::optional<double> lower_bound;
  std::optional<double> upper_bound;
  PixelStorage pixel_storage{PixelStorage::planar};
  ByteOrder byte_order{ByteOrder::little};
  BlockLocation block;
  std::string compression;
  std::string subblocks;
  std::string checksum;
};

class Document {
public:
  Document() = default;
  Document(std::string version, std::vector<ImageInfo> images,
           std::vector<MetadataEntry> metadata, std::uint64_t file_size,
           std::uint32_t header_length)
      : version_(std::move(version)), images_(std::move(images)),
        metadata_(std::move(metadata)), file_size_(file_size),
        header_length_(header_length) {}

  [[nodiscard]] const std::string &version() const noexcept { return version_; }
  [[nodiscard]] const std::vector<ImageInfo> &images() const noexcept {
    return images_;
  }
  [[nodiscard]] const std::vector<MetadataEntry> &metadata() const noexcept {
    return metadata_;
  }
  [[nodiscard]] std::uint64_t file_size() const noexcept { return file_size_; }
  [[nodiscard]] std::uint32_t header_length() const noexcept {
    return header_length_;
  }

private:
  std::string version_;
  std::vector<ImageInfo> images_;
  std::vector<MetadataEntry> metadata_;
  std::uint64_t file_size_{0};
  std::uint32_t header_length_{0};
};

[[nodiscard]] const char *to_string(SampleFormat format) noexcept;
[[nodiscard]] const char *to_string(PixelStorage storage) noexcept;
[[nodiscard]] const char *to_string(ByteOrder order) noexcept;
[[nodiscard]] const char *to_string(BlockKind kind) noexcept;

} // namespace mmxisf
