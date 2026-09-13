// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  const bool decode = argc == 3 && std::string(argv[1]) == "--decode";
  if ((!decode && argc != 2) || (argc == 3 && !decode)) {
    std::cerr << "Usage: mmxisf-inspect [--decode] <file.xisf>\n";
    return EXIT_FAILURE;
  }
  auto result = mmxisf::Reader::open_file(argv[decode ? 2 : 1]);
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
            << "metadata: " << document.metadata().size() << "\n"
            << "metadata-bindings: " << document.metadata_bindings().size()
            << "\n";
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
              << " channels="
              << mmxisf::to_string(image.nominal_channel_order)
              << " origin=" << mmxisf::to_string(image.pixel_origin)
              << " traversal=" << mmxisf::to_string(image.pixel_traversal)
              << " storage=" << mmxisf::to_string(image.pixel_storage)
              << " block=" << mmxisf::to_string(image.block.kind);
    if (image.orientation) {
      std::cout << " orientation=" << mmxisf::to_string(*image.orientation);
    } else {
      std::cout << " orientation=absent";
    }
    if (image.lower_bound && image.upper_bound) {
      std::cout << " bounds=" << *image.lower_bound << ':'
                << *image.upper_bound;
    }
    std::cout << '\n';
    if (decode) {
      auto pixels = result.value().read_image(index);
      if (!pixels) {
        std::cerr << "image[" << index
                  << "] decode: " << mmxisf::to_string(pixels.error().code)
                  << ": " << pixels.error().message << '\n';
        return EXIT_FAILURE;
      }
      std::cout << "image[" << index
                << "] decoded-bytes: " << pixels.value().pixels.size()
                << " checksum: "
                << mmxisf::to_string(pixels.value().checksum_verification)
                << '\n';
    }
  }
  for (const auto &entry : document.metadata()) {
    std::cout << mmxisf::to_string(entry.scope);
    if (entry.image_index) {
      std::cout << '[' << *entry.image_index << ']';
    }
    std::cout << '\t'
              << (entry.kind == mmxisf::MetadataEntry::Kind::property
                      ? "Property"
                      : "FITSKeyword")
              << '\t' << entry.name << '\t' << entry.type << '\t'
              << mmxisf::to_string(entry.value_form) << '\t' << entry.value;
    if (!entry.comment.empty()) {
      std::cout << "\tcomment=" << entry.comment;
    }
    if (!entry.format.empty()) {
      std::cout << "\tformat=" << entry.format;
    }
    if (!entry.uid.empty()) {
      std::cout << "\tuid=" << entry.uid;
    }
    if (entry.block.kind != mmxisf::BlockKind::unknown) {
      std::cout << "\tlocation=" << entry.block.raw;
    }
    if (entry.length) {
      std::cout << "\tlength=" << *entry.length;
    }
    if (entry.rows) {
      std::cout << "\trows=" << *entry.rows;
    }
    if (entry.columns) {
      std::cout << "\tcolumns=" << *entry.columns;
    }
    std::cout << '\n';
  }
  for (const auto &binding : document.metadata_bindings()) {
    std::cout << "binding\tmetadata[" << binding.metadata_index << "]\t"
              << (binding.scope == mmxisf::MetadataBinding::Scope::image
                      ? "Image"
                      : "XISF unit");
    if (binding.image_index) {
      std::cout << '[' << *binding.image_index << ']';
    }
    std::cout << '\t' << (binding.by_reference ? "Reference" : "Direct")
              << '\n';
  }
  return EXIT_SUCCESS;
}
