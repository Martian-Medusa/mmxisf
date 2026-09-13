// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

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
  }

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

  mmxisf::ReaderOptions tiny_metadata_limit;
  tiny_metadata_limit.max_metadata_value_bytes = 2;
  auto limited_metadata =
      mmxisf::Reader::open_file(valid_path, tiny_metadata_limit);
  expect(!limited_metadata &&
             limited_metadata.error().code == mmxisf::ErrorCode::resource_limit,
         "metadata value budget is enforced");

  const auto bad_storage_path = write_fixture(
      "mmxisf-storage.xisf", valid_xml("pixelStorage=\"interlaced\""), pixels);
  auto bad_storage = mmxisf::Reader::open_file(bad_storage_path);
  expect(!bad_storage &&
             bad_storage.error().code == mmxisf::ErrorCode::invalid_xisf,
         "invalid pixel storage is rejected");

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
  std::filesystem::remove(doctype_path);
  std::filesystem::remove(bad_root_path);
  std::filesystem::remove(no_namespace_path);
  std::filesystem::remove(bad_storage_path);
  std::filesystem::remove(compressed_path);
  return failures == 0 ? 0 : 1;
}
