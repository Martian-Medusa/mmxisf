// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class MemorySource final : public mmxisf::ByteSource {
public:
  explicit MemorySource(std::vector<std::byte> bytes)
      : bytes_(std::move(bytes)) {}

  mmxisf::Result<std::uint64_t> size() const override {
    return static_cast<std::uint64_t>(bytes_.size());
  }

  mmxisf::Result<std::size_t>
  read_at(std::uint64_t offset,
          std::span<std::byte> destination) const override {
    if (offset > bytes_.size()) {
      mmxisf::Error error;
      error.code = mmxisf::ErrorCode::io_error;
      error.message = "test source offset is outside the fixture";
      return error;
    }
    const auto count = std::min<std::size_t>(
        destination.size(), bytes_.size() - static_cast<std::size_t>(offset));
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), count,
                destination.begin());
    return count;
  }

private:
  std::vector<std::byte> bytes_;
};

int base64_value(char character) {
  if (character >= 'A' && character <= 'Z') {
    return character - 'A';
  }
  if (character >= 'a' && character <= 'z') {
    return character - 'a' + 26;
  }
  if (character >= '0' && character <= '9') {
    return character - '0' + 52;
  }
  if (character == '+') {
    return 62;
  }
  if (character == '/') {
    return 63;
  }
  return -1;
}

std::vector<std::byte> load_base64(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  expect(static_cast<bool>(input), "cannot open encoded interop fixture");
  std::vector<std::byte> result;
  std::array<int, 4> quartet{};
  std::size_t count = 0;
  bool complete = false;
  for (char character; input.get(character);) {
    if (character == ' ' || character == '\t' || character == '\r' ||
        character == '\n') {
      continue;
    }
    expect(!complete, "base64 data follows terminal padding");
    quartet[count++] = character == '=' ? -2 : base64_value(character);
    expect(quartet[count - 1] >= -2 && quartet[count - 1] != -1,
           "invalid base64 fixture character");
    if (count != 4) {
      continue;
    }
    expect(quartet[0] >= 0 && quartet[1] >= 0,
           "invalid base64 fixture padding");
    result.push_back(
        static_cast<std::byte>((quartet[0] << 2) | (quartet[1] >> 4)));
    if (quartet[2] == -2) {
      expect(quartet[3] == -2, "invalid base64 fixture padding");
      complete = true;
    } else {
      expect(quartet[2] >= 0, "invalid base64 fixture quartet");
      result.push_back(static_cast<std::byte>(((quartet[1] & 0x0f) << 4) |
                                              (quartet[2] >> 2)));
      if (quartet[3] == -2) {
        complete = true;
      } else {
        expect(quartet[3] >= 0, "invalid base64 fixture quartet");
        result.push_back(
            static_cast<std::byte>(((quartet[2] & 0x03) << 6) | quartet[3]));
      }
    }
    count = 0;
  }
  expect(count == 0, "incomplete base64 fixture quartet");
  return result;
}

std::string sha256_hex(std::span<const std::byte> bytes) {
  std::array<unsigned char, 32> digest{};
  unsigned int digest_size = 0;
  const auto *data = reinterpret_cast<const unsigned char *>(bytes.data());
  expect(EVP_Digest(data, bytes.size(), digest.data(), &digest_size,
                    EVP_sha256(), nullptr) == 1,
         "cannot hash independent-producer fixture");
  expect(digest_size == digest.size(), "unexpected SHA-256 digest size");
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto value : digest) {
    output << std::setw(2) << static_cast<unsigned int>(value);
  }
  return output.str();
}

void check_fixture(
    const std::filesystem::path &path, std::string_view expected_source_sha256,
    std::string_view expected_compression, mmxisf::SampleFormat expected_format,
    std::string_view expected_color, std::uint64_t expected_width,
    std::uint64_t expected_height, std::uint64_t expected_channels,
    std::size_t expected_pixel_bytes, std::string_view expected_pixel_sha256,
    std::string_view expected_checksum = {}) {
  auto bytes = load_base64(path);
  expect(sha256_hex(bytes) == expected_source_sha256,
         "independent-producer fixture identity changed");
  auto opened = mmxisf::Reader::open_source(
      std::make_shared<MemorySource>(std::move(bytes)));
  expect(opened.has_value(), "independent-producer fixture did not open");
  const auto &images = opened.value().document().images();
  expect(images.size() == 1, "independent fixture image count changed");
  const auto &descriptor = images[0];
  expect(descriptor.geometry == std::vector<std::uint64_t>{expected_width,
                                                           expected_height,
                                                           expected_channels},
         "independent fixture geometry changed");
  expect(descriptor.sample_format == expected_format,
         "independent fixture sample format changed");
  expect(descriptor.color_space == expected_color,
         "independent fixture color space changed");
  expect(descriptor.pixel_storage == mmxisf::PixelStorage::planar,
         "independent fixture pixel storage changed");
  expect(descriptor.byte_order == mmxisf::ByteOrder::little,
         "independent fixture byte order changed");
  expect(expected_compression.empty()
             ? descriptor.compression.empty()
             : descriptor.compression.starts_with(
                   std::string(expected_compression) + ":"),
         "independent fixture codec changed");
  if (!expected_checksum.empty()) {
    expect(descriptor.checksum == expected_checksum,
           "independent fixture checksum descriptor changed");
  }
  auto decoded = opened.value().read_image(0);
  expect(decoded.has_value(), "independent fixture did not decode");
  expect(decoded.value().pixels.size() == expected_pixel_bytes,
         "independent fixture decoded byte count changed");
  expect(sha256_hex(decoded.value().pixels) == expected_pixel_sha256,
         "independent fixture decoded bytes differ from the source array");
  if (!expected_checksum.empty()) {
    expect(decoded.value().checksum_verification ==
               mmxisf::ChecksumVerification::verified,
           "independent fixture checksum was not verified");
  }
}

void check_multi_writer_fixture(const std::filesystem::path &path) {
  auto bytes = load_base64(path);
  expect(sha256_hex(bytes) ==
             "c72c577d090e49d4966b1dda8d20e9948a9d23e5ba1fae3688c3ae34ddeb42ba",
         "multi-image writer fixture identity changed");
  auto opened = mmxisf::Reader::open_source(
      std::make_shared<MemorySource>(std::move(bytes)));
  expect(opened.has_value(), "multi-image writer fixture did not open");
  const auto &images = opened.value().document().images();
  expect(images.size() == 4, "multi-image writer fixture count changed");
  const std::array expected_formats{
      mmxisf::SampleFormat::uint8, mmxisf::SampleFormat::uint32,
      mmxisf::SampleFormat::float32, mmxisf::SampleFormat::float64};
  const std::array<std::string_view, 4> expected_hashes{
      "89273d2f70b93285bb7ddb4bcee86a5347ca7159352e3cbdd20c23e9d1e507d3",
      "8534950f56d583bdf50c1fda159d104fcb79d58532a12b16c5a2151abb1d1d8e",
      "7641eeb488776a3586d213f2bb1dabf0a264526a2089fb5cac2117e194087c28",
      "4cfa5b42ca669328764e67cd9a34bb8f90b16ed7ca8d85e8443783d7ccce15ed"};
  for (std::size_t index = 0; index < images.size(); ++index) {
    expect(images[index].sample_format == expected_formats[index] &&
               images[index].pixel_storage == mmxisf::PixelStorage::planar &&
               images[index].byte_order == mmxisf::ByteOrder::little,
           "multi-image writer descriptor changed");
    auto decoded = opened.value().read_image(index);
    expect(decoded.has_value() &&
               sha256_hex(decoded.value().pixels) == expected_hashes[index],
           "multi-image writer fixture pixels changed");
  }
}

void check_metadata_writer_fixture(const std::filesystem::path &path) {
  auto bytes = load_base64(path);
  expect(sha256_hex(bytes) ==
             "e9a64e68b495aed77da38ce900e490878ef5539a407d6aa10e9a23d562d279f8",
         "metadata writer fixture identity changed");
  auto opened = mmxisf::Reader::open_source(
      std::make_shared<MemorySource>(std::move(bytes)));
  expect(opened.has_value(), "metadata writer fixture did not open");
  const auto &document = opened.value().document();
  expect(document.images().size() == 1 && document.metadata().size() == 6 &&
             document.metadata_bindings().size() == 6,
         "metadata writer fixture shape changed");
  const auto find_entry = [&](std::string_view name) {
    return std::find_if(document.metadata().begin(), document.metadata().end(),
                        [&](const auto &entry) { return entry.name == name; });
  };
  const auto filter = find_entry("Instrument:Filter:Name");
  const auto exposure = find_entry("EXPTIME");
  const auto module = find_entry("XISF:CreatorModule");
  expect(filter != document.metadata().end() && filter->value == "L<&\"" &&
             exposure != document.metadata().end() &&
             exposure->value == "30.5" &&
             exposure->comment == "seconds & more" &&
             module != document.metadata().end() &&
             module->scope == mmxisf::MetadataEntry::Scope::xisf_unit,
         "metadata writer fixture values changed");
  auto decoded = opened.value().read_image(0);
  expect(decoded.has_value() && sha256_hex(decoded.value().pixels) ==
                                    "ea99f710d9d0b8ba192295c969a63ed7ce8fc5743d"
                                    "a20d2057fa2b6d2c404bfb",
         "metadata writer fixture pixels changed");
}

void check_codec_writer_fixture(const std::filesystem::path &path) {
  auto bytes = load_base64(path);
  expect(sha256_hex(bytes) ==
             "78911e120d89a6718765053d82e85fffd5f1c6da0739ff275f3085d7d2e7eaa5",
         "codec writer fixture identity changed");
  auto opened = mmxisf::Reader::open_source(
      std::make_shared<MemorySource>(std::move(bytes)));
  expect(opened.has_value(), "codec writer fixture did not open");
  const auto &images = opened.value().document().images();
  expect(images.size() == 5, "codec writer fixture image count changed");
  const std::array<std::string_view, 5> compression{
      "zlib:512", "lz4+sh:512:2", "lz4hc:512", "zstd+sh:512:2", ""};
  const std::array<std::string_view, 5> checksums{
      "sha-1:", "sha-256:", "sha-512:", "sha-256:", "sha-256:"};
  for (std::size_t index = 0; index < images.size(); ++index) {
    expect(images[index].compression == compression[index] &&
               images[index].checksum.starts_with(checksums[index]),
           "codec writer fixture descriptor changed");
    auto decoded = opened.value().read_image(index);
    expect(decoded.has_value() &&
               decoded.value().checksum_verification ==
                   mmxisf::ChecksumVerification::verified &&
               sha256_hex(decoded.value().pixels) ==
                   "09b05089277895cf05fab154c732c5e8228d7735879075fd1edf57e901f"
                   "07042",
           "codec writer fixture pixels or checksum changed");
  }
}

void check_native_properties_writer_fixture(const std::filesystem::path &path) {
  auto bytes = load_base64(path);
  expect(sha256_hex(bytes) ==
             "b130c2a3b65180b1bf31b64e82bf82740fd105ba8cadda4d91d4355d4a6ea7b6",
         "native Property writer fixture identity changed");
  auto opened = mmxisf::Reader::open_source(
      std::make_shared<MemorySource>(std::move(bytes)));
  expect(opened.has_value(), "native Property writer fixture did not open");
  const auto &document = opened.value().document();
  expect(document.images().size() == 1 && document.metadata().size() == 5 &&
             document.metadata_bindings().size() == 5,
         "native Property writer fixture shape changed");

  std::optional<std::size_t> matrix_index;
  std::optional<std::size_t> vector_index;
  for (std::size_t index = 0; index < document.metadata().size(); ++index) {
    const auto &entry = document.metadata()[index];
    if (entry.name == "Test:Matrix") {
      matrix_index = index;
      expect(entry.image_index == 0 && entry.type == "F64Matrix" &&
                 entry.rows == 2 && entry.columns == 2 &&
                 entry.compression == "zstd+sh:32:8" &&
                 entry.checksum.starts_with("sha-256:"),
             "native matrix Property descriptor changed");
    } else if (entry.name == "Test:Vector") {
      vector_index = index;
      expect(entry.image_index == 0 && entry.type == "UI16Vector" &&
                 entry.length == 2 && entry.compression.empty(),
             "native vector Property descriptor changed");
    } else if (entry.name == "Test:Label") {
      expect(entry.image_index == 0 && entry.type == "String" &&
                 entry.value == "mmxisf native writer validation",
             "native String Property changed");
    }
  }
  expect(matrix_index && vector_index,
         "native block Property descriptors are missing");

  const std::array<std::byte, 32> expected_matrix{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0xf0}, std::byte{0x3f},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x08}, std::byte{0x40},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x10}, std::byte{0x40}};
  const std::array<std::byte, 4> expected_vector{
      std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
  const auto matrix = opened.value().read_property_block(*matrix_index);
  const auto vector = opened.value().read_property_block(*vector_index);
  expect(matrix &&
             matrix.value().bytes ==
                 std::vector<std::byte>(expected_matrix.begin(),
                                        expected_matrix.end()) &&
             matrix.value().checksum_verification ==
                 mmxisf::ChecksumVerification::verified,
         "native matrix Property bytes changed");
  expect(vector && vector.value().bytes ==
                       std::vector<std::byte>(expected_vector.begin(),
                                              expected_vector.end()),
         "native vector Property bytes changed");
  const auto image = opened.value().read_image(0);
  expect(image && sha256_hex(image.value().pixels) ==
                      "ea99f710d9d0b8ba192295c969a63ed7ce8fc5743da20d2057fa2b6d"
                      "2c404bfb",
         "native Property writer fixture pixels changed");
}

} // namespace

int main() {
  try {
    const auto root =
        std::filesystem::path(MMXISF_TEST_SOURCE_DIR) / "tests" / "interop";
    struct Fixture {
      std::string_view name;
      std::string_view source_sha256;
      std::string_view compression;
      mmxisf::SampleFormat sample_format;
      std::string_view color_space;
      std::uint64_t width{257};
      std::uint64_t height{193};
      std::uint64_t channels{1};
      std::size_t pixel_bytes;
      std::string_view pixel_sha256;
      std::string_view checksum{};
    };
    const auto fixtures = std::array<Fixture, 11>{
        {{"gray-u8-zstd-sh.xisf.b64",
          "97b1d65ed17d088a694661be3879fe93152ed7d8fed59222552ca5a12d5365b8",
          "zstd+sh", mmxisf::SampleFormat::uint8, "Gray", 257, 193, 1, 49601,
          "f21988f7824386f4b0a53b95426645e7ceb1834d96461f89e3fde32a3f988f69"},
         {"gray-u16-zlib-sh.xisf.b64",
          "7a5e97af8cb10c57ba787892df9c8050a080a3e4332612937c3f86343501e8f3",
          "zlib+sh", mmxisf::SampleFormat::uint16, "Gray", 257, 193, 1, 99202,
          "6ae7190a5d6d9af4daad32c0ef92c700256f05218cb3c92660cac9655b41891f"},
         {"gray-u16-lz4-sh.xisf.b64",
          "0f1e521e612a9574c1761af9fe41b6acc825c837aa6c1162cc8e89e918f344ee",
          "lz4+sh", mmxisf::SampleFormat::uint16, "Gray", 257, 193, 1, 99202,
          "6ae7190a5d6d9af4daad32c0ef92c700256f05218cb3c92660cac9655b41891f"},
         {"gray-u16-lz4hc-sh.xisf.b64",
          "f33b9cc8f397ef4d8b333b9754d72bfa88795052335a23daafc661b113873131",
          "lz4hc+sh", mmxisf::SampleFormat::uint16, "Gray", 257, 193, 1, 99202,
          "6ae7190a5d6d9af4daad32c0ef92c700256f05218cb3c92660cac9655b41891f"},
         {"gray-u16-zstd-sh.xisf.b64",
          "8c86271006085159be06906ebe05a26cebfc86cc4bc10a54dd669ca96e2afa13",
          "zstd+sh", mmxisf::SampleFormat::uint16, "Gray", 257, 193, 1, 99202,
          "6ae7190a5d6d9af4daad32c0ef92c700256f05218cb3c92660cac9655b41891f"},
         {"gray-u32-zstd-sh.xisf.b64",
          "66460230fc2001b2f870dfe9b64846ddb54e74912cc1f90734f11128c41fc33b",
          "zstd+sh", mmxisf::SampleFormat::uint32, "Gray", 257, 193, 1, 198404,
          "34e34706460891117d9f761f9d7ce4b7a8e40b469a460ce0aa7711aa6e45bcbd"},
         {"gray-f64-zstd-sh.xisf.b64",
          "a929d29e24c9610b256c392b0176ff80deb17d62ab39d6e23a2321da9ef53a42",
          "zstd+sh", mmxisf::SampleFormat::float64, "Gray", 257, 193, 1, 396808,
          "a996274b34f96bdede2d99c6c1bfa89e7f07a4c6b1029ecb9bae58cd9bad2cd0"},
         {"rgb-u8-zstd-sh.xisf.b64",
          "d560db890bd9ed8b5599ee540cfaa4361a1a0f85cce430186ac71214d3585389",
          "zstd+sh", mmxisf::SampleFormat::uint8, "RGB", 257, 193, 3, 148803,
          "8599597f09b871909284f89662d45f001843b4495cee6404410c7e8b81d518bf"},
         {"rgb-f32-zstd-sh.xisf.b64",
          "f47f4530345ab1e43e320351458f574a39f283a3211ee597ce062410ab0a66d0",
          "zstd+sh", mmxisf::SampleFormat::float32, "RGB", 257, 193, 3, 595212,
          "0132a60d85d32dc98a67944988fdf1276d597e52dd3bedfd0e13fb50eb311475"},
         {"mmxisf-writer-rgb-u16.xisf.b64",
          "951279e808a160c7028405f30cbffaa71ba1266dba5c245dbb9adf0c4294be10",
          "", mmxisf::SampleFormat::uint16, "RGB", 2, 2, 3, 24,
          "9ed139a002ff273082f356718eebc4b085df62d42c8463b35738e143d852c4d4"},
         {"mmxisf-writer-sha3-rgb.xisf.b64",
          "2e318a9d66bd30c16029c1be0608d764a1dca3760d6d0121c951966e6739e7b5",
          "zstd+sh", mmxisf::SampleFormat::uint16, "RGB", 2, 2, 3, 24,
          "adc4289fa7f0c65f72ac49b058d1368e7028ab84cd7c91eb027b4a589d21bbc6",
          "sha3-256:"
          "1a4a14880e0c29187dc19f47cbd5e09c983450f2bc71cc029da03cef7b616f8c"}}};
    for (const auto &fixture : fixtures) {
      check_fixture(root / fixture.name, fixture.source_sha256,
                    fixture.compression, fixture.sample_format,
                    fixture.color_space, fixture.width, fixture.height,
                    fixture.channels, fixture.pixel_bytes, fixture.pixel_sha256,
                    fixture.checksum);
    }
    check_multi_writer_fixture(root / "mmxisf-writer-multi-scalars.xisf.b64");
    check_metadata_writer_fixture(root / "mmxisf-writer-metadata.xisf.b64");
    check_codec_writer_fixture(root / "mmxisf-writer-codecs.xisf.b64");
    check_native_properties_writer_fixture(
        root / "mmxisf-writer-native-properties.xisf.b64");
    std::cout << "PASS: independent producer and consumer interop matrix\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
