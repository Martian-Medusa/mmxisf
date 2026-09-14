// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/writer.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: mmxisf-example-write <new-file.xisf>\n";
    return EXIT_FAILURE;
  }

  // Four UInt16 samples encoded explicitly as little-endian bytes.
  const std::array<std::byte, 8> pixels{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40},
      std::byte{0x00}, std::byte{0x80}, std::byte{0xff}, std::byte{0xff}};

  mmxisf::ImageWriteView image;
  image.id = "example";
  image.width = 2;
  image.height = 2;
  image.channels = 1;
  image.sample_format = mmxisf::SampleFormat::uint16;
  image.color_space = "Gray";
  image.pixel_storage = mmxisf::PixelStorage::planar;
  image.byte_order = mmxisf::ByteOrder::little;
  image.checksum = mmxisf::ChecksumAlgorithm::sha256;
  image.pixels = pixels;

  mmxisf::WriterOptions options;
  options.creation_time = "2026-01-01T00:00:00Z";
  options.creator_application = "mmxisf public API example";

  auto written = mmxisf::Writer::write_file(argv[1], image, options);
  if (!written) {
    std::cerr << mmxisf::to_string(written.error().code) << ": "
              << written.error().message << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "wrote " << written.value().file_size << " bytes\n";
  return EXIT_SUCCESS;
}
