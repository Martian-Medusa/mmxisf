// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"
#include "mmxisf/writer.hpp"

#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
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
    if (std::getenv("MMXISF_KEEP_TEST_OUTPUTS") != nullptr) {
      return;
    }
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

void test_multi_image_scalar_round_trip() {
  const std::array<std::byte, 4> uint8_pixels{std::byte{0x00}, std::byte{0x7f},
                                              std::byte{0x80}, std::byte{0xff}};
  const std::array<std::byte, 8> uint32_pixels{
      std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0xef}, std::byte{0xcd}, std::byte{0xab}, std::byte{0x89}};
  const std::array<std::byte, 12> float32_pixels{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3f},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0x3f},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x80}, std::byte{0xbf}};
  const std::array<std::byte, 8> float64_pixels{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0xe0}, std::byte{0x3f}};

  std::vector<mmxisf::ImageWriteView> images(4);
  images[0] = {.id = "u8",
               .width = 2,
               .height = 2,
               .channels = 1,
               .sample_format = mmxisf::SampleFormat::uint8,
               .color_space = "Gray",
               .pixels = uint8_pixels};
  images[1] = {.id = "u32",
               .width = 2,
               .height = 1,
               .channels = 1,
               .sample_format = mmxisf::SampleFormat::uint32,
               .color_space = "Gray",
               .pixels = uint32_pixels};
  images[2] = {.id = "f32-rgb",
               .width = 1,
               .height = 1,
               .channels = 3,
               .sample_format = mmxisf::SampleFormat::float32,
               .color_space = "RGB",
               .lower_bound = -1.0,
               .upper_bound = 1.0,
               .pixels = float32_pixels};
  images[3] = {.id = "f64",
               .width = 1,
               .height = 1,
               .channels = 1,
               .sample_format = mmxisf::SampleFormat::float64,
               .color_space = "Gray",
               .lower_bound = 0.0,
               .upper_bound = 1.0,
               .pixels = float64_pixels};

  const auto path = output_path("mmxisf-writer-multi-scalars.xisf");
  auto written = mmxisf::Writer::write_file(
      path, std::span<const mmxisf::ImageWriteView>(images), options());
  expect(written.has_value(), "multi-image scalar writer failed");
  expect(written.value().image_blocks.size() == images.size() &&
             written.value().image_block.offset ==
                 written.value().image_blocks.front().offset,
         "multi-image writer summary changed");
  for (std::size_t index = 0; index < images.size(); ++index) {
    const auto &block = written.value().image_blocks[index];
    expect(block.kind == mmxisf::BlockKind::attachment &&
               block.offset % 4096 == 0 &&
               block.size == images[index].pixels.size(),
           "multi-image writer block layout changed");
    if (index != 0) {
      const auto &previous = written.value().image_blocks[index - 1];
      expect(block.offset >= previous.offset + previous.size,
             "multi-image writer blocks overlap");
    }
  }
  expect(written.value().file_size == std::filesystem::file_size(path),
         "multi-image writer file-size summary changed");
  const auto serialized = read_file(path);
  expect(sha256(serialized) ==
             "c72c577d090e49d4966b1dda8d20e9948a9d23e5ba1fae3688c3ae34ddeb42ba",
         "multi-image writer deterministic external-oracle anchor changed");

  auto opened = mmxisf::Reader::open_file(path);
  expect(opened.has_value() &&
             opened.value().document().images().size() == images.size(),
         "multi-image writer result did not reopen");
  const std::array expected_formats{
      mmxisf::SampleFormat::uint8, mmxisf::SampleFormat::uint32,
      mmxisf::SampleFormat::float32, mmxisf::SampleFormat::float64};
  for (std::size_t index = 0; index < images.size(); ++index) {
    const auto &descriptor = opened.value().document().images()[index];
    expect(descriptor.id == images[index].id &&
               descriptor.sample_format == expected_formats[index] &&
               descriptor.block.offset ==
                   written.value().image_blocks[index].offset,
           "multi-image writer descriptor changed");
    if (index >= 2) {
      expect(descriptor.lower_bound == images[index].lower_bound &&
                 descriptor.upper_bound == images[index].upper_bound,
             "floating-point writer bounds changed");
    }
    auto decoded = opened.value().read_image(index);
    expect(decoded.has_value() &&
               decoded.value().pixels ==
                   std::vector<std::byte>(images[index].pixels.begin(),
                                          images[index].pixels.end()),
           "multi-image scalar pixels did not round trip exactly");
  }
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

  image.sample_format = mmxisf::SampleFormat::uint64;
  auto unsupported = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-unsupported.xisf"), image, options());
  expect(!unsupported &&
             unsupported.error().code == mmxisf::ErrorCode::unsupported_feature,
         "unsupported writer profile was not explicit");

  std::array<std::byte, 16> float_pixels{};
  image = gray_image(float_pixels);
  image.sample_format = mmxisf::SampleFormat::float32;
  auto missing_bounds = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-missing-bounds.xisf"), image, options());
  expect(!missing_bounds &&
             missing_bounds.error().code == mmxisf::ErrorCode::invalid_argument,
         "floating-point writer image without bounds was accepted");
  image.lower_bound = 0.0;
  image.upper_bound = 0.0;
  auto invalid_bounds = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-invalid-bounds.xisf"), image, options());
  expect(!invalid_bounds &&
             invalid_bounds.error().code == mmxisf::ErrorCode::invalid_argument,
         "non-increasing writer bounds were accepted");
  image.upper_bound = std::numeric_limits<double>::infinity();
  auto nonfinite_bounds = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-nonfinite-bounds.xisf"), image, options());
  expect(!nonfinite_bounds && nonfinite_bounds.error().code ==
                                  mmxisf::ErrorCode::invalid_argument,
         "non-finite writer bounds were accepted");
  image.upper_bound.reset();
  auto half_bounds = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-half-bounds.xisf"), image, options());
  expect(!half_bounds &&
             half_bounds.error().code == mmxisf::ErrorCode::invalid_argument,
         "incomplete writer bounds were accepted");

  image = gray_image(pixels);
  std::array<mmxisf::ImageWriteView, 2> two_images{image, image};
  auto count_options = options();
  count_options.max_images = 1;
  auto image_count = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-image-count.xisf"), two_images, count_options);
  expect(!image_count &&
             image_count.error().code == mmxisf::ErrorCode::resource_limit,
         "writer image-count budget was not enforced");
  auto cumulative_options = options();
  cumulative_options.max_cumulative_image_bytes = pixels.size();
  auto cumulative = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-cumulative-limit.xisf"), two_images,
      cumulative_options);
  expect(!cumulative &&
             cumulative.error().code == mmxisf::ErrorCode::resource_limit,
         "writer cumulative image-byte budget was not enforced");
  std::span<const mmxisf::ImageWriteView> no_images;
  auto empty = mmxisf::Writer::write_file(
      output_path("mmxisf-writer-empty.xisf"), no_images, options());
  expect(!empty && empty.error().code == mmxisf::ErrorCode::invalid_argument,
         "writer accepted an empty image sequence");
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
    test_multi_image_scalar_round_trip();
    test_rejection_and_cleanup();
    std::cout << "PASS: deterministic multi-image scalar writer\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
}
