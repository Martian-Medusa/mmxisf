// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/writer.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t kWidth = 6064;
constexpr std::uint64_t kHeight = 4040;
constexpr std::uint64_t kChannels = 3;

void print_usage(std::string_view program) {
  std::cerr << "Usage: " << program << " OUTPUT.xisf\n";
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    print_usage(argv[0]);
    return 2;
  }

  try {
    const auto plane_size = kWidth * kHeight;
    std::vector<std::byte> pixels(plane_size * kChannels);
    std::uint64_t random_state = 0x6d6d786973663031ULL;
    for (std::uint64_t channel = 0; channel < kChannels; ++channel) {
      for (std::uint64_t y = 0; y < kHeight; ++y) {
        for (std::uint64_t x = 0; x < kWidth; ++x) {
          random_state ^= random_state << 13U;
          random_state ^= random_state >> 7U;
          random_state ^= random_state << 17U;
          const auto noise = static_cast<unsigned char>(random_state >> 59U);
          const auto gradient = static_cast<unsigned char>(
              (x / 128U + y / 128U + channel * 7U) & 0x0fU);
          const auto value = static_cast<unsigned char>(
              static_cast<unsigned int>(noise) + gradient);
          pixels[channel * plane_size + y * kWidth + x] =
              static_cast<std::byte>(value);
        }
      }
    }

    mmxisf::ImageWriteView image;
    image.id = "writer-benchmark-rgb";
    image.width = kWidth;
    image.height = kHeight;
    image.channels = kChannels;
    image.sample_format = mmxisf::SampleFormat::uint8;
    image.color_space = "RGB";
    image.compression = mmxisf::CompressionCodec::zstd;
    image.byte_shuffle = true;
    image.checksum = mmxisf::ChecksumAlgorithm::sha256;
    image.pixels = pixels;

    mmxisf::WriterOptions options;
    options.creation_time = "2026-09-14T00:00:00Z";
    options.creator_application = "mmxisf writer benchmark 0.1.0";

    const auto start = std::chrono::steady_clock::now();
    auto result = mmxisf::Writer::write_file(std::filesystem::path(argv[1]),
                                             image, options);
    const auto stop = std::chrono::steady_clock::now();
    if (!result) {
      std::cerr << "writer error: " << result.error().message << '\n';
      return 1;
    }

    const auto elapsed = std::chrono::duration<double>(stop - start).count();
    const auto mebibytes =
        static_cast<double>(pixels.size()) / static_cast<double>(1024U * 1024U);
    std::cout << std::fixed << std::setprecision(3)
              << "decoded_bytes=" << pixels.size() << '\n'
              << "serialized_bytes=" << result.value().image_block.size << '\n'
              << "file_bytes=" << result.value().file_size << '\n'
              << "elapsed_seconds=" << elapsed << '\n'
              << "throughput_mib_per_second=" << mebibytes / elapsed << '\n';
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "benchmark error: " << exception.what() << '\n';
    return 1;
  }
}
