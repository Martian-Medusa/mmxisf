// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/byte_source.hpp"
#include "mmxisf/reader.hpp"
#include "mmxisf/version.hpp"

#include <string_view>

int main() {
  mmxisf::ReaderOptions options;
  mmxisf::ImageReadOptions read_options;
  mmxisf::MetadataBinding binding;
  mmxisf::RawImage image;
  return options.max_header_bytes > 0 &&
                 options.max_encoded_block_bytes > 0 &&
                 options.max_unused_space_bytes > 0 &&
                 binding.scope == mmxisf::MetadataBinding::Scope::xisf_unit &&
                 !binding.by_reference &&
                 read_options.pixel_storage ==
                     mmxisf::PixelStorageOutput::source &&
                 read_options.byte_order == mmxisf::ByteOrderOutput::source &&
                 image.pixel_origin == mmxisf::PixelOrigin::top_left &&
                 image.nominal_channel_order ==
                     mmxisf::NominalChannelOrder::gray_then_alpha &&
                 image.checksum_verification ==
                     mmxisf::ChecksumVerification::not_declared &&
                 std::string_view(mmxisf::version()) == "0.1.0"
             ? 0
             : 1;
}
