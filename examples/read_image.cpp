// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: mmxisf-example-read <file.xisf>\n";
    return EXIT_FAILURE;
  }

  auto opened = mmxisf::Reader::open_file(argv[1]);
  if (!opened) {
    std::cerr << mmxisf::to_string(opened.error().code) << ": "
              << opened.error().message << '\n';
    return EXIT_FAILURE;
  }

  const auto &document = opened.value().document();
  if (document.images().empty()) {
    std::cerr << "The XISF unit contains no image.\n";
    return EXIT_FAILURE;
  }

  const auto &descriptor = document.images().front();
  if (descriptor.geometry.size() < 2) {
    std::cerr << "The first image does not declare width and height.\n";
    return EXIT_FAILURE;
  }
  mmxisf::ImageReadOptions options;
  options.pixel_storage = mmxisf::PixelStorageOutput::planar;
  options.byte_order = mmxisf::ByteOrderOutput::native;
  auto decoded = opened.value().read_image(0, options);
  if (!decoded) {
    std::cerr << mmxisf::to_string(decoded.error().code) << ": "
              << decoded.error().message << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "XISF " << document.version() << '\n'
            << "images: " << document.images().size() << '\n'
            << "first image: " << descriptor.geometry[0] << 'x'
            << descriptor.geometry[1] << ' ' << descriptor.color_space << ' '
            << descriptor.sample_format_name << '\n'
            << "decoded bytes: " << decoded.value().pixels.size() << '\n'
            << "output storage: "
            << mmxisf::to_string(decoded.value().pixel_storage) << '\n'
            << "output byte order: "
            << mmxisf::to_string(decoded.value().byte_order) << '\n';
  return EXIT_SUCCESS;
}
