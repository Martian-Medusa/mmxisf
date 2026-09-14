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

enum class ChecksumVerification { not_declared, verified };

struct ReaderOptions {
  std::uint32_t max_header_bytes{16U * 1024U * 1024U};
  std::size_t max_xml_depth{64};
  std::size_t max_xml_nodes{250'000};
  std::size_t max_attributes_per_element{256};
  std::size_t max_images{64};
  std::size_t max_metadata_entries{100'000};
  std::size_t max_metadata_value_bytes{8U * 1024U * 1024U};
  std::size_t max_extension_elements{4'096};
  std::size_t max_extension_attributes{65'536};
  std::size_t max_extension_bytes{4U * 1024U * 1024U};
  std::size_t max_ancillary_objects{4'096};
  std::size_t max_ancillary_attributes{65'536};
  std::size_t max_ancillary_bindings{100'000};
  std::size_t max_ancillary_bytes{4U * 1024U * 1024U};
  std::size_t max_icc_profiles{4'096};
  std::size_t max_icc_profile_bindings{100'000};
  std::uint64_t max_serialized_icc_profile_bytes{64ULL * 1024ULL * 1024ULL};
  std::uint64_t max_decoded_icc_profile_bytes{64ULL * 1024ULL * 1024ULL};
  std::size_t max_encoded_block_bytes{256U * 1024U * 1024U};
  std::uint64_t max_serialized_image_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
  std::uint64_t max_serialized_property_bytes{256ULL * 1024ULL * 1024ULL};
  std::uint64_t max_decoded_property_bytes{256ULL * 1024ULL * 1024ULL};
  std::uint64_t max_unused_space_bytes{64ULL * 1024ULL * 1024ULL};
  std::size_t max_compressed_subblocks{65'536};
  std::uint64_t max_decompression_ratio{65'536};
  std::uint64_t max_zstd_window_bytes{256ULL * 1024ULL * 1024ULL};
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
  std::optional<ImageOrientation> orientation;
  PixelOrigin pixel_origin{PixelOrigin::top_left};
  PixelTraversal pixel_traversal{PixelTraversal::top_to_bottom_left_to_right};
  NominalChannelOrder nominal_channel_order{
      NominalChannelOrder::gray_then_alpha};
  std::optional<double> lower_bound;
  std::optional<double> upper_bound;
  PixelStorage pixel_storage{PixelStorage::planar};
  ByteOrder byte_order{ByteOrder::little};
  ChecksumVerification checksum_verification{
      ChecksumVerification::not_declared};
  std::vector<std::byte> pixels;
};

[[nodiscard]] MMXISF_API const char *
to_string(ChecksumVerification verification) noexcept;

enum class PixelStorageOutput { source, planar, normal };
enum class ByteOrderOutput { source, native };

struct ImageReadOptions {
  PixelStorageOutput pixel_storage{PixelStorageOutput::source};
  ByteOrderOutput byte_order{ByteOrderOutput::source};
};

struct RawPropertyBlock {
  ByteOrder byte_order{ByteOrder::little};
  ChecksumVerification checksum_verification{
      ChecksumVerification::not_declared};
  std::vector<std::byte> bytes;
};

struct PropertyReadOptions {
  ByteOrderOutput byte_order{ByteOrderOutput::source};
};

struct RawIccProfile {
  ChecksumVerification checksum_verification{
      ChecksumVerification::not_declared};
  std::vector<std::byte> bytes;
};

class MMXISF_API Reader {
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

  // Decoded bytes for supported local or embedded blocks.
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
  [[nodiscard]] Result<RawPropertyBlock>
  read_property_block(std::size_t metadata_index,
                      PropertyReadOptions read_options = {},
                      std::stop_token stop_token = {}) const;
  // Returns the exact decompressed ICC profile byte stream. ICC structures are
  // always big-endian by definition; no byte-order transformation is applied.
  [[nodiscard]] Result<RawIccProfile>
  read_icc_profile(std::size_t profile_index,
                   std::stop_token stop_token = {}) const;

private:
  struct Impl;
  explicit Reader(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace mmxisf
