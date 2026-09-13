// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

#include "mmxisf/byte_source.hpp"
#include "mmxisf/document.hpp"
#include "mmxisf/result.hpp"

namespace mmxisf {

struct ReaderOptions {
  std::uint32_t max_header_bytes{16U * 1024U * 1024U};
  std::size_t max_xml_depth{64};
  std::size_t max_xml_nodes{250'000};
  std::size_t max_attributes_per_element{256};
  std::size_t max_images{64};
  std::size_t max_metadata_entries{100'000};
  std::size_t max_metadata_value_bytes{8U * 1024U * 1024U};
  std::size_t max_encoded_block_bytes{256U * 1024U * 1024U};
  std::uint64_t max_unused_space_bytes{64ULL * 1024ULL * 1024ULL};
  std::size_t max_image_dimensions{8};
  std::uint64_t max_inspected_channels{64};
  std::uint64_t max_decoded_channels{16};
  std::uint64_t max_samples_per_image{536'870'912};
  std::uint64_t max_decoded_image_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
};

struct RawImage {
  std::uint64_t width{0};
  std::uint64_t height{0};
  std::uint64_t channels{0};
  SampleFormat sample_format{SampleFormat::unsupported};
  std::optional<double> lower_bound;
  std::optional<double> upper_bound;
  PixelStorage pixel_storage{PixelStorage::planar};
  ByteOrder byte_order{ByteOrder::little};
  std::vector<std::byte> pixels;
};

enum class PixelStorageOutput { source, planar, normal };
enum class ByteOrderOutput { source, native };

struct ImageReadOptions {
  PixelStorageOutput pixel_storage{PixelStorageOutput::source};
  ByteOrderOutput byte_order{ByteOrderOutput::source};
};

class Reader {
public:
  Reader(Reader &&) noexcept;
  Reader &operator=(Reader &&) noexcept;
  ~Reader();

  Reader(const Reader &) = delete;
  Reader &operator=(const Reader &) = delete;

  [[nodiscard]] static Result<Reader>
  open_file(const std::filesystem::path &path, ReaderOptions options = {});
  [[nodiscard]] static Result<Reader>
  open_source(std::shared_ptr<const ByteSource> source,
              ReaderOptions options = {});

  [[nodiscard]] const Document &document() const noexcept;

  // Exact serialized bytes for supported, uncompressed local or embedded
  // blocks.
  // byte_order and pixel_storage describe the returned byte representation;
  // no implicit endian or layout conversion is performed.
  [[nodiscard]] Result<RawImage>
  read_image(std::size_t image_index, std::stop_token stop_token = {}) const;
  [[nodiscard]] Result<RawImage>
  read_image(std::size_t image_index, ImageReadOptions read_options,
             std::stop_token stop_token = {}) const;
  [[nodiscard]] Result<std::size_t>
  read_image_into(std::size_t image_index, std::span<std::byte> destination,
                  std::stop_token stop_token = {}) const;
  [[nodiscard]] Result<std::size_t>
  read_image_into(std::size_t image_index, std::span<std::byte> destination,
                  ImageReadOptions read_options,
                  std::stop_token stop_token = {}) const;

private:
  struct Impl;
  explicit Reader(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace mmxisf
