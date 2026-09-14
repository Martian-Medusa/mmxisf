// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <openssl/evp.h>
#include <span>
#include <sstream>
#include <string>

namespace {

std::string sha256(std::span<const std::byte> bytes) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0;
  if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &digest_size,
                 EVP_sha256(), nullptr) != 1 ||
      digest_size != 32) {
    return {};
  }
  std::ostringstream encoded;
  encoded << std::hex << std::setfill('0');
  for (unsigned int index = 0; index < digest_size; ++index) {
    encoded << std::setw(2) << static_cast<unsigned int>(digest[index]);
  }
  return encoded.str();
}

} // namespace

int main(int argc, char **argv) {
  const bool decode_properties =
      argc == 3 && std::string(argv[1]) == "--decode-properties-sha256";
  const bool decode_icc =
      argc == 3 && std::string(argv[1]) == "--decode-icc-sha256";
  const bool decode = argc == 3 && (std::string(argv[1]) == "--decode" ||
                                    std::string(argv[1]) == "--decode-sha256");
  const bool decode_sha256 =
      decode && std::string(argv[1]) == "--decode-sha256";
  const bool has_option = decode || decode_properties || decode_icc;
  if ((!has_option && argc != 2) || (argc == 3 && !has_option)) {
    std::cerr << "Usage: mmxisf-inspect "
                 "[--decode|--decode-sha256|--decode-properties-sha256|"
                 "--decode-icc-sha256] "
                 "<file.xisf>\n";
    return EXIT_FAILURE;
  }
  auto result = mmxisf::Reader::open_file(argv[has_option ? 2 : 1]);
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
            << "\n"
            << "ancillary-objects: " << document.ancillary_objects().size()
            << "\n"
            << "ancillary-bindings: " << document.ancillary_bindings().size()
            << "\n"
            << "icc-profiles: " << document.icc_profiles().size() << "\n"
            << "icc-profile-bindings: "
            << document.icc_profile_bindings().size() << "\n"
            << "extensions: " << document.extension_elements().size() << "\n";
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
              << " channels=" << mmxisf::to_string(image.nominal_channel_order)
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
                << mmxisf::to_string(pixels.value().checksum_verification);
      if (decode_sha256) {
        const auto digest = sha256(pixels.value().pixels);
        if (digest.empty()) {
          std::cerr << "image[" << index
                    << "] decode: unable to compute pixel SHA-256\n";
          return EXIT_FAILURE;
        }
        std::cout << " pixel-sha256: " << digest;
      }
      std::cout << '\n';
    }
  }
  for (std::size_t metadata_index = 0;
       metadata_index < document.metadata().size(); ++metadata_index) {
    const auto &entry = document.metadata()[metadata_index];
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
    if (decode_properties &&
        entry.kind == mmxisf::MetadataEntry::Kind::property &&
        entry.value_form == mmxisf::MetadataEntry::ValueForm::data_block) {
      auto block = result.value().read_property_block(metadata_index);
      if (!block) {
        std::cerr << "metadata[" << metadata_index
                  << "] decode: " << mmxisf::to_string(block.error().code)
                  << ": " << block.error().message << '\n';
        return EXIT_FAILURE;
      }
      const auto digest = sha256(block.value().bytes);
      if (digest.empty()) {
        std::cerr << "metadata[" << metadata_index
                  << "] decode: unable to compute Property SHA-256\n";
        return EXIT_FAILURE;
      }
      std::cout << "metadata[" << metadata_index
                << "] decoded-bytes: " << block.value().bytes.size()
                << " checksum: "
                << mmxisf::to_string(block.value().checksum_verification)
                << " property-sha256: " << digest << '\n';
    }
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
  for (std::size_t index = 0; index < document.ancillary_objects().size();
       ++index) {
    const auto &object = document.ancillary_objects()[index];
    std::cout << "ancillary[" << index << "]\t"
              << mmxisf::to_string(object.kind);
    if (!object.uid.empty()) {
      std::cout << "\tuid=" << object.uid;
    }
    if (object.image_index) {
      std::cout << "\tdirect-image[" << *object.image_index << ']';
    }
    std::cout << '\n';
    for (const auto &attribute : object.attributes) {
      std::cout << "ancillary[" << index << "].attribute\t{"
                << attribute.namespace_uri << '}' << attribute.name << '='
                << std::quoted(attribute.value) << '\n';
    }
  }
  for (const auto &binding : document.ancillary_bindings()) {
    std::cout << "ancillary-binding\tobject[" << binding.object_index
              << "]\timage[" << binding.image_index << "]\t"
              << (binding.by_reference ? "Reference" : "Direct") << '\n';
  }
  for (std::size_t index = 0; index < document.icc_profiles().size(); ++index) {
    const auto &profile = document.icc_profiles()[index];
    std::cout << "icc-profile[" << index << "]\tlocation=" << profile.block.raw;
    if (!profile.uid.empty()) {
      std::cout << "\tuid=" << profile.uid;
    }
    if (profile.image_index) {
      std::cout << "\tdirect-image[" << *profile.image_index << ']';
    }
    if (!profile.compression.empty()) {
      std::cout << "\tcompression=" << profile.compression;
    }
    if (!profile.subblocks.empty()) {
      std::cout << "\tsubblocks=" << profile.subblocks;
    }
    if (!profile.checksum.empty()) {
      std::cout << "\tchecksum=" << profile.checksum;
    }
    std::cout << '\n';
    if (decode_icc) {
      auto decoded = result.value().read_icc_profile(index);
      if (!decoded) {
        std::cerr << "icc-profile[" << index
                  << "] decode: " << mmxisf::to_string(decoded.error().code)
                  << ": " << decoded.error().message << '\n';
        return EXIT_FAILURE;
      }
      const auto digest = sha256(decoded.value().bytes);
      if (digest.empty()) {
        std::cerr << "icc-profile[" << index
                  << "] decode: unable to compute profile SHA-256\n";
        return EXIT_FAILURE;
      }
      std::cout << "icc-profile[" << index
                << "] decoded-bytes: " << decoded.value().bytes.size()
                << " checksum: "
                << mmxisf::to_string(decoded.value().checksum_verification)
                << " profile-sha256: " << digest << '\n';
    }
  }
  for (const auto &binding : document.icc_profile_bindings()) {
    std::cout << "icc-profile-binding\tprofile[" << binding.profile_index
              << "]\timage[" << binding.image_index << "]\t"
              << (binding.by_reference ? "Reference" : "Direct") << '\n';
  }
  for (std::size_t index = 0; index < document.extension_elements().size();
       ++index) {
    const auto &extension = document.extension_elements()[index];
    std::cout << "extension[" << index << "]\t{" << extension.namespace_uri
              << '}' << extension.name << "\tparent={"
              << extension.parent_namespace_uri << '}' << extension.parent_name;
    if (extension.parent_extension_index) {
      std::cout << "\tparent-extension[" << *extension.parent_extension_index
                << ']';
    }
    if (extension.image_index) {
      std::cout << "\timage[" << *extension.image_index << ']';
    }
    if (!extension.text.empty()) {
      std::cout << "\ttext=" << std::quoted(extension.text);
    }
    std::cout << '\n';
    for (const auto &attribute : extension.attributes) {
      std::cout << "extension[" << index << "].attribute\t{"
                << attribute.namespace_uri << '}' << attribute.name << '='
                << std::quoted(attribute.value) << '\n';
    }
  }
  return EXIT_SUCCESS;
}
