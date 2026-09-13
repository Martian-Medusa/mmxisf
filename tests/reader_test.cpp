// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;
std::vector<std::filesystem::path> fixture_paths;

class FixtureCleanup {
public:
  ~FixtureCleanup() {
    for (const auto &path : fixture_paths) {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
  }
};

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
                                    std::size_t attachment_offset = 1024,
                                    bool add_xml_declaration = true) {
  const auto path = std::filesystem::temp_directory_path() / name;
  fixture_paths.push_back(path);
  constexpr std::string_view declaration =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
  const auto header = add_xml_declaration && !xml.starts_with(declaration)
                          ? std::string(declaration) + xml
                          : xml;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write("XISF0100", 8);
  write_u32_le(output, static_cast<std::uint32_t>(header.size()));
  output.write("\0\0\0\0", 4);
  output.write(header.data(), static_cast<std::streamsize>(header.size()));
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

std::string valid_metadata() {
  return "<Metadata>"
         "<Property id=\"XISF:CreationTime\" type=\"TimePoint\" "
         "value=\"2026-09-13T00:00:00Z\"/>"
         "<Property id=\"XISF:CreatorApplication\" "
         "type=\"String\">test</Property>"
         "</Metadata>";
}

std::string short_metadata() {
  return "<Metadata>"
         "<Property id=\"XISF:CreationTime\" type=\"String\">t</Property>"
         "<Property id=\"XISF:CreatorApplication\" type=\"String\">a</Property>"
         "</Metadata>";
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
         "</Image>" + valid_metadata() + "</xisf>";
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
  fixture_paths.push_back(path);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return path;
}

} // namespace

int main() {
  FixtureCleanup cleanup;
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
    expect(reader.document().metadata().size() == 4,
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

  const auto metadata_fidelity_path = write_fixture(
      "mmxisf-metadata-fidelity.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Image geometry=\"2:2:1\" sampleFormat=\"UInt16\" "
      "location=\"attachment:1024:8\">"
      "<Property uid=\"exposure\" id=\"Instrument:ExposureTime\" "
      "type=\"Float64\" value=\"30.000\" format=\"precision:3\" "
      "comment=\"seconds\"/>"
      "<FITSKeyword name=\"FILTER\" value=\"'Luminance'\" "
      "comment=\"Filter name\"/>"
      "</Image>"
      "<Property uid=\"profile\" id=\"Test:Profile\" type=\"F64Vector\" "
      "length=\"3\" location=\"inline:base64\">AAAAAAAAAAAAAAAAAAAAAA=="
      "</Property>" +
          valid_metadata() + "</xisf>",
      pixels);
  auto metadata_fidelity =
      mmxisf::Reader::open_file(metadata_fidelity_path);
  expect(metadata_fidelity.has_value(),
         "metadata fidelity fixture opens");
  if (metadata_fidelity) {
    const auto &entries = metadata_fidelity.value().document().metadata();
    expect(entries.size() == 5, "all metadata serializations are retained");
    expect(entries[0].scope == mmxisf::MetadataEntry::Scope::image &&
               entries[0].image_index == 0 && entries[0].uid == "exposure",
           "direct image Property scope and uid are retained");
    expect(entries[0].value_form ==
                   mmxisf::MetadataEntry::ValueForm::attribute &&
               entries[0].value == "30.000" &&
               entries[0].format == "precision:3" &&
               entries[0].comment == "seconds",
           "Property attribute serialization remains exact");
    expect(entries[1].kind == mmxisf::MetadataEntry::Kind::fits_keyword &&
               entries[1].value == "'Luminance'" &&
               entries[1].comment == "Filter name",
           "FITS raw value and comment remain exact");
    expect(entries[2].scope == mmxisf::MetadataEntry::Scope::standalone &&
               entries[2].value_form ==
                   mmxisf::MetadataEntry::ValueForm::data_block &&
               entries[2].block.kind == mmxisf::BlockKind::inline_data &&
               entries[2].block.raw == "inline:base64" &&
               entries[2].length == 3,
           "standalone block Property form and extent are retained");
    expect(entries[4].scope == mmxisf::MetadataEntry::Scope::xisf_unit &&
               entries[4].value_form ==
                   mmxisf::MetadataEntry::ValueForm::character_data &&
               entries[4].value == "test",
           "XISF-unit character data Property remains exact");
  }

  const auto invalid_property_extent_path = write_fixture(
      "mmxisf-invalid-property-extent.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Property id=\"Test:Vector\" type=\"F64Vector\" "
          "length=\"3x\" location=\"inline:base64\"/>") +
          valid_metadata() + "</xisf>");
  auto invalid_property_extent =
      mmxisf::Reader::open_file(invalid_property_extent_path);
  expect(!invalid_property_extent &&
             invalid_property_extent.error().code ==
                 mmxisf::ErrorCode::invalid_xisf,
         "malformed Property extent is rejected");

  const auto multi_image_metadata_path = write_fixture(
      "mmxisf-multi-image-metadata.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image id=\"first\" geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"path(first.bin)\">"
          "<Property id=\"Instrument:Filter:Name\" type=\"String\">L"
          "</Property></Image>"
          "<Image id=\"second\" geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"path(second.bin)\">"
          "<Property id=\"Instrument:Filter:Name\" type=\"String\">R"
          "</Property></Image>") +
          valid_metadata() + "</xisf>");
  auto multi_image_metadata =
      mmxisf::Reader::open_file(multi_image_metadata_path);
  expect(multi_image_metadata.has_value(),
         "standalone reader enumerates multi-image metadata");
  if (multi_image_metadata) {
    const auto &document = multi_image_metadata.value().document();
    expect(document.images().size() == 2 &&
               document.metadata_bindings().size() == 4,
           "two images and their metadata bindings remain distinct");
    expect(document.metadata_bindings()[0].image_index == 0 &&
               document.metadata_bindings()[1].image_index == 1 &&
               document.metadata()[0].name == document.metadata()[1].name,
           "equal Property identifiers remain valid across different images");
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
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:2:1\" sampleFormat=\"UInt16\" "
          "pixelStorage=\"Normal\" location=\"attachment:1024:8\"/>") +
          valid_metadata() + "</xisf>",
      pixels);
  auto default_image_attributes =
      mmxisf::Reader::open_file(default_image_attributes_path);
  expect(default_image_attributes.has_value(),
         "optional colorSpace and canonical pixelStorage are accepted");
  if (default_image_attributes) {
    const auto &image = default_image_attributes.value().document().images()[0];
    expect(image.color_space == "Gray", "missing colorSpace defaults to Gray");
    expect(!image.orientation,
           "missing orientation remains explicitly absent");
    expect(image.pixel_origin == mmxisf::PixelOrigin::top_left,
           "XISF pixel origin is explicit");
    expect(image.pixel_traversal ==
               mmxisf::PixelTraversal::top_to_bottom_left_to_right,
           "XISF pixel traversal is explicit");
    expect(image.nominal_channel_order ==
               mmxisf::NominalChannelOrder::gray_then_alpha,
           "Gray nominal channel order is explicit");
    expect(image.pixel_storage == mmxisf::PixelStorage::normal,
           "canonical Normal pixelStorage is preserved");
    expect(!image.lower_bound && !image.upper_bound,
           "absent integer bounds remain explicitly absent");
    auto decoded = default_image_attributes.value().read_image(0);
    expect(decoded.has_value(), "default-semantics image decodes");
    if (decoded) {
      expect(!decoded.value().orientation,
             "absent orientation propagates to decoded pixels");
      expect(decoded.value().pixel_origin == mmxisf::PixelOrigin::top_left,
             "decoded pixel origin is explicit");
      expect(decoded.value().pixel_traversal ==
                 mmxisf::PixelTraversal::top_to_bottom_left_to_right,
             "decoded pixel traversal is explicit");
      expect(decoded.value().nominal_channel_order ==
                 mmxisf::NominalChannelOrder::gray_then_alpha,
             "decoded Gray channel order is explicit");
      expect(decoded.value().checksum_verification ==
                 mmxisf::ChecksumVerification::not_declared,
             "absent checksum is explicit on decoded pixels");
    }
  }

  struct OrientationCase {
    const char *serialized;
    mmxisf::ImageOrientation expected;
  };
  const std::array orientation_cases{
      OrientationCase{"0", mmxisf::ImageOrientation::identity},
      OrientationCase{"flip", mmxisf::ImageOrientation::flip},
      OrientationCase{"90", mmxisf::ImageOrientation::rotate_90},
      OrientationCase{"90;flip", mmxisf::ImageOrientation::rotate_90_flip},
      OrientationCase{"-90", mmxisf::ImageOrientation::rotate_minus_90},
      OrientationCase{"-90;flip",
                      mmxisf::ImageOrientation::rotate_minus_90_flip},
      OrientationCase{"180", mmxisf::ImageOrientation::rotate_180},
      OrientationCase{"180;flip",
                      mmxisf::ImageOrientation::rotate_180_flip}};
  const std::vector<std::byte> asymmetric_pixels{std::byte{0x12},
                                                 std::byte{0x34}};
  for (std::size_t index = 0; index < orientation_cases.size(); ++index) {
    const auto &test = orientation_cases[index];
    const auto path = write_fixture(
        "mmxisf-orientation-" + std::to_string(index) + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"2:1:1\" sampleFormat=\"UInt8\" "
            "orientation=\"") +
            test.serialized + "\" location=\"attachment:1024:2\"/>" +
            valid_metadata() + "</xisf>",
        asymmetric_pixels);
    auto reader = mmxisf::Reader::open_file(path);
    expect(reader.has_value(), "valid Image orientation is accepted");
    if (reader) {
      const auto &info = reader.value().document().images()[0];
      expect(info.orientation == test.expected,
             "Image orientation is preserved");
      expect(std::string(mmxisf::to_string(*info.orientation)) ==
                 test.serialized,
             "Image orientation has a lossless string representation");
      auto decoded = reader.value().read_image(0);
      expect(decoded.has_value(), "oriented image decodes");
      if (decoded) {
        expect(decoded.value().orientation == test.expected,
               "Image orientation propagates to decoded pixels");
        expect(decoded.value().pixels == asymmetric_pixels,
               "scientific decode does not apply display orientation");
      }
    }
  }

  const auto invalid_orientation_path = write_fixture(
      "mmxisf-invalid-orientation.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "orientation=\"vertical\" location=\"attachment:1024:1\"/>") +
          valid_metadata() + "</xisf>",
      {std::byte{0x00}});
  auto invalid_orientation =
      mmxisf::Reader::open_file(invalid_orientation_path);
  expect(!invalid_orientation &&
             invalid_orientation.error().code ==
                 mmxisf::ErrorCode::invalid_xisf,
         "invalid Image orientation is rejected");

  struct ChannelOrderCase {
    const char *name;
    const char *color_space;
    std::uint64_t channels;
    mmxisf::NominalChannelOrder expected;
  };
  const std::array channel_order_cases{
      ChannelOrderCase{"gray", "Gray", 1,
                       mmxisf::NominalChannelOrder::gray_then_alpha},
      ChannelOrderCase{
          "rgb", "RGB", 3,
          mmxisf::NominalChannelOrder::red_green_blue_then_alpha},
      ChannelOrderCase{"cielab", "CIELab", 3,
                       mmxisf::NominalChannelOrder::cie_l_a_b_then_alpha}};
  for (const auto &test : channel_order_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-channel-order-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"1:1:") +
            std::to_string(test.channels) + "\" sampleFormat=\"UInt8\" " +
            "colorSpace=\"" + test.color_space +
            "\" location=\"attachment:1024:" +
            std::to_string(test.channels) + "\"/>" + valid_metadata() +
            "</xisf>",
        std::vector<std::byte>(static_cast<std::size_t>(test.channels),
                               std::byte{0}));
    auto reader = mmxisf::Reader::open_file(path);
    expect(reader.has_value(), "supported color-space descriptor opens");
    if (reader) {
      expect(reader.value().document().images()[0].nominal_channel_order ==
                 test.expected,
             "nominal channel order follows colorSpace");
      if (std::string_view(test.color_space) != "CIELab") {
        auto decoded = reader.value().read_image(0);
        expect(decoded.has_value(), "supported color space decodes");
        if (decoded) {
          expect(decoded.value().nominal_channel_order == test.expected,
                 "nominal channel order propagates to decoded pixels");
        }
      }
    }
  }

  struct ScalarDecodeCase {
    const char *name;
    const char *sample_format;
    const char *bounds;
    mmxisf::SampleFormat expected_format;
    std::size_t byte_count;
  };
  const std::array scalar_decode_cases{
      ScalarDecodeCase{"uint8", "UInt8", "", mmxisf::SampleFormat::uint8, 1},
      ScalarDecodeCase{"uint16", "UInt16", "", mmxisf::SampleFormat::uint16, 2},
      ScalarDecodeCase{"uint32", "UInt32", "", mmxisf::SampleFormat::uint32, 4},
      ScalarDecodeCase{"float32", "Float32", " bounds=\"0:1\"",
                       mmxisf::SampleFormat::float32, 4},
      ScalarDecodeCase{"float64", "Float64", " bounds=\"-1:1\"",
                       mmxisf::SampleFormat::float64, 8},
  };
  for (const auto &test : scalar_decode_cases) {
    std::vector<std::byte> sample_bytes(test.byte_count);
    for (std::size_t index = 0; index < sample_bytes.size(); ++index) {
      sample_bytes[index] = static_cast<std::byte>(index + 1);
    }
    const auto path = write_fixture(
        std::string("mmxisf-m2-scalar-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"1:1:1\" sampleFormat=\"") +
            test.sample_format + "\" colorSpace=\"Gray\"" + test.bounds +
            " location=\"attachment:1024:" + std::to_string(test.byte_count) +
            "\"/>" + valid_metadata() + "</xisf>",
        sample_bytes);
    auto result = mmxisf::Reader::open_file(path);
    expect(result.has_value(), test.name);
    if (result) {
      const auto &info = result.value().document().images()[0];
      expect(info.sample_format == test.expected_format, test.name);
      expect(std::string(mmxisf::to_string(info.sample_format)) ==
                 test.sample_format,
             test.name);
      auto image = result.value().read_image(0);
      expect(image && image.value().pixels == sample_bytes, test.name);
    }
  }

  struct NativeScalarOracleCase {
    const char *name;
    const char *sample_format;
    const char *bounds;
    std::size_t sample_size;
    std::vector<std::byte> big_endian_bytes;
  };
  const std::array native_scalar_oracle_cases{
      NativeScalarOracleCase{
          "uint8", "UInt8", "", 1, {std::byte{0x12}, std::byte{0x34}}},
      NativeScalarOracleCase{
          "uint16",
          "UInt16",
          "",
          2,
          {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}},
      NativeScalarOracleCase{"uint32",
                             "UInt32",
                             "",
                             4,
                             {std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                              std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
                              std::byte{0x07}, std::byte{0x08}}},
      NativeScalarOracleCase{"float32",
                             "Float32",
                             " bounds=\"0:1\"",
                             4,
                             {std::byte{0x3f}, std::byte{0x80}, std::byte{0x00},
                              std::byte{0x00}, std::byte{0x3f}, std::byte{0x00},
                              std::byte{0x00}, std::byte{0x00}}},
      NativeScalarOracleCase{
          "float64",
          "Float64",
          " bounds=\"0:1\"",
          8,
          {std::byte{0x3f}, std::byte{0xf0}, std::byte{0x00}, std::byte{0x00},
           std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
           std::byte{0x3f}, std::byte{0xe0}, std::byte{0x00}, std::byte{0x00},
           std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}}},
  };
  for (const auto &test : native_scalar_oracle_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m2-native-oracle-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"2:1:1\" sampleFormat=\"") +
            test.sample_format + "\" byteOrder=\"big\"" + test.bounds +
            " location=\"attachment:1024:" +
            std::to_string(test.big_endian_bytes.size()) + "\"/>" +
            valid_metadata() + "</xisf>",
        test.big_endian_bytes);
    auto result = mmxisf::Reader::open_file(path);
    expect(result.has_value(), test.name);
    if (result) {
      mmxisf::ImageReadOptions options;
      options.byte_order = mmxisf::ByteOrderOutput::native;
      auto image = result.value().read_image(0, options);
      auto expected = test.big_endian_bytes;
      if constexpr (std::endian::native == std::endian::little) {
        for (std::size_t offset = 0; offset < expected.size();
             offset += test.sample_size) {
          std::reverse(expected.begin() + static_cast<std::ptrdiff_t>(offset),
                       expected.begin() + static_cast<std::ptrdiff_t>(
                                              offset + test.sample_size));
        }
      }
      expect(image && image.value().pixels == expected, test.name);
    }
  }

  struct InspectOnlyScalarCase {
    const char *name;
    const char *sample_format;
    mmxisf::SampleFormat expected_format;
  };
  const std::array inspect_only_scalar_cases{
      InspectOnlyScalarCase{"uint64", "UInt64", mmxisf::SampleFormat::uint64},
      InspectOnlyScalarCase{"complex32", "Complex32",
                            mmxisf::SampleFormat::complex32},
      InspectOnlyScalarCase{"complex64", "Complex64",
                            mmxisf::SampleFormat::complex64},
  };
  for (const auto &test : inspect_only_scalar_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m2-inspect-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"1:1:1\" sampleFormat=\"") +
            test.sample_format +
            "\" colorSpace=\"Gray\" location=\"attachment:1024:8\"/>" +
            valid_metadata() + "</xisf>",
        std::vector<std::byte>(8));
    auto result = mmxisf::Reader::open_file(path);
    expect(result.has_value(), test.name);
    if (result) {
      expect(result.value().document().images()[0].sample_format ==
                 test.expected_format,
             test.name);
      auto image = result.value().read_image(0);
      expect(!image &&
                 image.error().code == mmxisf::ErrorCode::unsupported_feature,
             test.name);
    }
  }

  struct RgbStorageCase {
    const char *name;
    const char *pixel_storage;
    mmxisf::PixelStorage expected_storage;
  };
  const std::array rgb_storage_cases{
      RgbStorageCase{"planar", "Planar", mmxisf::PixelStorage::planar},
      RgbStorageCase{"normal", "Normal", mmxisf::PixelStorage::normal},
  };
  const std::vector<std::byte> rgb_pixels{std::byte{0x10}, std::byte{0x20},
                                          std::byte{0x30}, std::byte{0x40},
                                          std::byte{0x50}, std::byte{0x60}};
  for (const auto &test : rgb_storage_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m2-rgb-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
            "colorSpace=\"RGB\" pixelStorage=\"") +
            test.pixel_storage + "\" location=\"attachment:1024:6\"/>" +
            valid_metadata() + "</xisf>",
        rgb_pixels);
    auto result = mmxisf::Reader::open_file(path);
    expect(result.has_value(), test.name);
    if (result) {
      const auto &info = result.value().document().images()[0];
      expect(info.color_space == "RGB" &&
                 info.pixel_storage == test.expected_storage,
             test.name);
      auto image = result.value().read_image(0);
      expect(image && image.value().channels == 3 &&
                 image.value().pixels == rgb_pixels,
             test.name);
    }
  }

  const std::vector<std::byte> big_endian_planar_rgb{
      std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
      std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
      std::byte{0x09}, std::byte{0x0a}, std::byte{0x0b}, std::byte{0x0c}};
  const auto transformed_rgb_path = write_fixture(
      "mmxisf-m2-transformed-rgb.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:1:3\" sampleFormat=\"UInt16\" "
          "colorSpace=\"RGB\" pixelStorage=\"Planar\" byteOrder=\"big\" "
          "location=\"attachment:1024:12\"/>") +
          valid_metadata() + "</xisf>",
      big_endian_planar_rgb);
  auto transformed_rgb_reader = mmxisf::Reader::open_file(transformed_rgb_path);
  expect(transformed_rgb_reader.has_value(),
         "transform fixture opens for typed delivery");
  if (transformed_rgb_reader) {
    mmxisf::ImageReadOptions options;
    options.pixel_storage = mmxisf::PixelStorageOutput::normal;
    options.byte_order = mmxisf::ByteOrderOutput::native;
    auto transformed = transformed_rgb_reader.value().read_image(0, options);
    expect(transformed.has_value(),
           "Planar big-endian RGB transforms to Normal native order");
    if (transformed) {
      std::vector<std::byte> expected;
      const std::array<std::uint16_t, 6> values{0x0102, 0x0506, 0x090a,
                                                0x0304, 0x0708, 0x0b0c};
      for (const auto value : values) {
        if constexpr (std::endian::native == std::endian::little) {
          expected.push_back(static_cast<std::byte>(value & 0xffU));
          expected.push_back(static_cast<std::byte>(value >> 8U));
        } else {
          expected.push_back(static_cast<std::byte>(value >> 8U));
          expected.push_back(static_cast<std::byte>(value & 0xffU));
        }
      }
      expect(transformed.value().pixels == expected,
             "layout and endian transform preserves every UInt16 value");
      expect(transformed.value().pixel_storage ==
                     mmxisf::PixelStorage::normal &&
                 transformed.value().byte_order ==
                     (std::endian::native == std::endian::little
                          ? mmxisf::ByteOrder::little
                          : mmxisf::ByteOrder::big),
             "transformed image describes its output representation");
      std::vector<std::byte> destination(expected.size() + 2, std::byte{0x7f});
      auto into = transformed_rgb_reader.value().read_image_into(0, destination,
                                                                 options);
      expect(into && into.value() == expected.size() &&
                 std::equal(expected.begin(), expected.end(),
                            destination.begin()) &&
                 destination[expected.size()] == std::byte{0x7f},
             "caller-owned transformed delivery writes only the image span");
    }
  }

  const auto native_endian_path = write_fixture(
      "mmxisf-m2-native-endian.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt16\" "
          "byteOrder=\"big\" location=\"attachment:1024:2\"/>") +
          valid_metadata() + "</xisf>",
      {std::byte{0x12}, std::byte{0x34}});
  auto native_endian_reader = mmxisf::Reader::open_file(native_endian_path);
  expect(native_endian_reader.has_value(), "native-endian fixture opens");
  if (native_endian_reader) {
    mmxisf::ImageReadOptions options;
    options.byte_order = mmxisf::ByteOrderOutput::native;
    auto image = native_endian_reader.value().read_image(0, options);
    const std::vector<std::byte> expected =
        std::endian::native == std::endian::little
            ? std::vector<std::byte>{std::byte{0x34}, std::byte{0x12}}
            : std::vector<std::byte>{std::byte{0x12}, std::byte{0x34}};
    expect(image && image.value().pixels == expected,
           "native-endian conversion works without a layout transform");
  }

  const auto normal_to_planar_path = write_fixture(
      "mmxisf-m2-normal-to-planar.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
          "colorSpace=\"RGB\" pixelStorage=\"Normal\" "
          "location=\"attachment:1024:6\"/>") +
          valid_metadata() + "</xisf>",
      {std::byte{10}, std::byte{30}, std::byte{50}, std::byte{20},
       std::byte{40}, std::byte{60}});
  auto normal_to_planar = mmxisf::Reader::open_file(normal_to_planar_path);
  expect(normal_to_planar.has_value(), "Normal RGB transform fixture opens");
  if (normal_to_planar) {
    mmxisf::ImageReadOptions options;
    options.pixel_storage = mmxisf::PixelStorageOutput::planar;
    auto transformed = normal_to_planar.value().read_image(0, options);
    expect(transformed && transformed.value().pixels ==
                              std::vector<std::byte>{
                                  std::byte{10}, std::byte{20}, std::byte{30},
                                  std::byte{40}, std::byte{50}, std::byte{60}},
           "Normal UInt8 RGB transforms to Planar without channel loss");

    options.pixel_storage = static_cast<mmxisf::PixelStorageOutput>(0xffU);
    auto invalid_options = normal_to_planar.value().read_image(0, options);
    expect(!invalid_options && invalid_options.error().code ==
                                   mmxisf::ErrorCode::invalid_argument,
           "invalid pixel-storage output option fails closed");
  }

  const std::vector<std::byte> zlib_rgb_compressed{
      std::byte{0x78}, std::byte{0x9c}, std::byte{0x63}, std::byte{0x64},
      std::byte{0x62}, std::byte{0x66}, std::byte{0x61}, std::byte{0x65},
      std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3e},
      std::byte{0x00}, std::byte{0x16}};
  const std::vector<std::byte> zlib_rgb_pixels{std::byte{1}, std::byte{2},
                                               std::byte{3}, std::byte{4},
                                               std::byte{5}, std::byte{6}};
  const auto zlib_attachment_path = write_fixture(
      "mmxisf-m3-zlib-attachment.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
          "colorSpace=\"RGB\" compression=\"zlib:6\" "
          "location=\"attachment:1024:14\"/>") +
          valid_metadata() + "</xisf>",
      zlib_rgb_compressed);
  auto zlib_attachment = mmxisf::Reader::open_file(zlib_attachment_path);
  expect(zlib_attachment.has_value(), "zlib attachment fixture opens");
  if (zlib_attachment) {
    auto image = zlib_attachment.value().read_image(0);
    expect(image && image.value().pixels == zlib_rgb_pixels,
           "zlib attachment decompresses to exact RGB bytes");
  }

  const auto zlib_embedded_path = write_fixture(
      "mmxisf-m3-zlib-embedded.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
          "colorSpace=\"RGB\" location=\"embedded\">"
          "<Data encoding=\"base64\" compression=\"zlib:6\" "
          "checksum=\"sha256:2fc3146104370f94342cbabfe10ecc0f019cfb1196541"
          "460372fd8fec537a314\">"
          "eJxjZGJmYWUDAAA+ABY=</Data></Image>") +
          valid_metadata() + "</xisf>");
  auto zlib_embedded = mmxisf::Reader::open_file(zlib_embedded_path);
  expect(zlib_embedded.has_value(), "zlib embedded fixture opens");
  if (zlib_embedded) {
    expect(zlib_embedded.value().document().images()[0].compression == "zlib:6",
           "embedded Data compression descriptor is preserved");
    auto image = zlib_embedded.value().read_image(0);
    expect(image && image.value().pixels == zlib_rgb_pixels,
           "zlib embedded block decompresses after Base64 decoding");
  }

  const std::vector<std::byte> shuffled_zlib_compressed{
      std::byte{0x78}, std::byte{0x9c}, std::byte{0x63}, std::byte{0x64},
      std::byte{0x66}, std::byte{0x65}, std::byte{0xe7}, std::byte{0xe4},
      std::byte{0x66}, std::byte{0x62}, std::byte{0x61}, std::byte{0xe3},
      std::byte{0xe0}, std::byte{0xe2}, std::byte{0x01}, std::byte{0x00},
      std::byte{0x01}, std::byte{0xaf}, std::byte{0x00}, std::byte{0x4f}};
  const auto shuffled_zlib_path = write_fixture(
      "mmxisf-m3-zlib-shuffle.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:1:3\" sampleFormat=\"UInt16\" "
          "colorSpace=\"RGB\" pixelStorage=\"Planar\" byteOrder=\"big\" "
          "compression=\"zlib+sh:12:2\" "
          "location=\"attachment:1024:20\"/>") +
          valid_metadata() + "</xisf>",
      shuffled_zlib_compressed);
  auto shuffled_zlib = mmxisf::Reader::open_file(shuffled_zlib_path);
  expect(shuffled_zlib.has_value(), "zlib+sh fixture opens");
  if (shuffled_zlib) {
    const std::vector<std::byte> source_order{
        std::byte{1}, std::byte{2},  std::byte{3},  std::byte{4},
        std::byte{5}, std::byte{6},  std::byte{7},  std::byte{8},
        std::byte{9}, std::byte{10}, std::byte{11}, std::byte{12}};
    auto source_image = shuffled_zlib.value().read_image(0);
    expect(source_image && source_image.value().pixels == source_order,
           "zlib+sh reverses byte shuffle exactly");

    mmxisf::ImageReadOptions options;
    options.pixel_storage = mmxisf::PixelStorageOutput::normal;
    options.byte_order = mmxisf::ByteOrderOutput::native;
    auto transformed = shuffled_zlib.value().read_image(0, options);
    std::vector<std::byte> expected;
    const std::array<std::uint16_t, 6> values{0x0102, 0x0506, 0x090a,
                                              0x0304, 0x0708, 0x0b0c};
    for (const auto value : values) {
      if constexpr (std::endian::native == std::endian::little) {
        expected.push_back(static_cast<std::byte>(value & 0xffU));
        expected.push_back(static_cast<std::byte>(value >> 8U));
      } else {
        expected.push_back(static_cast<std::byte>(value >> 8U));
        expected.push_back(static_cast<std::byte>(value & 0xffU));
      }
    }
    expect(transformed && transformed.value().pixels == expected,
           "compressed RGB supports layout and endian output transforms");
  }

  const std::vector<std::byte> zlib_subblocks{
      std::byte{0x78}, std::byte{0x9c}, std::byte{0x63}, std::byte{0x64},
      std::byte{0x62}, std::byte{0x66}, std::byte{0x01}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x18}, std::byte{0x00}, std::byte{0x0b},
      std::byte{0x78}, std::byte{0x9c}, std::byte{0x63}, std::byte{0x65},
      std::byte{0x63}, std::byte{0xe7}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x1b}};
  const auto zlib_subblocks_path = write_fixture(
      "mmxisf-m3-zlib-subblocks.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"8:1:1\" sampleFormat=\"UInt8\" "
          "compression=\"zlib:8\" subblocks=\"12,4:12,4\" "
          "location=\"attachment:1024:24\"/>") +
          valid_metadata() + "</xisf>",
      zlib_subblocks);
  auto zlib_subblock_reader = mmxisf::Reader::open_file(zlib_subblocks_path);
  expect(zlib_subblock_reader.has_value(), "zlib subblock fixture opens");
  if (zlib_subblock_reader) {
    auto image = zlib_subblock_reader.value().read_image(0);
    expect(image && image.value().pixels ==
                        std::vector<std::byte>{std::byte{1}, std::byte{2},
                                               std::byte{3}, std::byte{4},
                                               std::byte{5}, std::byte{6},
                                               std::byte{7}, std::byte{8}},
           "zlib subblocks concatenate exact decoded bytes");
  }

  struct Lz4CodecCase {
    const char *name;
    const char *codec;
  };
  const std::array lz4_codec_cases{Lz4CodecCase{"lz4", "lz4"},
                                   Lz4CodecCase{"lz4hc", "lz4hc"}};
  const std::vector<std::byte> lz4_rgb_compressed{
      std::byte{0x60}, std::byte{1}, std::byte{2}, std::byte{3},
      std::byte{4},    std::byte{5}, std::byte{6}};
  for (const auto &test : lz4_codec_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m3-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
            "colorSpace=\"RGB\" compression=\"") +
            test.codec + ":6\" location=\"attachment:1024:7\"/>" +
            valid_metadata() + "</xisf>",
        lz4_rgb_compressed);
    auto reader = mmxisf::Reader::open_file(path);
    expect(reader.has_value(), test.name);
    if (reader) {
      auto image = reader.value().read_image(0);
      expect(image && image.value().pixels == zlib_rgb_pixels, test.name);
    }
  }

  const std::vector<std::byte> shuffled_lz4_compressed{
      std::byte{0xc0}, std::byte{1},  std::byte{3}, std::byte{5}, std::byte{7},
      std::byte{9},    std::byte{11}, std::byte{2}, std::byte{4}, std::byte{6},
      std::byte{8},    std::byte{10}, std::byte{12}};
  const auto shuffled_lz4_path = write_fixture(
      "mmxisf-m3-lz4-shuffle.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:1:3\" sampleFormat=\"UInt16\" "
          "colorSpace=\"RGB\" byteOrder=\"big\" "
          "compression=\"lz4+sh:12:2\" "
          "location=\"attachment:1024:13\"/>") +
          valid_metadata() + "</xisf>",
      shuffled_lz4_compressed);
  auto shuffled_lz4 = mmxisf::Reader::open_file(shuffled_lz4_path);
  expect(shuffled_lz4.has_value(), "lz4+sh fixture opens");
  if (shuffled_lz4) {
    auto image = shuffled_lz4.value().read_image(0);
    expect(image && image.value().pixels ==
                        std::vector<std::byte>{
                            std::byte{1}, std::byte{2}, std::byte{3},
                            std::byte{4}, std::byte{5}, std::byte{6},
                            std::byte{7}, std::byte{8}, std::byte{9},
                            std::byte{10}, std::byte{11}, std::byte{12}},
           "lz4+sh reverses byte shuffle exactly");
  }

  const auto shuffled_lz4hc_path = write_fixture(
      "mmxisf-m3-lz4hc-shuffle.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:1:3\" sampleFormat=\"UInt16\" "
          "colorSpace=\"RGB\" byteOrder=\"big\" "
          "compression=\"lz4hc+sh:12:2\" "
          "location=\"attachment:1024:13\"/>") +
          valid_metadata() + "</xisf>",
      shuffled_lz4_compressed);
  auto shuffled_lz4hc = mmxisf::Reader::open_file(shuffled_lz4hc_path);
  expect(shuffled_lz4hc.has_value(), "lz4hc+sh fixture opens");
  if (shuffled_lz4hc) {
    auto image = shuffled_lz4hc.value().read_image(0);
    expect(image && image.value().pixels ==
                        std::vector<std::byte>{
                            std::byte{1}, std::byte{2}, std::byte{3},
                            std::byte{4}, std::byte{5}, std::byte{6},
                            std::byte{7}, std::byte{8}, std::byte{9},
                            std::byte{10}, std::byte{11}, std::byte{12}},
           "lz4hc+sh uses the compatible LZ4 decoder and unshuffle path");
  }

  const std::vector<std::byte> lz4_subblocks{
      std::byte{0x40}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
      std::byte{0x40}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
  const auto lz4_subblocks_path = write_fixture(
      "mmxisf-m3-lz4-subblocks.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"8:1:1\" sampleFormat=\"UInt8\" "
          "compression=\"lz4:8\" subblocks=\"5,4:5,4\" "
          "location=\"attachment:1024:10\"/>") +
          valid_metadata() + "</xisf>",
      lz4_subblocks);
  auto lz4_subblock_reader = mmxisf::Reader::open_file(lz4_subblocks_path);
  expect(lz4_subblock_reader.has_value(), "LZ4 subblock fixture opens");
  if (lz4_subblock_reader) {
    auto image = lz4_subblock_reader.value().read_image(0);
    expect(image && image.value().pixels ==
                        std::vector<std::byte>{std::byte{1}, std::byte{2},
                                               std::byte{3}, std::byte{4},
                                               std::byte{5}, std::byte{6},
                                               std::byte{7}, std::byte{8}},
           "LZ4 subblocks concatenate exact decoded bytes");
  }

  struct ChecksumCase {
    const char *name;
    const char *descriptor;
  };
  const std::array checksum_cases{
      ChecksumCase{"sha-1", "sha-1:5d211bad8f4ee70e16c7d343a838fc344a1ed961"},
      ChecksumCase{"sha1", "sha1:5d211bad8f4ee70e16c7d343a838fc344a1ed961"},
      ChecksumCase{
          "sha-256",
          "sha-256:"
          "7192385c3c0605de55bb9476ce1d90748190ecb32a8eed7f5207b30cf6a1fe89"},
      ChecksumCase{
          "sha256",
          "sha256:"
          "7192385c3c0605de55bb9476ce1d90748190ecb32a8eed7f5207b30cf6a1fe89"},
      ChecksumCase{
          "sha-512",
          "sha-512:"
          "178d767c364244ede054ebb3cc4af0ac2b307a86fba6a32706ce4f692642674d"
          "2ab8f51ee738ecb09bc296918aa85db48abe28fcaef7aa2da81a618cc6d891c3"},
      ChecksumCase{
          "sha512",
          "sha512:"
          "178d767c364244ede054ebb3cc4af0ac2b307a86fba6a32706ce4f692642674d"
          "2ab8f51ee738ecb09bc296918aa85db48abe28fcaef7aa2da81a618cc6d891c3"}};
  for (const auto &test : checksum_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m3-checksum-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
            "colorSpace=\"RGB\" checksum=\"") +
            test.descriptor + "\" location=\"attachment:1024:6\"/>" +
            valid_metadata() + "</xisf>",
        zlib_rgb_pixels);
    auto reader = mmxisf::Reader::open_file(path);
    expect(reader.has_value(), test.name);
    if (reader) {
      auto image = reader.value().read_image(0);
      expect(image && image.value().pixels == zlib_rgb_pixels, test.name);
      if (image) {
        expect(image.value().checksum_verification ==
                   mmxisf::ChecksumVerification::verified,
               "successful declared checksum is explicit");
      }
    }
  }

  const auto checksum_before_codec_path = write_fixture(
      "mmxisf-m3-checksum-before-codec.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
          "colorSpace=\"RGB\" compression=\"zlib:6\" "
          "checksum=\"sha1:89a1ac94f2d9b748cedfd823d6ff59078736ff4a\" "
          "location=\"attachment:1024:14\"/>") +
          valid_metadata() + "</xisf>",
      std::vector<std::byte>(zlib_rgb_compressed.size(), std::byte{0}));
  auto checksum_before_codec =
      mmxisf::Reader::open_file(checksum_before_codec_path);
  expect(checksum_before_codec.has_value(),
         "checksum-before-codec fixture opens");
  if (checksum_before_codec) {
    auto image = checksum_before_codec.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::checksum_mismatch,
           "failed checksum prevents invalid compressed bytes reaching codec");
  }

  struct InvalidChecksumCase {
    const char *name;
    const char *checksum;
    mmxisf::ErrorCode expected_error;
  };
  const std::array invalid_checksum_cases{
      InvalidChecksumCase{"mismatch",
                          "sha1:0000000000000000000000000000000000000000",
                          mmxisf::ErrorCode::checksum_mismatch},
      InvalidChecksumCase{"uppercase",
                          "sha1:5D211bad8f4ee70e16c7d343a838fc344a1ed961",
                          mmxisf::ErrorCode::invalid_block},
      InvalidChecksumCase{"short", "sha256:00",
                          mmxisf::ErrorCode::invalid_block},
      InvalidChecksumCase{
          "sha3-inspect",
          "sha3-256:"
          "0000000000000000000000000000000000000000000000000000000000000000",
          mmxisf::ErrorCode::unsupported_feature}};
  for (const auto &test : invalid_checksum_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m3-invalid-checksum-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
            "colorSpace=\"RGB\" checksum=\"") +
            test.checksum + "\" location=\"attachment:1024:6\"/>" +
            valid_metadata() + "</xisf>",
        zlib_rgb_pixels);
    auto reader = mmxisf::Reader::open_file(path);
    expect(reader.has_value(), test.name);
    if (reader) {
      auto image = reader.value().read_image(0);
      expect(!image && image.error().code == test.expected_error, test.name);
    }
  }

  const auto embedded_wrong_level_path = write_fixture(
      "mmxisf-m3-embedded-wrong-level.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"embedded\" compression=\"zlib:1\">"
          "<Data encoding=\"base64\">eJxjBAAAAgAC</Data></Image>") +
          valid_metadata() + "</xisf>");
  auto embedded_wrong_level =
      mmxisf::Reader::open_file(embedded_wrong_level_path);
  expect(!embedded_wrong_level && embedded_wrong_level.error().code ==
                                      mmxisf::ErrorCode::invalid_xisf,
         "embedded compression attributes on Image are rejected");

  struct InvalidCompressionCase {
    const char *name;
    const char *compression;
    const char *subblocks;
    mmxisf::ErrorCode expected_error;
  };
  const std::array invalid_compression_cases{
      InvalidCompressionCase{"extra-component", "zlib:6:1", "",
                             mmxisf::ErrorCode::invalid_block},
      InvalidCompressionCase{"size-mismatch", "zlib:7", "",
                             mmxisf::ErrorCode::invalid_block},
      InvalidCompressionCase{"subblock-totals", "zlib:6", "13,6",
                             mmxisf::ErrorCode::invalid_block},
      InvalidCompressionCase{"unknown", "brotli:6", "",
                             mmxisf::ErrorCode::unsupported_feature}};
  for (const auto &test : invalid_compression_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m3-invalid-compression-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"2:1:3\" sampleFormat=\"UInt8\" "
            "colorSpace=\"RGB\" compression=\"") +
            test.compression + "\"" +
            (std::string_view(test.subblocks).empty()
                 ? ""
                 : std::string(" subblocks=\"") + test.subblocks + "\"") +
            " location=\"attachment:1024:14\"/>" + valid_metadata() + "</xisf>",
        zlib_rgb_compressed);
    auto reader = mmxisf::Reader::open_file(path);
    expect(reader.has_value(), test.name);
    if (reader) {
      auto image = reader.value().read_image(0);
      expect(!image && image.error().code == test.expected_error, test.name);
    }
  }

  const auto ratio_limit_path = write_fixture(
      "mmxisf-m3-ratio-limit.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"8193:1:1\" sampleFormat=\"UInt8\" "
          "compression=\"zlib:8193\" "
          "location=\"attachment:1024:1\"/>") +
          valid_metadata() + "</xisf>",
      {std::byte{0}});
  auto ratio_limit = mmxisf::Reader::open_file(ratio_limit_path);
  expect(ratio_limit.has_value(), "decompression ratio fixture opens");
  if (ratio_limit) {
    auto image = ratio_limit.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::resource_limit,
           "decompression ratio is rejected before codec invocation");
  }

  mmxisf::ReaderOptions serialized_limit_options;
  serialized_limit_options.max_serialized_image_bytes = 13;
  auto serialized_limit =
      mmxisf::Reader::open_file(zlib_attachment_path, serialized_limit_options);
  expect(serialized_limit.has_value(), "serialized-size limit fixture opens");
  if (serialized_limit) {
    auto image = serialized_limit.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::resource_limit,
           "serialized image byte limit is enforced before allocation");
  }

  mmxisf::ReaderOptions subblock_limit_options;
  subblock_limit_options.max_compressed_subblocks = 1;
  auto subblock_limit =
      mmxisf::Reader::open_file(zlib_subblocks_path, subblock_limit_options);
  expect(subblock_limit.has_value(), "subblock-count limit fixture opens");
  if (subblock_limit) {
    auto image = subblock_limit.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::resource_limit,
           "compression subblock count limit is enforced");
  }

  const std::array invalid_color_channel_cases{std::pair{"RGB", "2"},
                                               std::pair{"CIELab", "2"}};
  for (const auto &[color_space, channel_count] : invalid_color_channel_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m2-invalid-") + color_space + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"1:1:") +
            channel_count + "\" sampleFormat=\"UInt8\" colorSpace=\"" +
            color_space + "\" location=\"attachment:1024:2\"/>" +
            valid_metadata() + "</xisf>",
        {std::byte{0}, std::byte{0}});
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == mmxisf::ErrorCode::invalid_xisf,
           color_space);
  }

  struct EmbeddedDecodeCase {
    const char *name;
    const char *encoding;
    const char *encoded;
    const char *geometry;
    const char *sample_format;
    std::vector<std::byte> expected;
  };
  const std::array embedded_decode_cases{
      EmbeddedDecodeCase{"base64-rgb",
                         "base64",
                         " AAEC\nAwQF ",
                         "2:1:3",
                         "UInt8",
                         {std::byte{0x00}, std::byte{0x01}, std::byte{0x02},
                          std::byte{0x03}, std::byte{0x04}, std::byte{0x05}}},
      EmbeddedDecodeCase{"base64-padding",
                         "base64",
                         "EjQ=",
                         "1:1:1",
                         "UInt16",
                         {std::byte{0x12}, std::byte{0x34}}},
      EmbeddedDecodeCase{"hex-rgb",
                         "hex",
                         " 0011\n22334455 ",
                         "2:1:3",
                         "UInt8",
                         {std::byte{0x00}, std::byte{0x11}, std::byte{0x22},
                          std::byte{0x33}, std::byte{0x44}, std::byte{0x55}}},
  };
  for (const auto &test : embedded_decode_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m2-embedded-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"") +
            test.geometry + "\" sampleFormat=\"" + test.sample_format +
            "\" colorSpace=\"" +
            (std::string_view(test.geometry).ends_with(":3") ? "RGB" : "Gray") +
            "\" location=\"embedded\"><Data encoding=\"" + test.encoding +
            "\">" + test.encoded + "</Data></Image>" + valid_metadata() +
            "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(result.has_value(), test.name);
    if (result) {
      expect(result.value().document().images()[0].block.kind ==
                 mmxisf::BlockKind::embedded,
             test.name);
      auto image = result.value().read_image(0);
      expect(image && image.value().pixels == test.expected, test.name);
      std::vector<std::byte> destination(test.expected.size());
      auto into = result.value().read_image_into(0, destination);
      expect(into && into.value() == test.expected.size() &&
                 destination == test.expected,
             test.name);
      if (std::string_view(test.name) == "base64-rgb") {
        mmxisf::ImageReadOptions options;
        options.pixel_storage = mmxisf::PixelStorageOutput::normal;
        auto transformed = result.value().read_image(0, options);
        expect(transformed &&
                   transformed.value().pixels ==
                       std::vector<std::byte>{std::byte{0x00}, std::byte{0x02},
                                              std::byte{0x04}, std::byte{0x01},
                                              std::byte{0x03}, std::byte{0x05}},
               "embedded Planar RGB transforms to Normal order");
      }
    }
  }

  struct InvalidEmbeddedCase {
    const char *name;
    const char *image_body;
    mmxisf::ErrorCode expected_error;
  };
  const std::array invalid_embedded_cases{
      InvalidEmbeddedCase{"missing-data", "", mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"invalid-encoding",
                          "<Data encoding=\"Base64\">AA==</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"uppercase-hex", "<Data encoding=\"hex\">0A</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"odd-hex", "<Data encoding=\"hex\">0</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"base64-character",
                          "<Data encoding=\"base64\">A?==</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"base64-incomplete",
                          "<Data encoding=\"base64\">AAA</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"base64-pad-bits",
                          "<Data encoding=\"base64\">AB==</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"base64-after-padding",
                          "<Data encoding=\"base64\">AA==AA==</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"duplicate-data",
                          "<Data encoding=\"base64\">AA==</Data>"
                          "<Data encoding=\"base64\">AA==</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"data-child",
                          "<Data encoding=\"base64\"><Property id=\"p\" "
                          "type=\"String\" value=\"x\"/></Data>",
                          mmxisf::ErrorCode::invalid_xisf},
      InvalidEmbeddedCase{"text-outside-data",
                          "x<Data encoding=\"base64\">AA==</Data>",
                          mmxisf::ErrorCode::invalid_xisf},
  };
  for (const auto &test : invalid_embedded_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-m2-invalid-embedded-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
            "location=\"embedded\">") +
            test.image_body + "</Image>" + valid_metadata() + "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == test.expected_error, test.name);
  }

  const auto data_on_attachment_path = write_fixture(
      "mmxisf-m2-data-on-attachment.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"attachment:1024:1\">"
          "<Data encoding=\"base64\">AA==</Data></Image>") +
          valid_metadata() + "</xisf>",
      {std::byte{0}});
  auto data_on_attachment = mmxisf::Reader::open_file(data_on_attachment_path);
  expect(!data_on_attachment &&
             data_on_attachment.error().code == mmxisf::ErrorCode::invalid_xisf,
         "Data child on attachment Image is rejected");

  const auto embedded_size_mismatch_path = write_fixture(
      "mmxisf-m2-embedded-size-mismatch.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt16\" "
          "location=\"embedded\"><Data encoding=\"base64\">AA==</Data>"
          "</Image>") +
          valid_metadata() + "</xisf>");
  auto embedded_size_mismatch =
      mmxisf::Reader::open_file(embedded_size_mismatch_path);
  expect(embedded_size_mismatch.has_value(),
         "embedded size mismatch remains inspectable");
  if (embedded_size_mismatch) {
    auto image = embedded_size_mismatch.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::invalid_block,
           "embedded size mismatch is rejected before pixel delivery");
  }

  mmxisf::ReaderOptions tiny_encoded_block_limit;
  tiny_encoded_block_limit.max_encoded_block_bytes = 3;
  const auto encoded_limit_path = write_fixture(
      "mmxisf-m2-embedded-encoded-limit.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"embedded\"><Data encoding=\"base64\">AA==</Data>"
          "</Image>") +
          valid_metadata() + "</xisf>");
  auto encoded_limit =
      mmxisf::Reader::open_file(encoded_limit_path, tiny_encoded_block_limit);
  expect(!encoded_limit &&
             encoded_limit.error().code == mmxisf::ErrorCode::resource_limit,
         "embedded encoded-byte budget is enforced while parsing");

  mmxisf::ReaderOptions tiny_embedded_decoded_limit;
  tiny_embedded_decoded_limit.max_decoded_image_bytes = 1;
  const auto decoded_limit_path = write_fixture(
      "mmxisf-m2-embedded-decoded-limit.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt16\" "
          "location=\"embedded\"><Data encoding=\"base64\">EjQ=</Data>"
          "</Image>") +
          valid_metadata() + "</xisf>");
  auto decoded_limit = mmxisf::Reader::open_file(decoded_limit_path,
                                                 tiny_embedded_decoded_limit);
  expect(!decoded_limit &&
             decoded_limit.error().code == mmxisf::ErrorCode::resource_limit,
         "embedded decoded-byte budget is enforced while parsing");

  const auto embedded_memory_path = write_fixture(
      "mmxisf-m2-embedded-memory-source.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"embedded\"><Data encoding=\"base64\">Kg==</Data>"
          "</Image>") +
          valid_metadata() + "</xisf>");
  auto embedded_memory_source =
      std::make_shared<MemoryByteSource>(read_bytes(embedded_memory_path));
  auto embedded_memory_reader =
      mmxisf::Reader::open_source(embedded_memory_source);
  expect(embedded_memory_reader.has_value(),
         "embedded image opens through a custom ByteSource");
  if (embedded_memory_reader) {
    const auto calls_after_open = embedded_memory_source->read_calls;
    auto image = embedded_memory_reader.value().read_image(0);
    expect(image &&
               image.value().pixels == std::vector<std::byte>{std::byte{0x2a}},
           "embedded image bytes are retained exactly");
    expect(embedded_memory_source->read_calls == calls_after_open,
           "embedded image read performs no post-header source I/O");
    std::stop_source embedded_stop;
    embedded_stop.request_stop();
    auto cancelled =
        embedded_memory_reader.value().read_image(0, embedded_stop.get_token());
    expect(!cancelled && cancelled.error().code == mmxisf::ErrorCode::cancelled,
           "embedded image read observes pre-cancellation");
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

  const auto missing_declaration_path = write_fixture(
      "mmxisf-missing-declaration.xisf",
      std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                  "version=\"1.0\">") +
          valid_metadata() + "</xisf>",
      {}, 1024, false);
  auto missing_declaration =
      mmxisf::Reader::open_file(missing_declaration_path);
  expect(!missing_declaration && missing_declaration.error().code ==
                                     mmxisf::ErrorCode::invalid_xisf,
         "mandatory XML declaration is enforced");

  const auto noncanonical_declaration_path = write_fixture(
      "mmxisf-noncanonical-declaration.xisf",
      std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                  "<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                  "version=\"1.0\">") +
          valid_metadata() + "</xisf>",
      {}, 1024, false);
  auto noncanonical_declaration =
      mmxisf::Reader::open_file(noncanonical_declaration_path);
  expect(!noncanonical_declaration && noncanonical_declaration.error().code ==
                                          mmxisf::ErrorCode::invalid_xisf,
         "canonical XML 1.0 UTF-8 declaration is enforced");

  const auto root_text_path = write_fixture(
      "mmxisf-root-text.xisf",
      std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                  "version=\"1.0\">not allowed") +
          valid_metadata() + "</xisf>");
  auto root_text = mmxisf::Reader::open_file(root_text_path);
  expect(!root_text &&
             root_text.error().code == mmxisf::ErrorCode::invalid_xisf,
         "XISF root character data is rejected");

  const auto root_whitespace_path = write_fixture(
      "mmxisf-root-whitespace.xisf",
      std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                  "version=\"1.0\">\n  ") +
          valid_metadata() + "\n</xisf>");
  auto root_whitespace = mmxisf::Reader::open_file(root_whitespace_path);
  expect(root_whitespace.has_value(),
         "XML whitespace between root children remains valid");

  const auto forward_reference_path = write_fixture(
      "mmxisf-forward-reference.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Reference ref=\"later\"/>"
          "<Property uid=\"later\" id=\"p\" type=\"Int32\" value=\"1\"/>") +
          valid_metadata() + "</xisf>");
  auto forward_reference = mmxisf::Reader::open_file(forward_reference_path);
  expect(forward_reference.has_value(),
         "forward Reference to a core uid is accepted");

  const auto ordered_metadata_bindings_path = write_fixture(
      "mmxisf-ordered-metadata-bindings.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"path(pixels.bin)\">"
          "<FITSKeyword name=\"HISTORY\" value=\"\" comment=\"first\"/>"
          "<Reference ref=\"shared_filter\"/>"
          "<FITSKeyword name=\"HISTORY\" value=\"\" comment=\"last\"/>"
          "<Reference ref=\"shared_filter\"/>"
          "</Image>"
          "<FITSKeyword uid=\"shared_filter\" name=\"FILTER\" "
          "value=\"'L'\" comment=\"shared\"/>") +
          valid_metadata() + "</xisf>");
  auto ordered_metadata_bindings =
      mmxisf::Reader::open_file(ordered_metadata_bindings_path);
  expect(ordered_metadata_bindings.has_value(),
         "forward metadata references are resolved");
  if (ordered_metadata_bindings) {
    const auto &document = ordered_metadata_bindings.value().document();
    const auto &bindings = document.metadata_bindings();
    expect(bindings.size() == 6,
           "direct and referenced metadata bindings are retained");
    expect(bindings[0].metadata_index == 0 && !bindings[0].by_reference &&
               bindings[0].image_index == 0 &&
               bindings[1].metadata_index == 2 &&
               bindings[1].by_reference &&
               bindings[2].metadata_index == 1 &&
               !bindings[2].by_reference &&
               bindings[3].metadata_index == 2 &&
               bindings[3].by_reference,
           "image metadata binding order and duplicate references are exact");
    expect(document.metadata()[2].scope ==
                   mmxisf::MetadataEntry::Scope::standalone &&
               document.metadata()[2].uid == "shared_filter",
           "resolved metadata retains standalone lexical provenance");
  }

  const auto invalid_unit_fits_binding_path = write_fixture(
      "mmxisf-invalid-unit-fits-binding.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<FITSKeyword uid=\"shared\" name=\"FILTER\" value=\"'L'\" "
      "comment=\"shared\"/>"
      "<Metadata><Reference ref=\"shared\"/>"
      "<Property id=\"XISF:CreationTime\" type=\"TimePoint\" "
      "value=\"2026-09-13T00:00:00Z\"/>"
      "<Property id=\"XISF:CreatorApplication\" type=\"String\">test"
      "</Property></Metadata></xisf>");
  auto invalid_unit_fits_binding =
      mmxisf::Reader::open_file(invalid_unit_fits_binding_path);
  expect(!invalid_unit_fits_binding &&
             invalid_unit_fits_binding.error().code ==
                 mmxisf::ErrorCode::invalid_xisf,
         "FITSKeyword cannot be associated with XISF-unit metadata");

  struct InvalidUidCase {
    const char *name;
    const char *uid;
  };
  const std::array invalid_uid_cases{
      InvalidUidCase{"empty", ""},
      InvalidUidCase{"leading-digit", "1bad"},
      InvalidUidCase{"hyphen", "bad-id"},
      InvalidUidCase{"non-ascii", "zażółć"},
  };
  for (const auto &test : invalid_uid_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-invalid-uid-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Property uid=\"") +
            test.uid + "\" id=\"p\" type=\"String\" value=\"x\"/>" +
            valid_metadata() + "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == mmxisf::ErrorCode::invalid_xisf,
           test.name);
  }

  const std::array invalid_reference_elements{
      "<Reference/>",
      "<Reference ref=\"missing\"/>",
      "<Reference ref=\"bad-id\"/>",
      "<Reference uid=\"self\" ref=\"self\"/>",
  };
  for (std::size_t index = 0; index < invalid_reference_elements.size();
       ++index) {
    const auto path = write_fixture(
        "mmxisf-invalid-reference-" + std::to_string(index) + ".xisf",
        std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                    "version=\"1.0\">") +
            invalid_reference_elements[index] + valid_metadata() + "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == mmxisf::ErrorCode::invalid_xisf,
           "invalid Reference contract is rejected");
  }

  const auto duplicate_uid_path = write_fixture(
      "mmxisf-duplicate-uid.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Property uid=\"duplicate\" id=\"p1\" type=\"Int32\" value=\"1\"/>"
          "<Property uid=\"duplicate\" id=\"p2\" type=\"Int32\" "
          "value=\"2\"/>") +
          valid_metadata() + "</xisf>");
  auto duplicate_uid = mmxisf::Reader::open_file(duplicate_uid_path);
  expect(!duplicate_uid &&
             duplicate_uid.error().code == mmxisf::ErrorCode::invalid_xisf,
         "core element uid values must be unique");

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

  const auto missing_metadata_property_path = write_fixture(
      "mmxisf-missing-metadata-property.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Metadata><Property id=\"XISF:CreatorApplication\" "
      "type=\"String\">test</Property></Metadata></xisf>");
  auto missing_metadata_property =
      mmxisf::Reader::open_file(missing_metadata_property_path);
  expect(!missing_metadata_property && missing_metadata_property.error().code ==
                                           mmxisf::ErrorCode::invalid_xisf,
         "mandatory Metadata properties are enforced");

  const auto wrong_metadata_type_path = write_fixture(
      "mmxisf-wrong-metadata-type.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Metadata><Property id=\"XISF:CreationTime\" type=\"UInt32\">"
      "now</Property><Property id=\"XISF:CreatorApplication\" "
      "type=\"String\">test</Property></Metadata></xisf>");
  auto wrong_metadata_type =
      mmxisf::Reader::open_file(wrong_metadata_type_path);
  expect(!wrong_metadata_type && wrong_metadata_type.error().code ==
                                     mmxisf::ErrorCode::invalid_xisf,
         "mandatory Metadata property types are enforced");

  const auto string_creation_time_path = write_fixture(
      "mmxisf-string-creation-time.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Metadata><Property id=\"XISF:CreationTime\" type=\"String\">"
      "2026-09-13T00:00:00Z</Property>"
      "<Property id=\"XISF:CreatorApplication\" "
      "type=\"String\">PixInsight</Property></Metadata></xisf>");
  auto string_creation_time =
      mmxisf::Reader::open_file(string_creation_time_path);
  expect(string_creation_time.has_value(),
         "PixInsight String CreationTime compatibility is retained");

  mmxisf::ReaderOptions tiny_metadata_limit;
  tiny_metadata_limit.max_metadata_value_bytes = 2;
  auto limited_metadata =
      mmxisf::Reader::open_file(valid_path, tiny_metadata_limit);
  expect(!limited_metadata &&
             limited_metadata.error().code == mmxisf::ErrorCode::resource_limit,
         "metadata value budget is enforced");

  struct MetadataValueCase {
    const char *name;
    const char *element;
    bool accepted;
  };
  const std::array metadata_value_cases{
      MetadataValueCase{"text-at-limit",
                        "<Property id=\"p\" type=\"String\">abcd</Property>",
                        true},
      MetadataValueCase{"text-over-limit",
                        "<Property id=\"p\" type=\"String\">abcde</Property>",
                        false},
      MetadataValueCase{"attribute-at-limit",
                        "<Property id=\"p\" type=\"Int32\" value=\"1234\"/>",
                        true},
      MetadataValueCase{"attribute-over-limit",
                        "<Property id=\"p\" type=\"Int32\" value=\"12345\"/>",
                        false},
      MetadataValueCase{
          "fits-attribute-over-limit",
          "<FITSKeyword name=\"TEST\" value=\"abcde\" comment=\"\"/>", false},
  };
  for (const auto &test : metadata_value_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-metadata-limit-") + test.name + ".xisf",
        std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                    "version=\"1.0\">") +
            test.element + short_metadata() + "</xisf>");
    mmxisf::ReaderOptions options;
    options.max_metadata_value_bytes = 4;
    auto result = mmxisf::Reader::open_file(path, options);
    expect(test.accepted ? result.has_value()
                         : (!result && result.error().code ==
                                           mmxisf::ErrorCode::resource_limit),
           test.name);
  }

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
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:2:1\" sampleFormat=\"UInt16\" "
          "colorSpace=\"Gray\" location=\"attachment:1:8\"/>") +
          valid_metadata() + "</xisf>",
      pixels);
  auto bad_attachment = mmxisf::Reader::open_file(bad_attachment_path);
  expect(!bad_attachment &&
             bad_attachment.error().code == mmxisf::ErrorCode::invalid_block,
         "attachment overlapping the header is rejected");

  auto nonzero_padding_bytes = read_bytes(valid_path);
  const auto header_length =
      static_cast<std::uint32_t>(nonzero_padding_bytes[8]) |
      (static_cast<std::uint32_t>(nonzero_padding_bytes[9]) << 8U) |
      (static_cast<std::uint32_t>(nonzero_padding_bytes[10]) << 16U) |
      (static_cast<std::uint32_t>(nonzero_padding_bytes[11]) << 24U);
  const auto first_unused_byte = 16U + static_cast<std::size_t>(header_length);
  expect(first_unused_byte < 1024,
         "test fixture has unused space before its attachment");
  nonzero_padding_bytes[first_unused_byte] = std::byte{1};
  const auto nonzero_padding_path =
      write_bytes("mmxisf-nonzero-unused-space.xisf", nonzero_padding_bytes);
  auto nonzero_padding = mmxisf::Reader::open_file(nonzero_padding_path);
  expect(!nonzero_padding &&
             nonzero_padding.error().code == mmxisf::ErrorCode::invalid_block,
         "nonzero byte in unused monolithic space is rejected");

  mmxisf::ReaderOptions zero_unused_space_budget;
  zero_unused_space_budget.max_unused_space_bytes = 0;
  auto unused_space_limit =
      mmxisf::Reader::open_file(valid_path, zero_unused_space_budget);
  expect(!unused_space_limit && unused_space_limit.error().code ==
                                    mmxisf::ErrorCode::resource_limit,
         "unused-space validation byte budget is enforced");

  const auto overlapping_attachments_path = write_fixture(
      "mmxisf-overlapping-attachments.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt16\" "
          "location=\"attachment:1024:2\"/>"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt16\" "
          "location=\"attachment:1025:2\"/>") +
          valid_metadata() + "</xisf>",
      {std::byte{0}, std::byte{0}, std::byte{0}});
  auto overlapping_attachments =
      mmxisf::Reader::open_file(overlapping_attachments_path);
  expect(!overlapping_attachments && overlapping_attachments.error().code ==
                                         mmxisf::ErrorCode::invalid_block,
         "overlapping attached blocks are rejected");

  const auto extension_attachment_path = write_fixture(
      "mmxisf-extension-attachment.xisf",
      std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                  "xmlns:ext=\"urn:mmxisf:test\" version=\"1.0\">"
                  "<ext:Block location=\"attachment:1024:2\"/>"
                  "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
                  "location=\"attachment:1026:1\"/>") +
          valid_metadata() + "</xisf>",
      {std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}});
  auto extension_attachment =
      mmxisf::Reader::open_file(extension_attachment_path);
  expect(extension_attachment.has_value(),
         "extension attachment is inventoried for unused-space validation");
  if (extension_attachment) {
    auto image = extension_attachment.value().read_image(0);
    expect(image &&
               image.value().pixels == std::vector<std::byte>{std::byte{0xcc}},
           "image following an extension attachment is read exactly");
  }

  const auto nonzero_trailing_path = write_fixture(
      "mmxisf-nonzero-trailing-space.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"attachment:1024:1\"/>") +
          valid_metadata() + "</xisf>",
      {std::byte{0x01}, std::byte{0x02}});
  auto nonzero_trailing = mmxisf::Reader::open_file(nonzero_trailing_path);
  expect(!nonzero_trailing &&
             nonzero_trailing.error().code == mmxisf::ErrorCode::invalid_block,
         "unreferenced nonzero trailing bytes are rejected");

  struct AttachmentBoundaryCase {
    const char *name;
    const char *location;
  };
  const std::array attachment_boundary_cases{
      AttachmentBoundaryCase{"zero-size", "attachment:1024:0"},
      AttachmentBoundaryCase{"offset-at-eof", "attachment:1025:1"},
      AttachmentBoundaryCase{"maximum-offset",
                             "attachment:18446744073709551615:1"},
      AttachmentBoundaryCase{"maximum-size",
                             "attachment:1024:18446744073709551615"},
  };
  for (const auto &test : attachment_boundary_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-attachment-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
            "colorSpace=\"Gray\" location=\"") +
            test.location + "\"/>" + valid_metadata() + "</xisf>",
        {std::byte{0}});
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == mmxisf::ErrorCode::invalid_block,
           test.name);
  }

  struct GeometryOverflowCase {
    const char *name;
    const char *geometry;
    const char *sample_format;
  };
  const std::array geometry_overflow_cases{
      GeometryOverflowCase{"width-times-height", "18446744073709551615:2:1",
                           "UInt8"},
      GeometryOverflowCase{"two-large-axes", "4294967296:4294967296:1",
                           "UInt8"},
      GeometryOverflowCase{"sample-byte-count", "9223372036854775808:1:1",
                           "UInt16"},
  };
  for (const auto &test : geometry_overflow_cases) {
    const auto path = write_fixture(
        std::string("mmxisf-overflow-") + test.name + ".xisf",
        std::string(
            "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
            "<Image geometry=\"") +
            test.geometry + "\" sampleFormat=\"" + test.sample_format +
            "\" colorSpace=\"Gray\" location=\"attachment:1024:1\"/>" +
            valid_metadata() + "</xisf>",
        {std::byte{0}});
    auto result = mmxisf::Reader::open_file(path);
    expect(result.has_value(), test.name);
    if (result) {
      auto image = result.value().read_image(0);
      expect(!image && image.error().code == mmxisf::ErrorCode::overflow,
             test.name);
    }
  }

  const auto short_attachment_path = write_fixture(
      "mmxisf-attachment-size-mismatch.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"2:2:1\" sampleFormat=\"UInt16\" "
          "colorSpace=\"Gray\" location=\"attachment:1024:7\"/>") +
          valid_metadata() + "</xisf>",
      std::vector<std::byte>(7, std::byte{0}));
  auto short_attachment = mmxisf::Reader::open_file(short_attachment_path);
  expect(short_attachment.has_value(),
         "in-range attachment remains inspectable before typed decode");
  if (short_attachment) {
    auto image = short_attachment.value().read_image(0);
    expect(!image && image.error().code == mmxisf::ErrorCode::invalid_block,
           "attachment size must match geometry at typed decode");
  }

  const auto bad_property_path = write_fixture(
      "mmxisf-property-attributes.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Property id=\"missing-type\"/><Metadata/></xisf>");
  auto bad_property = mmxisf::Reader::open_file(bad_property_path);
  expect(!bad_property &&
             bad_property.error().code == mmxisf::ErrorCode::invalid_xisf,
         "Property mandatory attributes are enforced");

  const auto property_forms_path = write_fixture(
      "mmxisf-property-forms.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Property id=\"Test:Scalar\" type=\"Int32\" value=\"-7\"/>"
          "<Property id=\"Test:Complex\" type=\"Complex64\" "
          "value=\"(1.5,-2)\"/>"
          "<Property id=\"Test:String\" type=\"String\"> value </Property>"
          "<Property id=\"Test:Time\" type=\"TimePoint\" "
          "value=\"2026-09-13T00:00:00Z\"/>"
          "<Property id=\"Test:Vector\" type=\"F64Vector\" length=\"1\" "
          "location=\"inline:base64\">AAAAAAAAAAA=</Property>"
          "<Property id=\"Test:Matrix\" type=\"F64Matrix\" rows=\"1\" "
          "columns=\"1\" location=\"inline:base64\">AAAAAAAAAAA=</Property>") +
          valid_metadata() + "</xisf>");
  auto property_forms = mmxisf::Reader::open_file(property_forms_path);
  expect(property_forms.has_value(),
         "all Property serialization categories are accepted");
  if (property_forms) {
    expect(property_forms.value().document().metadata()[2].value == " value ",
           "String Property whitespace remains significant");
  }

  struct PropertyValueCase {
    const char *name;
    const char *type;
    const char *value;
  };
  const std::array valid_property_values{
      PropertyValueCase{"boolean-alpha-true", "Boolean", " true "},
      PropertyValueCase{"boolean-alpha-false", "Boolean", "false"},
      PropertyValueCase{"boolean-numeric-zero", "Boolean", "0"},
      PropertyValueCase{"boolean-numeric-one", "Boolean", "1"},
      PropertyValueCase{"integer-zero", "Int32", "0"},
      PropertyValueCase{"integer-signed", "Int64", " -42 "},
      PropertyValueCase{"integer-binary", "UInt32", "0b101001"},
      PropertyValueCase{"integer-octal", "UInt32", "0O570261"},
      PropertyValueCase{"integer-hex", "Int32", "0x80E950AB"},
      PropertyValueCase{"int8-minimum", "Int8", "-128"},
      PropertyValueCase{"int8-maximum", "Int8", "127"},
      PropertyValueCase{"int8-full-bit-pattern", "Int8", "0xff"},
      PropertyValueCase{"uint16-octal-maximum", "UInt16", "0o177777"},
      PropertyValueCase{"uint64-maximum", "UInt64",
                        "18446744073709551615"},
      PropertyValueCase{"int128-minimum", "Int128",
                        "-170141183460469231731687303715884105728"},
      PropertyValueCase{"int128-maximum", "Int128",
                        "170141183460469231731687303715884105727"},
      PropertyValueCase{"uint128-maximum", "UInt128",
                        "340282366920938463463374607431768211455"},
      PropertyValueCase{"float-integer-form", "Float32", "123"},
      PropertyValueCase{"float-leading-dot", "Float64", "+.123"},
      PropertyValueCase{"float-exponent", "Double", "-0.123e+02"},
      PropertyValueCase{"float-nan", "Float64", "NaN"},
      PropertyValueCase{"float-positive-infinity", "Float64", "+Inf"},
      PropertyValueCase{"float-negative-infinity", "Float64", "-Inf"},
      PropertyValueCase{"complex", "Complex64", "( 1.5, -2e0 )"},
  };
  for (const auto &test : valid_property_values) {
    const auto path = write_fixture(
        std::string("mmxisf-valid-property-value-") + test.name + ".xisf",
        std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                    "version=\"1.0\"><Property id=\"Test:Value\" type=\"") +
            test.type + "\" value=\"" + test.value + "\"/>" +
            valid_metadata() + "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(result.has_value(), test.name);
    if (result) {
      expect(result.value().document().metadata()[0].value == test.value,
             "validated scalar source representation is preserved");
    }
  }

  const std::array valid_time_points{
      "2024-02-29T23:59:60Z", "2026-09-13T00:00:00.125+02:00",
      "2026-09-13T00:00:00-07:30", "2026-09-13T00:00:00",
      " 2026-09-13t00:00:00z "};
  for (std::size_t index = 0; index < valid_time_points.size(); ++index) {
    const auto path = write_fixture(
        "mmxisf-valid-time-point-" + std::to_string(index) + ".xisf",
        std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                    "version=\"1.0\"><Property id=\"Test:Time\" "
                    "type=\"TimePoint\" value=\"") +
            valid_time_points[index] + "\"/>" + valid_metadata() +
            "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(result.has_value(), "supported ISO 8601 TimePoint form is accepted");
  }

  const std::array invalid_property_values{
      PropertyValueCase{"boolean-word", "Boolean", "yes"},
      PropertyValueCase{"boolean-number", "Boolean", "2"},
      PropertyValueCase{"integer-leading-zero", "Int32", "00"},
      PropertyValueCase{"integer-fraction", "Int32", "1.0"},
      PropertyValueCase{"integer-empty-hex", "UInt32", "0x"},
      PropertyValueCase{"integer-bad-binary", "UInt32", "0b102"},
      PropertyValueCase{"unsigned-negative", "UInt32", "-1"},
      PropertyValueCase{"int8-positive-overflow", "Int8", "128"},
      PropertyValueCase{"int8-negative-overflow", "Int8", "-129"},
      PropertyValueCase{"uint8-overflow", "UInt8", "256"},
      PropertyValueCase{"uint8-hex-overflow", "UInt8", "0x100"},
      PropertyValueCase{"uint16-octal-overflow", "UInt16", "0o200000"},
      PropertyValueCase{"uint64-overflow", "UInt64",
                        "18446744073709551616"},
      PropertyValueCase{"int128-positive-overflow", "Int128",
                        "170141183460469231731687303715884105728"},
      PropertyValueCase{"int128-negative-overflow", "Int128",
                        "-170141183460469231731687303715884105729"},
      PropertyValueCase{"uint128-overflow", "UInt128",
                        "340282366920938463463374607431768211456"},
      PropertyValueCase{"float-trailing-dot", "Float64", "1."},
      PropertyValueCase{"float-unsigned-infinity", "Float64", "Inf"},
      PropertyValueCase{"float-lowercase-nan", "Float64", "nan"},
      PropertyValueCase{"float-empty-exponent", "Float64", "1e"},
      PropertyValueCase{"complex-no-parentheses", "Complex64", "1,2"},
      PropertyValueCase{"complex-no-comma", "Complex64", "(1 2)"},
      PropertyValueCase{"complex-extra-comma", "Complex64", "(1,2,3)"},
  };
  for (const auto &test : invalid_property_values) {
    const auto path = write_fixture(
        std::string("mmxisf-invalid-property-value-") + test.name + ".xisf",
        std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                    "version=\"1.0\"><Property id=\"Test:Value\" type=\"") +
            test.type + "\" value=\"" + test.value + "\"/>" +
            valid_metadata() + "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == mmxisf::ErrorCode::invalid_xisf,
           test.name);
  }

  const std::array invalid_time_points{
      "2023-02-29T00:00:00Z", "2026-13-01T00:00:00Z",
      "2026-09-13T24:00:00Z", "2026-09-13T00:00:00.Z",
      "2026-09-13T00:00:00+24:00", "2026-09-13 00:00:00Z"};
  for (std::size_t index = 0; index < invalid_time_points.size(); ++index) {
    const auto path = write_fixture(
        "mmxisf-invalid-time-point-" + std::to_string(index) + ".xisf",
        std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                    "version=\"1.0\"><Property id=\"Test:Time\" "
                    "type=\"TimePoint\" value=\"") +
            invalid_time_points[index] + "\"/>" + valid_metadata() +
            "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == mmxisf::ErrorCode::invalid_xisf,
           "malformed TimePoint is rejected");
  }

  struct InvalidPropertyFormCase {
    const char *name;
    const char *property;
  };
  const std::array invalid_property_forms{
      InvalidPropertyFormCase{"scalar-without-value",
                              "<Property id=\"p\" type=\"Int32\"/>"},
      InvalidPropertyFormCase{
          "string-with-value",
          "<Property id=\"p\" type=\"String\" value=\"x\"/>"},
      InvalidPropertyFormCase{
          "timepoint-character-data",
          "<Property id=\"p\" type=\"TimePoint\">time</Property>"},
      InvalidPropertyFormCase{
          "timepoint-with-format",
          "<Property id=\"p\" type=\"TimePoint\" value=\"time\" "
          "format=\"width:8\"/>"},
      InvalidPropertyFormCase{
          "vector-without-length",
          "<Property id=\"p\" type=\"F64Vector\" "
          "location=\"inline:base64\"/>"},
      InvalidPropertyFormCase{
          "vector-with-value",
          "<Property id=\"p\" type=\"F64Vector\" length=\"1\" "
          "value=\"1\" location=\"inline:base64\"/>"},
      InvalidPropertyFormCase{
          "matrix-without-columns",
          "<Property id=\"p\" type=\"F64Matrix\" rows=\"1\" "
          "location=\"inline:base64\"/>"},
      InvalidPropertyFormCase{
          "unknown-type",
          "<Property id=\"p\" type=\"Custom\" value=\"1\"/>"},
      InvalidPropertyFormCase{
          "invalid-identifier",
          "<Property id=\"bad-id\" type=\"Int32\" value=\"1\"/>"},
  };
  for (const auto &test : invalid_property_forms) {
    const auto path = write_fixture(
        std::string("mmxisf-invalid-property-form-") + test.name + ".xisf",
        std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                    "version=\"1.0\">") +
            test.property + valid_metadata() + "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == mmxisf::ErrorCode::invalid_xisf,
           test.name);
  }

  const auto duplicate_image_property_path = write_fixture(
      "mmxisf-duplicate-image-property.xisf",
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
          "location=\"path(pixels.bin)\">"
          "<Property id=\"Test:Value\" type=\"Int32\" value=\"1\"/>"
          "<Property id=\"Test:Value\" type=\"Int32\" value=\"2\"/>"
          "</Image>") +
          valid_metadata() + "</xisf>");
  auto duplicate_image_property =
      mmxisf::Reader::open_file(duplicate_image_property_path);
  expect(!duplicate_image_property &&
             duplicate_image_property.error().code ==
                 mmxisf::ErrorCode::invalid_xisf,
         "Property identifiers are unique within an image association");

  const auto bad_fits_parent_path = write_fixture(
      "mmxisf-fits-parent.xisf",
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Metadata><FITSKeyword name=\"TEST\" value=\"1\" "
      "comment=\"bad parent\"/></Metadata></xisf>");
  auto bad_fits_parent = mmxisf::Reader::open_file(bad_fits_parent_path);
  expect(!bad_fits_parent &&
             bad_fits_parent.error().code == mmxisf::ErrorCode::invalid_xisf,
         "FITSKeyword parent grammar is enforced");

  const std::array invalid_fits_names{"lower", "TOO-LONG9", "BAD NAME",
                                      "NON.ASC"};
  for (std::size_t index = 0; index < invalid_fits_names.size(); ++index) {
    const auto path = write_fixture(
        "mmxisf-invalid-fits-name-" + std::to_string(index) + ".xisf",
        std::string("<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
                    "version=\"1.0\"><FITSKeyword name=\"") +
            invalid_fits_names[index] +
            "\" value=\"1\" comment=\"invalid\"/>" + valid_metadata() +
            "</xisf>");
    auto result = mmxisf::Reader::open_file(path);
    expect(!result && result.error().code == mmxisf::ErrorCode::invalid_xisf,
           "invalid FITS keyword name is rejected");
  }

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
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"1:1:1\" sampleFormat=\"Float32\" "
          "bounds=\" +0 : +1 \" location=\"attachment:1024:4\"/>") +
          valid_metadata() + "</xisf>",
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
      std::string(
          "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
          "<Image geometry=\"4:1\" sampleFormat=\"UInt16\" "
          "colorSpace=\"Gray\" location=\"attachment:1024:8\"/>") +
          valid_metadata() + "</xisf>",
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
    expect(!image && image.error().code == mmxisf::ErrorCode::invalid_block,
           "invalid zlib payload fails closed");
  }

  return failures == 0 ? 0 : 1;
}
