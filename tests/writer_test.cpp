// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"
#include "mmxisf/writer.hpp"

#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::filesystem::path> cleanup_paths;

class Cleanup {
public:
  ~Cleanup() {
    for (const auto &path : cleanup_paths) {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
  }
};

void expect(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::filesystem::path output_path(std::string_view name) {
  auto path = std::filesystem::temp_directory_path() / name;
  cleanup_paths.push_back(path);
  auto temporary = path;
  temporary += ".mmxisf-tmp";
  cleanup_paths.push_back(temporary);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  std::filesystem::remove(temporary, ignored);
  return path;
}

std::vector<char> read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  expect(static_cast<bool>(input), "cannot read writer result");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string sha256(std::span<const char> bytes) {
  std::array<unsigned char, 32> digest{};
  unsigned int digest_size = 0;
  expect(EVP_Digest(bytes.data(), bytes.size(), digest.data(), &digest_size,
                    EVP_sha256(), nullptr) == 1 &&
             digest_size == digest.size(),
         "cannot hash writer result");
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    output << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return output.str();
}

mmxisf::WriterOptions options() {
  mmxisf::WriterOptions result;
  result.creation_time = "2026-09-14T00:00:00Z";
  result.creator_application = "Martian Medusa & mmxisf writer test";
  return result;
}

mmxisf::ImageWriteView gray_image(std::span<const std::byte> pixels) {
  mmxisf::ImageWriteView image;
  image.id = "gray&science";
  image.width = 2;
  image.height = 2;
  image.channels = 1;
  image.sample_format = mmxisf::SampleFormat::uint16;
  image.color_space = "Gray";
  image.pixel_storage = mmxisf::PixelStorage::planar;
  image.byte_order = mmxisf::ByteOrder::little;
  image.pixels = pixels;
  return image;
}

void test_deterministic_gray_round_trip() {
  const std::array<std::byte, 8> pixels{
      std::byte{0x01}, std::byte{0x00}, std::byte{0x02}, std::byte{0x01},
      std::byte{0x00}, std::byte{0x80}, std::byte{0xff}, std::byte{0xff}};
  const auto first_path = output_path("mmxisf-writer-gray-a.xisf");
  const auto second_path = output_path("mmxisf-writer-gray-b.xisf");
  const auto image = gray_image(pixels);
  const auto first = mmxisf::Writer::write_file(first_path, image, options());
  const auto second = mmxisf::Writer::write_file(second_path, image, options());
  expect(first.has_value() && second.has_value(), "gray writer failed");
  expect(first.value().file_size == std::filesystem::file_size(first_path) &&
             first.value().header_length == second.value().header_length &&
             first.value().image_block.offset % 4096 == 0,
         "writer layout summary changed");
  expect(read_file(first_path) == read_file(second_path),
         "equivalent writer inputs are not byte deterministic");

  auto opened = mmxisf::Reader::open_file(first_path);
  expect(opened.has_value(), "writer result did not reopen");
  const auto &document = opened.value().document();
  expect(document.images().size() == 1 && document.metadata().size() == 2,
         "writer document shape changed");
  const auto &descriptor = document.images()[0];
  expect(descriptor.id == "gray&science" &&
             descriptor.geometry == std::vector<std::uint64_t>{2, 2, 1} &&
             descriptor.sample_format == mmxisf::SampleFormat::uint16 &&
             descriptor.color_space == "Gray" &&
             descriptor.pixel_storage == mmxisf::PixelStorage::planar &&
             descriptor.byte_order == mmxisf::ByteOrder::little,
         "writer Gray descriptor changed");
  auto decoded = opened.value().read_image(0);
  expect(decoded.has_value() &&
             decoded.value().pixels ==
                 std::vector<std::byte>(pixels.begin(), pixels.end()),
         "writer Gray pixels did not round trip exactly");
  expect(document.metadata()[0].name == "XISF:CreationTime" &&
             document.metadata()[0].value == "2026-09-14T00:00:00Z" &&
             document.metadata()[1].name == "XISF:CreatorApplication" &&
             document.metadata()[1].value ==
                 "Martian Medusa & mmxisf writer test",
         "writer required metadata did not round trip exactly");
}

void test_rgb_little_endian_round_trip() {
  const std::array<std::byte, 24> pixels{
      std::byte{0x01}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00},
      std::byte{0x03}, std::byte{0x00}, std::byte{0x04}, std::byte{0x00},
      std::byte{0x11}, std::byte{0x00}, std::byte{0x12}, std::byte{0x00},
      std::byte{0x13}, std::byte{0x00}, std::byte{0x14}, std::byte{0x00},
      std::byte{0x21}, std::byte{0x00}, std::byte{0x22}, std::byte{0x00},
      std::byte{0x23}, std::byte{0x00}, std::byte{0x24}, std::byte{0x00}};
  mmxisf::ImageWriteView image;
  image.id = "rgb";
  image.width = 2;
  image.height = 2;
  image.channels = 3;
  image.sample_format = mmxisf::SampleFormat::uint16;
  image.color_space = "RGB";
  image.pixel_storage = mmxisf::PixelStorage::planar;
  image.byte_order = mmxisf::ByteOrder::little;
  image.pixels = pixels;
  const auto path = output_path("mmxisf-writer-rgb.xisf");
  auto written = mmxisf::Writer::write_file(path, image, options());
  expect(written.has_value(), "RGB writer failed");
  const auto serialized = read_file(path);
  expect(sha256(serialized) ==
             "951279e808a160c7028405f30cbffaa71ba1266dba5c245dbb9adf0c4294be10",
         "writer deterministic external-oracle anchor changed");
  auto opened = mmxisf::Reader::open_file(path);
  expect(opened.has_value(), "RGB writer result did not reopen");
  const auto &descriptor = opened.value().document().images()[0];
  expect(descriptor.geometry == std::vector<std::uint64_t>{2, 2, 3} &&
             descriptor.color_space == "RGB" &&
             descriptor.byte_order == mmxisf::ByteOrder::little,
         "writer RGB descriptor changed");
  auto decoded = opened.value().read_image(0);
  expect(decoded.has_value() &&
             decoded.value().pixels ==
                 std::vector<std::byte>(pixels.begin(), pixels.end()),
         "writer RGB source bytes did not round trip exactly");
}

void test_rejection_and_cleanup() {
  const std::array<std::byte, 8> pixels{};
  auto image = gray_image(pixels);
  auto invalid_options = options();
  invalid_options.creation_time = "2026-02-30T00:00:00Z";
  auto invalid_time = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-invalid-time.xisf"), image, invalid_options);
  expect(!invalid_time &&
             invalid_time.error().code == mmxisf::ErrorCode::invalid_argument,
         "invalid writer creation time was accepted");

  image.sample_format = mmxisf::SampleFormat::float32;
  auto unsupported = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-unsupported.xisf"), image, options());
  expect(!unsupported &&
             unsupported.error().code == mmxisf::ErrorCode::unsupported_feature,
         "unsupported writer profile was not explicit");
  image = gray_image(pixels);
  image.pixels = image.pixels.first(6);
  auto wrong_size = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-size.xisf"), image, options());
  expect(!wrong_size &&
             wrong_size.error().code == mmxisf::ErrorCode::invalid_argument,
         "writer pixel-size mismatch was accepted");

  image = gray_image(pixels);
  auto alignment_options = options();
  alignment_options.attachment_alignment = 1000;
  auto bad_alignment = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-alignment.xisf"), image, alignment_options);
  expect(!bad_alignment &&
             bad_alignment.error().code == mmxisf::ErrorCode::invalid_argument,
         "invalid writer alignment was accepted");

  image = gray_image(pixels);
  image.byte_order = mmxisf::ByteOrder::big;
  auto big_endian = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-big-endian.xisf"), image, options());
  expect(!big_endian &&
             big_endian.error().code == mmxisf::ErrorCode::unsupported_feature,
         "unsupported writer byte order was not explicit");

  image = gray_image(pixels);
  image.id = std::string("bad") + static_cast<char>(0xff);
  auto invalid_utf8 = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-utf8.xisf"), image, options());
  expect(!invalid_utf8 &&
             invalid_utf8.error().code == mmxisf::ErrorCode::invalid_argument,
         "invalid writer UTF-8 was accepted");

  image = gray_image(pixels);
  auto small_header_options = options();
  small_header_options.max_header_bytes = 32;
  auto header_limit =
      mmxisf::Writer::write_file(output_path("mmxisf-writer-header-limit.xisf"),
                                 image, small_header_options);
  expect(!header_limit &&
             header_limit.error().code == mmxisf::ErrorCode::resource_limit,
         "writer header budget was not enforced");

  const auto stale_path = output_path("mmxisf-writer-stale.xisf");
  auto stale_temporary = stale_path;
  stale_temporary += ".mmxisf-tmp";
  {
    std::ofstream stale(stale_temporary, std::ios::binary);
    stale << "keep";
  }
  auto stale = mmxisf::Writer::write_file(stale_path, image, options());
  expect(!stale && stale.error().code == mmxisf::ErrorCode::io_error &&
             read_file(stale_temporary) ==
                 std::vector<char>{'k', 'e', 'e', 'p'},
         "writer overwrote a stale temporary path");

  const auto existing_path = output_path("mmxisf-writer-existing.xisf");
  {
    std::ofstream existing(existing_path, std::ios::binary);
    existing << "keep";
  }
  auto existing = mmxisf::Writer::write_file(existing_path, image, options());
  expect(!existing && existing.error().code == mmxisf::ErrorCode::io_error &&
             read_file(existing_path) == std::vector<char>{'k', 'e', 'e', 'p'},
         "writer overwrote an existing destination");

  const auto cancelled_path = output_path("mmxisf-writer-cancelled.xisf");
  std::stop_source cancellation;
  cancellation.request_stop();
  auto cancelled = mmxisf::Writer::write_file(cancelled_path, image, options(),
                                              cancellation.get_token());
  expect(!cancelled && cancelled.error().code == mmxisf::ErrorCode::cancelled &&
             !std::filesystem::exists(cancelled_path),
         "cancelled writer left a destination file");
}

} // namespace

int main() {
  Cleanup cleanup;
  try {
    test_deterministic_gray_round_trip();
    test_rgb_little_endian_round_trip();
    test_rejection_and_cleanup();
    std::cout << "PASS: deterministic monolithic writer foundation\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
