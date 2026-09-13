// SPDX-License-Identifier: Apache-2.0

#include "Preview.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

mmxisf::RawImage rgb_image(mmxisf::PixelStorage storage,
                           std::vector<std::byte> pixels) {
  mmxisf::RawImage image;
  image.width = 2;
  image.height = 1;
  image.channels = 3;
  image.sample_format = mmxisf::SampleFormat::uint8;
  image.pixel_storage = storage;
  image.pixels = std::move(pixels);
  return image;
}

} // namespace

int main() {
  const auto planar = rgb_image(
      mmxisf::PixelStorage::planar,
      {std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
       std::byte{50}, std::byte{60}});
  expect(mmxisf::viewer::sample_value(planar, 0, 0) == 10.0,
         "planar pixel 0 red");
  expect(mmxisf::viewer::sample_value(planar, 0, 1) == 30.0,
         "planar pixel 0 green");
  expect(mmxisf::viewer::sample_value(planar, 1, 2) == 60.0,
         "planar pixel 1 blue");

  const auto normal = rgb_image(
      mmxisf::PixelStorage::normal,
      {std::byte{10}, std::byte{30}, std::byte{50}, std::byte{20},
       std::byte{40}, std::byte{60}});
  expect(mmxisf::viewer::sample_value(normal, 0, 0) == 10.0,
         "normal pixel 0 red");
  expect(mmxisf::viewer::sample_value(normal, 0, 1) == 30.0,
         "normal pixel 0 green");
  expect(mmxisf::viewer::sample_value(normal, 1, 2) == 60.0,
         "normal pixel 1 blue");

  mmxisf::RawImage big_endian;
  big_endian.width = 1;
  big_endian.height = 1;
  big_endian.channels = 1;
  big_endian.sample_format = mmxisf::SampleFormat::uint32;
  big_endian.byte_order = mmxisf::ByteOrder::big;
  big_endian.pixels = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                       std::byte{0x04}};
  expect(mmxisf::viewer::sample_value(big_endian, 0, 0) == 16909060.0,
         "big-endian UInt32 is interpreted exactly");

  mmxisf::RawImage float64;
  float64.width = 1;
  float64.height = 1;
  float64.channels = 1;
  float64.sample_format = mmxisf::SampleFormat::float64;
  float64.byte_order = mmxisf::ByteOrder::little;
  const auto bits = std::bit_cast<std::uint64_t>(0.25);
  for (std::size_t index = 0; index < 8; ++index) {
    float64.pixels.push_back(
        static_cast<std::byte>((bits >> (index * 8U)) & 0xffU));
  }
  expect(std::abs(mmxisf::viewer::sample_value(float64, 0, 0) - 0.25) <
             1e-12,
         "little-endian Float64 is interpreted exactly");

  const auto range = mmxisf::viewer::calculate_stretch_range(planar);
  expect(range.linear_low == 0.0 && range.linear_high == 255.0,
         "UInt8 default representable range is applied for display only");
  expect(range.auto_low == 10.0 && range.auto_high == 60.0,
         "RGB auto stretch samples all nominal channels");

  return failures == 0 ? 0 : 1;
}
