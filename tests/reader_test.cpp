// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace {

int failures = 0;

class MemoryByteSource final : public mmxisf::ByteSource {
public:
  explicit MemoryByteSource(std::vector<std::byte> bytes,
                            std::size_t max_read = 3)
      : bytes_(std::move(bytes)), max_read_(max_read) {}

  mmxisf::Result<std::uint64_t> size() const override {
    return static_cast<std::uint64_t>(bytes_.size());
  }

  mmxisf::Result<std::size_t>
  read_at(std::uint64_t offset,
          std::span<std::byte> destination) const override {
    ++read_calls;
    if (offset > bytes_.size()) {
      mmxisf::Error error;
      error.code = mmxisf::ErrorCode::io_error;
      error.message = "memory source range error";
      return error;
    }
    const auto available = bytes_.size() - static_cast<std::size_t>(offset);
    const auto count = std::min({available, destination.size(), max_read_});
    std::copy_n(bytes_.data() + static_cast<std::size_t>(offset), count,
                destination.data());
    if (stop_source_ != nullptr && read_calls >= stop_after_read_) {
      stop_source_->request_stop();
    }
    return count;
  }

  void request_stop_after(std::stop_source &source,
                          std::size_t additional_reads) {
    stop_source_ = &source;
    stop_after_read_ = read_calls + additional_reads;
  }

  mutable std::size_t read_calls{0};

private:
  std::vector<std::byte> bytes_;
  std::size_t max_read_{0};
  std::stop_source *stop_source_{nullptr};
  std::size_t stop_after_read_{0};
};

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void write_u32_le(std::ofstream &output, std::uint32_t value) {
  const std::array<char, 4> bytes{static_cast<char>(value & 0xffU),
                                  static_cast<char>((value >> 8U) & 0xffU),
                                  static_cast<char>((value >> 16U) & 0xffU),
                                  static_cast<char>((value >> 24U) & 0xffU)};
  output.write(bytes.data(), bytes.size());
}

std::filesystem::path write_fixture(const std::string &name,
                                    const std::string &xml,
                                    const std::vector<std::byte> &pixels = {},
                                    std::size_t attachment_offset = 1024) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write("XISF0100", 8);
  write_u32_le(output, static_cast<std::uint32_t>(xml.size()));
  output.write("\0\0\0\0", 4);
  output.write(xml.data(), static_cast<std::streamsize>(xml.size()));
  if (!pixels.empty()) {
    const auto position = static_cast<std::size_t>(output.tellp());
    expect(position <= attachment_offset, "test XML fits before attachment");
    std::vector<char> padding(attachment_offset - position, 0);
    output.write(padding.data(), static_cast<std::streamsize>(padding.size()));
    output.write(reinterpret_cast<const char *>(pixels.data()),
                 static_cast<std::streamsize>(pixels.size()));
  }
  return path;
}

std::string valid_xml(std::string_view image_attributes = {}) {
  return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") +
         "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">" +
         "<Image id=\"test\" geometry=\"2:2:1\" sampleFormat=\"UInt16\" "
         "colorSpace=\"Gray\" location=\"attachment:1024:8\" " +
         std::string(image_attributes) + ">" +
         "<FITSKeyword name=\"EXPTIME\" value=\"30\" comment=\"seconds\"/>" +
         "<Property id=\"Observation:Time:Start\" "
         "type=\"String\">now</Property>" +
         "</Image><Metadata><Property id=\"XISF:CreatorApplication\" "
         "type=\"String\">test</Property></Metadata></xisf>";
}

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  const auto size = static_cast<std::size_t>(input.tellg());
  input.seekg(0);
  std::vector<std::byte> bytes(size);
  input.read(reinterpret_cast<char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return bytes;
}

std::filesystem::path write_bytes(const std::string &name,
                                  const std::vector<std::byte> &bytes) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return path;
}

} // namespace

int main() {
  const std::vector<std::byte> pixels{
      std::byte{0x01}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
      std::byte{0x03}, std::byte{0x00}, std::byte{0x04}, std::byte{0x00}};
  const auto valid_path =
      write_fixture("mmxisf-valid.xisf", valid_xml(), pixels);
  auto reader_result = mmxisf::Reader::open_file(valid_path);
  expect(reader_result.has_value(), "valid fixture opens");
  if (reader_result) {
    auto reader = std::move(reader_result).value();
    expect(reader.document().version() == "1.0", "version is parsed");
    expect(reader.document().images().size() == 1, "one image is enumerated");
    expect(reader.document().metadata().size() == 3,
           "image and document metadata are preserved");
    expect(reader.document().metadata()[1].value == "now",
           "property element text is preserved");
    expect(!reader.document().metadata()[2].image_index,
           "document metadata scope is preserved");
    auto image_result = reader.read_image(0);
    expect(image_result.has_value(), "uncompressed attachment is read");
    if (image_result) {
      expect(image_result.value().pixels == pixels, "pixel bytes remain exact");
      expect(image_result.value().width == 2 &&
                 image_result.value().height == 2,
             "pixel geometry remains exact");
    }

    std::vector<std::byte> destination(pixels.size());
    auto into = reader.read_image_into(0, destination);
    expect(into && into.value() == pixels.size(),
           "caller-owned image buffer is filled");
    expect(destination == pixels, "caller-owned buffer remains byte-exact");
    std::vector<std::byte> too_small(pixels.size() - 1);
    auto short_destination = reader.read_image_into(0, too_small);
    expect(!short_destination && short_destination.error().code ==
                                     mmxisf::ErrorCode::invalid_argument,
           "short caller buffer is rejected");
    std::stop_source stop_source;
    stop_source.request_stop();
    auto cancelled = reader.read_image(0, stop_source.get_token());
    expect(!cancelled && cancelled.error().code == mmxisf::ErrorCode::cancelled,
           "pre-cancelled image read is rejected at a safe boundary");
  }

  auto memory_source =
      std::make_shared<MemoryByteSource>(read_bytes(valid_path));
  auto source_reader = mmxisf::Reader::open_source(memory_source);
  expect(source_reader.has_value(), "short-reading custom ByteSource opens");
  if (source_reader) {
    auto source_image = source_reader.value().read_image(0);
    expect(source_image && source_image.value().pixels == pixels,
           "custom ByteSource supplies exact image bytes");
  }
  expect(memory_source->read_calls > 3,
         "reader handles bounded partial ByteSource reads");

  auto cancelling_source =
      std::make_shared<MemoryByteSource>(read_bytes(valid_path));
  auto cancelling_reader = mmxisf::Reader::open_source(cancelling_source);
  expect(cancelling_reader.has_value(), "cancellation test ByteSource opens");
  if (cancelling_reader) {
    std::stop_source mid_read_stop;
    cancelling_source->request_stop_after(mid_read_stop, 1);
    std::vector<std::byte> destination(pixels.size());
    auto cancelled = cancelling_reader.value().read_image_into(
        0, destination, mid_read_stop.get_token());
    expect(!cancelled && cancelled.error().code == mmxisf::ErrorCode::cancelled,
           "cancellation is observed between partial source reads");
  }

  auto null_source = mmxisf::Reader::open_source(nullptr);
  expect(!null_source &&
             null_source.error().code == mmxisf::ErrorCode::invalid_argument,
         "null ByteSource is rejected");

  const auto default_image_attributes_path = write_fixture(
      "mmxisf-image-defaults.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Image geometry=\"2:2:1\" sampleFormat=\"UInt16\" "
      "pixelStorage=\"Normal\" location=\"attachment:1024:8\"/>"
      "<Metadata/></xisf>",
      pixels);
  auto default_image_attributes =
      mmxisf::Reader::open_file(default_image_attributes_path);
  expect(default_image_attributes.has_value(),
         "optional colorSpace and canonical pixelStorage are accepted");
  if (default_image_attributes) {
    const auto &image = default_image_attributes.value().document().images()[0];
    expect(image.color_space == "Gray", "missing colorSpace defaults to Gray");
    expect(image.pixel_storage == mmxisf::PixelStorage::normal,
           "canonical Normal pixelStorage is preserved");
  }

  auto bad_signature_bytes = read_bytes(valid_path);
  bad_signature_bytes[0] = std::byte{0};
  const auto bad_signature_path =
      write_bytes("mmxisf-signature.xisf", bad_signature_bytes);
  auto bad_signature = mmxisf::Reader::open_file(bad_signature_path);
  expect(!bad_signature &&
             bad_signature.error().code == mmxisf::ErrorCode::invalid_signature,
         "invalid preamble signature is rejected");

  auto reserved_bytes = read_bytes(valid_path);
  reserved_bytes[12] = std::byte{1};
  const auto reserved_path =
      write_bytes("mmxisf-reserved.xisf", reserved_bytes);
  auto reserved = mmxisf::Reader::open_file(reserved_path);
  expect(!reserved &&
             reserved.error().code == mmxisf::ErrorCode::invalid_preamble,
         "nonzero reserved preamble bytes are rejected");

  auto oversized_header_bytes = read_bytes(valid_path);
  const auto impossible_header =
      static_cast<std::uint32_t>(oversized_header_bytes.size());
  oversized_header_bytes[8] = static_cast<std::byte>(impossible_header & 0xffU);
  oversized_header_bytes[9] =
      static_cast<std::byte>((impossible_header >> 8U) & 0xffU);
  oversized_header_bytes[10] =
      static_cast<std::byte>((impossible_header >> 16U) & 0xffU);
  oversized_header_bytes[11] =
      static_cast<std::byte>((impossible_header >> 24U) & 0xffU);
  const auto oversized_header_path =
      write_bytes("mmxisf-header-range.xisf", oversized_header_bytes);
  auto oversized_header = mmxisf::Reader::open_file(oversized_header_path);
  expect(!oversized_header && oversized_header.error().code ==
                                  mmxisf::ErrorCode::invalid_preamble,
         "header range beyond source is rejected");

  const auto short_file_path = write_bytes(
      "mmxisf-short.xisf", std::vector<std::byte>(10, std::byte{0}));
  auto short_file = mmxisf::Reader::open_file(short_file_path);
  expect(!short_file &&
             short_file.error().code == mmxisf::ErrorCode::invalid_preamble,
         "source shorter than the preamble is rejected");

  const auto doctype_path = write_fixture(
      "mmxisf-doctype.xisf",
      "<!DOCTYPE xisf [<!ENTITY x \"bad\">]><xisf version=\"1.0\"/>");
  auto doctype = mmxisf::Reader::open_file(doctype_path);
  expect(!doctype && doctype.error().code == mmxisf::ErrorCode::malformed_xml,
         "DOCTYPE is rejected");

  const auto bad_root_path =
      write_fixture("mmxisf-root.xisf", "<notxisf version=\"1.0\"/>");
  auto bad_root = mmxisf::Reader::open_file(bad_root_path);
  expect(!bad_root && bad_root.error().code == mmxisf::ErrorCode::invalid_xisf,
         "non-XISF root is rejected");

  const auto no_namespace_path =
      write_fixture("mmxisf-namespace.xisf", "<xisf version=\"1.0\"/>");
  auto no_namespace = mmxisf::Reader::open_file(no_namespace_path);
  expect(!no_namespace &&
             no_namespace.error().code == mmxisf::ErrorCode::invalid_xisf,
         "missing XISF namespace is rejected");

  const auto missing_metadata_path = write_fixture(
      "mmxisf-missing-metadata.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\"/>");
  auto missing_metadata = mmxisf::Reader::open_file(missing_metadata_path);
  expect(!missing_metadata &&
             missing_metadata.error().code == mmxisf::ErrorCode::invalid_xisf,
         "mandatory Metadata element is enforced");

  const auto duplicate_metadata_path = write_fixture(
      "mmxisf-duplicate-metadata.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Metadata/><Metadata/></xisf>");
  auto duplicate_metadata = mmxisf::Reader::open_file(duplicate_metadata_path);
  expect(!duplicate_metadata &&
             duplicate_metadata.error().code == mmxisf::ErrorCode::invalid_xisf,
         "duplicate Metadata element is rejected");

  mmxisf::ReaderOptions tiny_metadata_limit;
  tiny_metadata_limit.max_metadata_value_bytes = 2;
  auto limited_metadata =
      mmxisf::Reader::open_file(valid_path, tiny_metadata_limit);
  expect(!limited_metadata &&
             limited_metadata.error().code == mmxisf::ErrorCode::resource_limit,
         "metadata value budget is enforced");

  mmxisf::ReaderOptions tiny_header_limit;
  tiny_header_limit.max_header_bytes = 8;
  auto limited_header =
      mmxisf::Reader::open_file(valid_path, tiny_header_limit);
  expect(!limited_header &&
             limited_header.error().code == mmxisf::ErrorCode::header_too_large,
         "header byte budget is enforced");

  mmxisf::ReaderOptions tiny_node_limit;
  tiny_node_limit.max_xml_nodes = 1;
  auto limited_nodes = mmxisf::Reader::open_file(valid_path, tiny_node_limit);
  expect(!limited_nodes &&
             limited_nodes.error().code == mmxisf::ErrorCode::resource_limit,
         "XML node budget is enforced");

  mmxisf::ReaderOptions tiny_depth_limit;
  tiny_depth_limit.max_xml_depth = 2;
  auto limited_depth = mmxisf::Reader::open_file(valid_path, tiny_depth_limit);
  expect(!limited_depth &&
             limited_depth.error().code == mmxisf::ErrorCode::resource_limit,
         "XML depth budget is enforced");

  mmxisf::ReaderOptions tiny_attribute_limit;
  tiny_attribute_limit.max_attributes_per_element = 1;
  auto limited_attributes =
      mmxisf::Reader::open_file(valid_path, tiny_attribute_limit);
  expect(!limited_attributes && limited_attributes.error().code ==
                                    mmxisf::ErrorCode::resource_limit,
         "XML attribute budget is enforced");

  mmxisf::ReaderOptions zero_image_limit;
  zero_image_limit.max_images = 0;
  auto limited_images = mmxisf::Reader::open_file(valid_path, zero_image_limit);
  expect(!limited_images &&
             limited_images.error().code == mmxisf::ErrorCode::resource_limit,
         "image count budget is enforced");

  mmxisf::ReaderOptions zero_metadata_limit;
  zero_metadata_limit.max_metadata_entries = 0;
  auto limited_entries =
      mmxisf::Reader::open_file(valid_path, zero_metadata_limit);
  expect(!limited_entries &&
             limited_entries.error().code == mmxisf::ErrorCode::resource_limit,
         "metadata entry budget is enforced");

  mmxisf::ReaderOptions one_dimension_limit;
  one_dimension_limit.max_image_dimensions = 1;
  auto limited_dimensions =
      mmxisf::Reader::open_file(valid_path, one_dimension_limit);
  expect(!limited_dimensions && limited_dimensions.error().code ==
                                    mmxisf::ErrorCode::resource_limit,
         "image dimension budget is enforced");

  mmxisf::ReaderOptions zero_inspected_channels;
  zero_inspected_channels.max_inspected_channels = 0;
  auto limited_inspected_channels =
      mmxisf::Reader::open_file(valid_path, zero_inspected_channels);
  expect(!limited_inspected_channels &&
             limited_inspected_channels.error().code ==
                 mmxisf::ErrorCode::resource_limit,
         "inspection channel budget is enforced");

  mmxisf::ReaderOptions zero_decoded_channels;
  zero_decoded_channels.max_decoded_channels = 0;
  auto decoded_channel_reader =
      mmxisf::Reader::open_file(valid_path, zero_decoded_channels);
  expect(decoded_channel_reader.has_value(),
         "decode channel budget does not block inspection");
  if (decoded_channel_reader) {
    auto image = decoded_channel_reader.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::resource_limit,
           "decode channel budget is enforced at decode time");
  }

  mmxisf::ReaderOptions tiny_sample_limit;
  tiny_sample_limit.max_samples_per_image = 3;
  auto sample_limit_reader =
      mmxisf::Reader::open_file(valid_path, tiny_sample_limit);
  expect(sample_limit_reader.has_value(),
         "sample budget does not block inspection");
  if (sample_limit_reader) {
    auto image = sample_limit_reader.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::resource_limit,
           "sample count budget is enforced at decode time");
  }

  mmxisf::ReaderOptions tiny_decoded_byte_limit;
  tiny_decoded_byte_limit.max_decoded_image_bytes = 7;
  auto byte_limit_reader =
      mmxisf::Reader::open_file(valid_path, tiny_decoded_byte_limit);
  expect(byte_limit_reader.has_value(),
         "decoded byte budget does not block inspection");
  if (byte_limit_reader) {
    auto image = byte_limit_reader.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::resource_limit,
           "decoded byte budget is enforced at decode time");
  }

  const auto bad_storage_path = write_fixture(
      "mmxisf-storage.xisf", valid_xml("pixelStorage=\"interlaced\""), pixels);
  auto bad_storage = mmxisf::Reader::open_file(bad_storage_path);
  expect(!bad_storage &&
             bad_storage.error().code == mmxisf::ErrorCode::invalid_xisf,
         "invalid pixel storage is rejected");

  const auto duplicate_attribute_path = write_fixture(
      "mmxisf-duplicate-attribute.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\" "
      "version=\"1.0\"><Metadata/></xisf>");
  auto duplicate_attribute =
      mmxisf::Reader::open_file(duplicate_attribute_path);
  expect(!duplicate_attribute && duplicate_attribute.error().code ==
                                     mmxisf::ErrorCode::malformed_xml,
         "duplicate XML attribute is rejected");

  const auto bad_attachment_path = write_fixture(
      "mmxisf-attachment-range.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Image geometry=\"2:2:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\" "
      "location=\"attachment:1:8\"/><Metadata/></xisf>",
      pixels);
  auto bad_attachment = mmxisf::Reader::open_file(bad_attachment_path);
  expect(!bad_attachment &&
             bad_attachment.error().code == mmxisf::ErrorCode::invalid_block,
         "attachment overlapping the header is rejected");

  const auto bad_property_path = write_fixture(
      "mmxisf-property-attributes.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Property id=\"missing-type\"/><Metadata/></xisf>");
  auto bad_property = mmxisf::Reader::open_file(bad_property_path);
  expect(!bad_property &&
             bad_property.error().code == mmxisf::ErrorCode::invalid_xisf,
         "Property mandatory attributes are enforced");

  const auto bad_fits_parent_path = write_fixture(
      "mmxisf-fits-parent.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Metadata><FITSKeyword name=\"TEST\" value=\"1\" "
      "comment=\"bad parent\"/></Metadata></xisf>");
  auto bad_fits_parent = mmxisf::Reader::open_file(bad_fits_parent_path);
  expect(!bad_fits_parent &&
             bad_fits_parent.error().code == mmxisf::ErrorCode::invalid_xisf,
         "FITSKeyword parent grammar is enforced");

  const auto float_without_bounds_path = write_fixture(
      "mmxisf-float-bounds.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Image geometry=\"1:1:1\" sampleFormat=\"Float32\" colorSpace=\"Gray\" "
      "location=\"attachment:1024:4\"/><Metadata/></xisf>",
      {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
  auto float_without_bounds =
      mmxisf::Reader::open_file(float_without_bounds_path);
  expect(!float_without_bounds && float_without_bounds.error().code ==
                                      mmxisf::ErrorCode::invalid_xisf,
         "floating-point Image without bounds is rejected");

  const auto invalid_bounds_path = write_fixture(
      "mmxisf-invalid-bounds.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Image geometry=\"1:1:1\" sampleFormat=\"Float32\" bounds=\"1:0\" "
      "colorSpace=\"Gray\" location=\"attachment:1024:4\"/>"
      "<Metadata/></xisf>",
      {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
  auto invalid_bounds = mmxisf::Reader::open_file(invalid_bounds_path);
  expect(!invalid_bounds &&
             invalid_bounds.error().code == mmxisf::ErrorCode::invalid_xisf,
         "non-increasing Image bounds are rejected");

  const auto spaced_bounds_path = write_fixture(
      "mmxisf-spaced-bounds.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Image geometry=\"1:1:1\" sampleFormat=\"Float32\" "
      "bounds=\" +0 : +1 \" location=\"attachment:1024:4\"/>"
      "<Metadata/></xisf>",
      {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
  auto spaced_bounds = mmxisf::Reader::open_file(spaced_bounds_path);
  expect(spaced_bounds.has_value(),
         "valid signed bounds with XML whitespace are accepted");

  const auto inline_image_path = write_fixture(
      "mmxisf-inline-image.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" colorSpace=\"Gray\" "
      "location=\"inline:base64\"/><Metadata/></xisf>");
  auto inline_image = mmxisf::Reader::open_file(inline_image_path);
  expect(!inline_image &&
             inline_image.error().code == mmxisf::ErrorCode::invalid_xisf,
         "inline Image block is rejected");

  const auto one_dimensional_path = write_fixture(
      "mmxisf-one-dimensional.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Image geometry=\"4:1\" sampleFormat=\"UInt16\" colorSpace=\"Gray\" "
      "location=\"attachment:1024:8\"/><Metadata/></xisf>",
      pixels);
  auto one_dimensional = mmxisf::Reader::open_file(one_dimensional_path);
  expect(one_dimensional.has_value(), "one-dimensional Image is inspectable");
  if (one_dimensional) {
    auto image = one_dimensional.value().read_image(0);
    expect(!image &&
               image.error().code == mmxisf::ErrorCode::unsupported_feature,
           "one-dimensional Image is not misread as 2-D");
  }

  const auto compressed_path = write_fixture(
      "mmxisf-compressed.xisf", valid_xml("compression=\"zlib:8\""), pixels);
  auto compressed = mmxisf::Reader::open_file(compressed_path);
  expect(compressed.has_value(), "compressed image remains inspectable");
  if (compressed) {
    auto image = compressed.value().read_image(0);
    expect(!image &&
               image.error().code == mmxisf::ErrorCode::unsupported_feature,
           "compressed image decode fails closed in M1");
  }

  std::filesystem::remove(valid_path);
  std::filesystem::remove(bad_signature_path);
  std::filesystem::remove(reserved_path);
  std::filesystem::remove(oversized_header_path);
  std::filesystem::remove(short_file_path);
  std::filesystem::remove(default_image_attributes_path);
  std::filesystem::remove(doctype_path);
  std::filesystem::remove(bad_root_path);
  std::filesystem::remove(no_namespace_path);
  std::filesystem::remove(missing_metadata_path);
  std::filesystem::remove(duplicate_metadata_path);
  std::filesystem::remove(bad_storage_path);
  std::filesystem::remove(duplicate_attribute_path);
  std::filesystem::remove(bad_attachment_path);
  std::filesystem::remove(bad_property_path);
  std::filesystem::remove(bad_fits_parent_path);
  std::filesystem::remove(float_without_bounds_path);
  std::filesystem::remove(invalid_bounds_path);
  std::filesystem::remove(spaced_bounds_path);
  std::filesystem::remove(inline_image_path);
  std::filesystem::remove(one_dimensional_path);
  std::filesystem::remove(compressed_path);
  return failures == 0 ? 0 : 1;
}
