// SPDX-License-Identifier: Apache-2.0

#include "Preview.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace mmxisf::viewer {
namespace {

std::uint64_t unsigned_sample(const std::uint8_t *bytes, std::uint64_t offset,
                              std::size_t size, ByteOrder byte_order) {
  std::uint64_t value = 0;
  if (byte_order == ByteOrder::little) {
    for (std::size_t index = 0; index < size; ++index) {
      value |= static_cast<std::uint64_t>(bytes[offset + index])
               << (index * 8U);
    }
  } else {
    for (std::size_t index = 0; index < size; ++index) {
      value = (value << 8U) | bytes[offset + index];
    }
  }
  return value;
}

} // namespace

double sample_value(const RawImage &image, std::uint64_t pixel_index,
                    std::uint64_t channel) {
  const auto *bytes =
      reinterpret_cast<const std::uint8_t *>(image.pixels.data());
  const std::uint64_t pixel_count = image.width * image.height;
  const std::uint64_t sample_index =
      image.pixel_storage == PixelStorage::planar
          ? channel * pixel_count + pixel_index
          : pixel_index * image.channels + channel;
  switch (image.sample_format) {
  case SampleFormat::uint8:
    return bytes[sample_index];
  case SampleFormat::uint16:
    return static_cast<double>(
        unsigned_sample(bytes, sample_index * 2, 2, image.byte_order));
  case SampleFormat::uint32:
    return static_cast<double>(
        unsigned_sample(bytes, sample_index * 4, 4, image.byte_order));
  case SampleFormat::uint64:
    return static_cast<double>(
        unsigned_sample(bytes, sample_index * 8, 8, image.byte_order));
  case SampleFormat::float32: {
    const auto bits = static_cast<std::uint32_t>(
        unsigned_sample(bytes, sample_index * 4, 4, image.byte_order));
    return static_cast<double>(std::bit_cast<float>(bits));
  }
  case SampleFormat::float64: {
    const auto bits =
        unsigned_sample(bytes, sample_index * 8, 8, image.byte_order);
    return std::bit_cast<double>(bits);
  }
  case SampleFormat::complex32:
  case SampleFormat::complex64:
  case SampleFormat::unsupported:
    return 0.0;
  }
  return 0.0;
}

StretchRange calculate_stretch_range(const RawImage &image) {
  const std::uint64_t count = image.width * image.height;
  constexpr std::uint64_t kMaximumSamples = 300'000;
  const std::uint64_t nominal_channels = image.channels >= 3 ? 3 : 1;
  const auto samples_per_channel = kMaximumSamples / nominal_channels;
  const auto stride =
      std::max<std::uint64_t>(1, count / samples_per_channel);
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(
      std::min(count * nominal_channels, kMaximumSamples + nominal_channels)));
  for (std::uint64_t index = 0; index < count; index += stride) {
    for (std::uint64_t channel = 0; channel < nominal_channels; ++channel) {
      const auto value = sample_value(image, index, channel);
      if (std::isfinite(value)) {
        samples.push_back(value);
      }
    }
  }
  StretchRange range;
  if (image.lower_bound && image.upper_bound) {
    range.linear_low = *image.lower_bound;
    range.linear_high = *image.upper_bound;
  } else if (image.sample_format == SampleFormat::uint8) {
    range.linear_high = 255.0;
  } else if (image.sample_format == SampleFormat::uint16) {
    range.linear_high = 65535.0;
  } else if (image.sample_format == SampleFormat::uint32) {
    range.linear_high = 4294967295.0;
  } else if (image.sample_format == SampleFormat::uint64) {
    range.linear_high =
        static_cast<double>(std::numeric_limits<std::uint64_t>::max());
  }
  if (samples.empty()) {
    return range;
  }
  std::sort(samples.begin(), samples.end());
  const auto at = [&](double percentile) {
    const auto index = std::min(
        samples.size() - 1,
        static_cast<std::size_t>(percentile *
                                 static_cast<double>(samples.size())));
    return samples[index];
  };
  range.auto_low = at(0.005);
  range.auto_high = at(0.999);
  if (!(range.auto_high > range.auto_low)) {
    range.auto_low = samples.front();
    range.auto_high = samples.back();
  }
  if (!(range.auto_high > range.auto_low)) {
    range.auto_high = range.auto_low + 1.0;
  }
  return range;
}

} // namespace mmxisf::viewer
