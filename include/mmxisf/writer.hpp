// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>

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
  std::span<const std::byte> pixels;
};

struct WriterOptions {
  std::uint64_t attachment_alignment{4096};
  std::uint32_t max_header_bytes{16U * 1024U * 1024U};
  std::uint64_t max_image_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
  std::string creation_time;
  std::string creator_application;
};

struct WriteSummary {
  std::uint64_t file_size{0};
  std::uint32_t header_length{0};
  BlockLocation image_block;
};

class Writer {
public:
  // The 0.1 writer foundation accepts exactly one attached Planar UInt16
  // Gray or RGB image. Input bytes are little-endian.
  // Existing destinations and stale temporary files are never overwritten.
  [[nodiscard]] static Result<WriteSummary>
  write_file(const std::filesystem::path &destination,
             const ImageWriteView &image, const WriterOptions &options,
             std::stop_token stop_token = {});
};

} // namespace mmxisf
