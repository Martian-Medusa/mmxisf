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
    std::size_t expected_pixel_bytes, std::string_view expected_pixel_sha256) {
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
  auto decoded = opened.value().read_image(0);
  expect(decoded.has_value(), "independent fixture did not decode");
  expect(decoded.value().pixels.size() == expected_pixel_bytes,
         "independent fixture decoded byte count changed");
  expect(sha256_hex(decoded.value().pixels) == expected_pixel_sha256,
         "independent fixture decoded bytes differ from the source array");
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
    };
    const auto fixtures = std::array<Fixture, 10>{
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
          "9ed139a002ff273082f356718eebc4b085df62d42c8463b35738e143d852c4d4"}}};
    for (const auto &fixture : fixtures) {
      check_fixture(root / fixture.name, fixture.source_sha256,
                    fixture.compression, fixture.sample_format,
                    fixture.color_space, fixture.width, fixture.height,
                    fixture.channels, fixture.pixel_bytes,
                    fixture.pixel_sha256);
    }
    std::cout << "PASS: independent producer and consumer interop matrix\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
