// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <bit>
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

void append_u16_le(std::vector<std::byte> &output, std::uint16_t value) {
  output.push_back(static_cast<std::byte>(value & 0xffU));
  output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_f32_le(std::vector<std::byte> &output, float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  output.push_back(static_cast<std::byte>(bits & 0xffU));
  output.push_back(static_cast<std::byte>((bits >> 8U) & 0xffU));
  output.push_back(static_cast<std::byte>((bits >> 16U) & 0xffU));
  output.push_back(static_cast<std::byte>((bits >> 24U) & 0xffU));
}

std::vector<std::byte> expected_gray() {
  std::vector<std::byte> result;
  result.reserve(257U * 193U * 2U);
  for (std::uint32_t y = 0; y < 193; ++y) {
    for (std::uint32_t x = 0; x < 257; ++x) {
      const auto value =
          static_cast<std::uint16_t>(((x % 17U) + 17U * (y % 13U)) * 257U);
      append_u16_le(result, value);
    }
  }
  return result;
}

std::vector<std::byte> expected_rgb() {
  std::vector<std::byte> result;
  result.reserve(257U * 193U * 3U * 4U);
  for (std::uint32_t channel = 0; channel < 3; ++channel) {
    for (std::uint32_t y = 0; y < 193; ++y) {
      for (std::uint32_t x = 0; x < 257; ++x) {
        double source = 0;
        if (channel == 0) {
          source = static_cast<double>(x % 11U) / 10.0;
        } else if (channel == 1) {
          source = static_cast<double>(y % 7U) / 6.0;
        } else {
          source = static_cast<double>((x + y) % 9U) / 8.0;
        }
        append_f32_le(result, static_cast<float>(source));
      }
    }
  }
  return result;
}

void check_fixture(const std::filesystem::path &path,
                   std::string_view expected_source_sha256,
                   std::string_view expected_compression,
                   mmxisf::SampleFormat expected_format,
                   std::string_view expected_color,
                   const std::vector<std::byte> &expected_pixels) {
  auto bytes = load_base64(path);
  expect(sha256_hex(bytes) == expected_source_sha256,
         "independent-producer fixture identity changed");
  auto opened = mmxisf::Reader::open_source(
      std::make_shared<MemorySource>(std::move(bytes)));
  expect(opened.has_value(), "independent-producer fixture did not open");
  const auto &images = opened.value().document().images();
  expect(images.size() == 1, "independent fixture image count changed");
  const auto &descriptor = images[0];
  expect(descriptor.geometry ==
             std::vector<std::uint64_t>{257, 193,
                                        expected_color == "RGB" ? 3U : 1U},
         "independent fixture geometry changed");
  expect(descriptor.sample_format == expected_format,
         "independent fixture sample format changed");
  expect(descriptor.color_space == expected_color,
         "independent fixture color space changed");
  expect(descriptor.pixel_storage == mmxisf::PixelStorage::planar,
         "independent fixture pixel storage changed");
  expect(descriptor.byte_order == mmxisf::ByteOrder::little,
         "independent fixture byte order changed");
  expect(descriptor.compression.starts_with(std::string(expected_compression) +
                                            ":"),
         "independent fixture codec changed");
  auto decoded = opened.value().read_image(0);
  expect(decoded.has_value(), "independent fixture did not decode");
  expect(decoded.value().pixels == expected_pixels,
         "independent fixture decoded bytes differ from the source array");
}

} // namespace

int main() {
  try {
    const auto root =
        std::filesystem::path(MMXISF_TEST_SOURCE_DIR) / "tests" / "interop";
    const auto gray = expected_gray();
    struct GrayFixture {
      std::string_view name;
      std::string_view sha256;
      std::string_view compression;
    };
    for (const auto &fixture :
         std::array<GrayFixture, 4>{{{"gray-u16-zlib-sh.xisf.b64",
                                      "7a5e97af8cb10c57ba787892df9c8050a080a3e4"
                                      "332612937c3f86343501e8f3",
                                      "zlib+sh"},
                                     {"gray-u16-lz4-sh.xisf.b64",
                                      "0f1e521e612a9574c1761af9fe41b6acc825c837"
                                      "aa6c1162cc8e89e918f344ee",
                                      "lz4+sh"},
                                     {"gray-u16-lz4hc-sh.xisf.b64",
                                      "f33b9cc8f397ef4d8b333b9754d72bfa88795052"
                                      "335a23daafc661b113873131",
                                      "lz4hc+sh"},
                                     {"gray-u16-zstd-sh.xisf.b64",
                                      "8c86271006085159be06906ebe05a26cebfc86cc"
                                      "4bc10a54dd669ca96e2afa13",
                                      "zstd+sh"}}}) {
      check_fixture(root / fixture.name, fixture.sha256, fixture.compression,
                    mmxisf::SampleFormat::uint16, "Gray", gray);
    }
    check_fixture(
        root / "rgb-f32-zstd-sh.xisf.b64",
        "f47f4530345ab1e43e320351458f574a39f283a3211ee597ce062410ab0a66d0",
        "zstd+sh", mmxisf::SampleFormat::float32, "RGB", expected_rgb());
    std::cout << "PASS: independent producer pixel and codec matrix\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
