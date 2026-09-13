// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: mmxisf-inspect <file.xisf>\n";
    return EXIT_FAILURE;
  }
  auto result = mmxisf::Reader::open_file(argv[1]);
  if (!result) {
    std::cerr << mmxisf::to_string(result.error().code) << ": "
              << result.error().message << '\n';
    return EXIT_FAILURE;
  }
  const auto &document = result.value().document();
  std::cout << "XISF " << document.version() << "\n"
            << "file-bytes: " << document.file_size() << "\n"
            << "header-bytes: " << document.header_length() << "\n"
            << "images: " << document.images().size() << "\n"
            << "metadata: " << document.metadata().size() << "\n";
  for (std::size_t index = 0; index < document.images().size(); ++index) {
    const auto &image = document.images()[index];
    std::cout << "image[" << index << "]: id=\"" << image.id << "\" geometry=";
    for (std::size_t axis = 0; axis < image.geometry.size(); ++axis) {
      if (axis != 0) {
        std::cout << ':';
      }
      std::cout << image.geometry[axis];
    }
    std::cout << " sample=" << image.sample_format_name
              << " color=" << image.color_space
              << " storage=" << mmxisf::to_string(image.pixel_storage)
              << " block=" << mmxisf::to_string(image.block.kind);
    if (image.lower_bound && image.upper_bound) {
      std::cout << " bounds=" << *image.lower_bound << ':'
                << *image.upper_bound;
    }
    std::cout << '\n';
  }
  for (const auto &entry : document.metadata()) {
    std::cout << (entry.image_index
                      ? "image[" + std::to_string(*entry.image_index) + "]"
                      : "document")
              << '\t'
              << (entry.kind == mmxisf::MetadataEntry::Kind::property
                      ? "Property"
                      : "FITSKeyword")
              << '\t' << entry.name << '\t' << entry.type << '\t'
              << entry.value;
    if (!entry.comment.empty()) {
      std::cout << '\t' << entry.comment;
    }
    std::cout << '\n';
  }
  return EXIT_SUCCESS;
}
