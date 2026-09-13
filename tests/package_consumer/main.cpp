// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/byte_source.hpp"
#include "mmxisf/reader.hpp"
#include "mmxisf/version.hpp"
#include "mmxisf/writer.hpp"

#include <string_view>

int main() {
  mmxisf::ReaderOptions options;
  mmxisf::ImageReadOptions read_options;
  mmxisf::PropertyReadOptions property_read_options;
  mmxisf::MetadataBinding binding;
  mmxisf::RawImage image;
  mmxisf::RawPropertyBlock property;
  mmxisf::ImageWriteView write_image;
  mmxisf::MetadataWriteEntry write_metadata;
  mmxisf::WriterOptions writer_options;
  mmxisf::WriteSummary write_summary;
  return options.max_header_bytes > 0 && options.max_encoded_block_bytes > 0 &&
                 options.max_serialized_property_bytes > 0 &&
                 options.max_decoded_property_bytes > 0 &&
                 options.max_unused_space_bytes > 0 &&
                 binding.scope == mmxisf::MetadataBinding::Scope::xisf_unit &&
                 !binding.by_reference &&
                 read_options.pixel_storage ==
                     mmxisf::PixelStorageOutput::source &&
                 read_options.byte_order == mmxisf::ByteOrderOutput::source &&
                 property_read_options.byte_order ==
                     mmxisf::ByteOrderOutput::source &&
                 image.pixel_origin == mmxisf::PixelOrigin::top_left &&
                 image.nominal_channel_order ==
                     mmxisf::NominalChannelOrder::gray_then_alpha &&
                 image.checksum_verification ==
                     mmxisf::ChecksumVerification::not_declared &&
                 property.checksum_verification ==
                     mmxisf::ChecksumVerification::not_declared &&
                 write_image.pixel_storage == mmxisf::PixelStorage::planar &&
                 writer_options.attachment_alignment == 4096 &&
                 writer_options.max_images == 64 &&
                 writer_options.max_metadata_entries == 4096 &&
                 writer_options.max_metadata_value_bytes > 0 &&
                 writer_options.max_cumulative_image_bytes > 0 &&
                 write_metadata.kind == mmxisf::MetadataWriteKind::property &&
                 write_summary.image_block.kind == mmxisf::BlockKind::unknown &&
                 write_summary.image_blocks.empty() &&
                 std::string_view(mmxisf::version()) == "0.1.0"
             ? 0
             : 1;
}
