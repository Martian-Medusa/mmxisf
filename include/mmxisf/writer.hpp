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

#include "mmxisf/document.hpp"
#include "mmxisf/result.hpp"

namespace mmxisf {

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
  std::span<const std::byte> pixels;
};

struct WriterOptions {
  std::uint64_t attachment_alignment{4096};
  std::uint32_t max_header_bytes{16U * 1024U * 1024U};
  std::size_t max_images{64};
  std::uint64_t max_image_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t max_cumulative_image_bytes{4ULL * 1024ULL * 1024ULL * 1024ULL};
  std::string creation_time;
  std::string creator_application;
};

struct WriteSummary {
  std::uint64_t file_size{0};
  std::uint32_t header_length{0};
  BlockLocation image_block;
  std::vector<BlockLocation> image_blocks;
};

class Writer {
public:
  // The 0.1 writer accepts attached Planar Gray or RGB images using the
  // scalar sample formats supported by the reader. Input bytes are
  // little-endian. Floating-point images require explicit finite bounds.
  // Existing destinations and stale temporary files are never overwritten.
  [[nodiscard]] static Result<WriteSummary>
  write_file(const std::filesystem::path &destination,
             const ImageWriteView &image, const WriterOptions &options,
             std::stop_token stop_token = {});
  [[nodiscard]] static Result<WriteSummary>
  write_file(const std::filesystem::path &destination,
             std::span<const ImageWriteView> images,
             const WriterOptions &options, std::stop_token stop_token = {});
};

} // namespace mmxisf
