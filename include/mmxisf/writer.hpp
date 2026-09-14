// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "mmxisf/byte_sink.hpp"
#include "mmxisf/document.hpp"
#include "mmxisf/result.hpp"

namespace mmxisf {

enum class CompressionCodec { none, zlib, lz4, lz4hc, zstd };
// SHA-3 uses the canonical XISF names sha3-256 and sha3-512.
enum class ChecksumAlgorithm { none, sha1, sha256, sha512, sha3_256, sha3_512 };

struct ImageWriteView {
  std::string id;
  std::uint64_t width{0};
  std::uint64_t height{0};
  std::uint64_t channels{0};
  SampleFormat sample_format{SampleFormat::unsupported};
  std::string color_space;
  PixelStorage pixel_storage{PixelStorage::planar};
  ByteOrder byte_order{ByteOrder::little};
  std::optional<double> lower_bound;
  std::optional<double> upper_bound;
  CompressionCodec compression{CompressionCodec::none};
  bool byte_shuffle{false};
  ChecksumAlgorithm checksum{ChecksumAlgorithm::none};
  std::span<const std::byte> pixels;
};

enum class MetadataWriteKind { property, fits_keyword };
enum class MetadataWriteValueForm { direct, data_block };

struct MetadataWriteEntry {
  MetadataWriteKind kind{MetadataWriteKind::property};
  // No image index means XISF-unit metadata. FITS keywords require an image.
  std::optional<std::size_t> image_index;
  // Property identifier or FITS keyword name, depending on kind.
  std::string name;
  // The current direct-value profile accepts String, TimePoint, Boolean,
  // signed/unsigned integer, real, and complex Property types.
  std::string type;
  std::string value;
  std::string comment;
  // Block-backed vector/matrix Properties borrow exact serialized element
  // bytes for the duration of write_file(). No byte-order conversion occurs.
  MetadataWriteValueForm value_form{MetadataWriteValueForm::direct};
  std::optional<std::uint64_t> length;
  std::optional<std::uint64_t> rows;
  std::optional<std::uint64_t> columns;
  ByteOrder byte_order{ByteOrder::little};
  std::string format;
  std::span<const std::byte> block_bytes;
  CompressionCodec compression{CompressionCodec::none};
  bool byte_shuffle{false};
  ChecksumAlgorithm checksum{ChecksumAlgorithm::none};
};

struct WriterOptions {
  std::uint64_t attachment_alignment{4096};
  std::uint32_t max_header_bytes{16U * 1024U * 1024U};
  std::size_t max_images{64};
  std::size_t max_metadata_entries{4096};
  std::size_t max_metadata_value_bytes{1024U * 1024U};
  std::uint64_t compression_subblock_bytes{16ULL * 1024ULL * 1024ULL};
  std::size_t max_compression_subblocks{65'536};
  std::uint64_t max_image_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t max_cumulative_image_bytes{4ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t max_property_bytes{256ULL * 1024ULL * 1024ULL};
  std::uint64_t max_cumulative_property_bytes{512ULL * 1024ULL * 1024ULL};
  std::uint64_t max_serialized_property_bytes{256ULL * 1024ULL * 1024ULL};
  std::uint64_t max_cumulative_serialized_property_bytes{512ULL * 1024ULL *
                                                         1024ULL};
  std::uint64_t max_serialized_image_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t max_cumulative_serialized_bytes{4ULL * 1024ULL * 1024ULL *
                                                1024ULL};
  std::string creation_time;
  std::string creator_application;
};

struct WriteSummary {
  std::uint64_t file_size{0};
  std::uint32_t header_length{0};
  BlockLocation image_block;
  std::vector<BlockLocation> image_blocks;
  // Block locations in the encounter order of data_block metadata entries.
  std::vector<BlockLocation> property_blocks;
};

struct SinkWriteOptions {
  // Required only when compression needs a multi-subblock spool. The writer
  // appends private suffixes and never overwrites existing paths.
  std::filesystem::path scratch_file_stem;
};

class MMXISF_API Writer {
public:
  // The 0.1 writer accepts attached Planar Gray or RGB images using the
  // scalar sample formats supported by the reader. Input bytes are
  // little-endian. Floating-point images require explicit finite bounds.
  // Lossless compression, byte shuffle, and serialized-block checksums are
  // selected independently for each image.
  // Existing destinations and stale temporary files are never overwritten.
  [[nodiscard]] static Result<WriteSummary>
  write_file(const std::filesystem::path &destination,
             const ImageWriteView &image, const WriterOptions &options,
             std::stop_token stop_token = {});
  [[nodiscard]] static Result<WriteSummary>
  write_file(const std::filesystem::path &destination,
             std::span<const ImageWriteView> images,
             const WriterOptions &options, std::stop_token stop_token = {});
  [[nodiscard]] static Result<WriteSummary>
  write_file(const std::filesystem::path &destination,
             std::span<const ImageWriteView> images,
             std::span<const MetadataWriteEntry> metadata,
             const WriterOptions &options, std::stop_token stop_token = {});

  // Writes a complete monolithic unit sequentially to caller-owned storage.
  // On failure, the sink may contain a prefix; transactional rollback remains
  // the caller's responsibility. write_file() retains its stronger atomic,
  // no-overwrite filesystem contract.
  [[nodiscard]] static Result<WriteSummary>
  write_to(ByteSink &destination, const ImageWriteView &image,
           const WriterOptions &options,
           const SinkWriteOptions &sink_options = {},
           std::stop_token stop_token = {});
  [[nodiscard]] static Result<WriteSummary>
  write_to(ByteSink &destination, std::span<const ImageWriteView> images,
           const WriterOptions &options,
           const SinkWriteOptions &sink_options = {},
           std::stop_token stop_token = {});
  [[nodiscard]] static Result<WriteSummary>
  write_to(ByteSink &destination, std::span<const ImageWriteView> images,
           std::span<const MetadataWriteEntry> metadata,
           const WriterOptions &options,
           const SinkWriteOptions &sink_options = {},
           std::stop_token stop_token = {});
};

} // namespace mmxisf
