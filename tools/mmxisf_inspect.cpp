// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
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

class HashingRowSink final : public mmxisf::ImageRowSink {
public:
  HashingRowSink() : context_(EVP_MD_CTX_new(), &EVP_MD_CTX_free) {
    ready_ = context_ != nullptr &&
             EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) == 1;
  }

  mmxisf::Result<void> consume(const mmxisf::ImageRowView &row) override {
    if (!ready_ || EVP_DigestUpdate(context_.get(), row.bytes.data(),
                                    row.bytes.size()) != 1) {
      return mmxisf::Error{.code = mmxisf::ErrorCode::internal_error,
                           .message = "Unable to hash streamed image row"};
    }
    ++rows;
    bytes += row.bytes.size();
    return {};
  }

  mmxisf::Result<std::string> finish() {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (!ready_ ||
        EVP_DigestFinal_ex(context_.get(), digest.data(), &digest_size) != 1 ||
        digest_size != 32) {
      return mmxisf::Error{.code = mmxisf::ErrorCode::internal_error,
                           .message = "Unable to finish streamed row hash"};
    }
    ready_ = false;
    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < digest_size; ++index) {
      encoded << std::setw(2) << static_cast<unsigned int>(digest[index]);
    }
    return encoded.str();
  }

  std::uint64_t rows{0};
  std::uint64_t bytes{0};

private:
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_;
  bool ready_{false};
};

} // namespace

int main(int argc, char **argv) {
  const bool decode_properties =
      argc == 3 && std::string(argv[1]) == "--decode-properties-sha256";
  const bool decode_icc =
      argc == 3 && std::string(argv[1]) == "--decode-icc-sha256";
  const bool decode_thumbnails =
      argc == 3 && std::string(argv[1]) == "--decode-thumbnails-sha256";
  const bool decode_rows = argc == 3 && std::string(argv[1]) == "--decode-rows";
  const bool decode = argc == 3 && (std::string(argv[1]) == "--decode" ||
                                    std::string(argv[1]) == "--decode-sha256");
  const bool decode_sha256 =
      decode && std::string(argv[1]) == "--decode-sha256";
  const bool has_option = decode || decode_properties || decode_icc ||
                          decode_thumbnails || decode_rows;
  if ((!has_option && argc != 2) || (argc == 3 && !has_option)) {
    std::cerr << "Usage: mmxisf-inspect "
                 "[--decode|--decode-sha256|--decode-properties-sha256|"
                 "--decode-icc-sha256|--decode-thumbnails-sha256|"
                 "--decode-rows] "
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
            << "thumbnails: " << document.thumbnails().size() << "\n"
            << "thumbnail-bindings: " << document.thumbnail_bindings().size()
            << "\n"
            << "table-structures: " << document.table_structures().size()
            << "\n"
            << "tables: " << document.tables().size() << "\n"
            << "table-bindings: " << document.table_bindings().size() << "\n"
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
    if (decode_rows) {
      HashingRowSink sink;
      auto streamed = result.value().read_image_rows(index, sink);
      if (!streamed) {
        std::cerr << "image[" << index << "] row-decode: "
                  << mmxisf::to_string(streamed.error().code) << ": "
                  << streamed.error().message << '\n';
        return EXIT_FAILURE;
      }
      std::cout << "image[" << index
                << "] planar-rows: " << streamed.value().rows_delivered
                << " decoded-bytes: " << streamed.value().bytes_delivered
                << " checksum: "
                << mmxisf::to_string(streamed.value().checksum_verification)
                << '\n';
      if (sink.rows != streamed.value().rows_delivered ||
          sink.bytes != streamed.value().bytes_delivered) {
        std::cerr << "image[" << index
                  << "] row-decode: sink accounting mismatch\n";
        return EXIT_FAILURE;
      }
      auto digest = sink.finish();
      if (!digest) {
        std::cerr << "image[" << index
                  << "] row-decode: " << digest.error().message << '\n';
        return EXIT_FAILURE;
      }
      std::cout << "image[" << index
                << "] row-stream-sha256: " << digest.value() << '\n';
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
  for (std::size_t index = 0; index < document.thumbnails().size(); ++index) {
    const auto &thumbnail = document.thumbnails()[index];
    std::cout << "thumbnail[" << index << "]\tgeometry=";
    for (std::size_t axis = 0; axis < thumbnail.image.geometry.size(); ++axis) {
      if (axis != 0) {
        std::cout << ':';
      }
      std::cout << thumbnail.image.geometry[axis];
    }
    std::cout << "\tsample=" << thumbnail.image.sample_format_name
              << "\tcolor=" << thumbnail.image.color_space << "\tstorage="
              << mmxisf::to_string(thumbnail.image.pixel_storage)
              << "\tlocation=" << thumbnail.image.block.raw;
    if (!thumbnail.uid.empty()) {
      std::cout << "\tuid=" << thumbnail.uid;
    }
    if (thumbnail.image_index) {
      std::cout << "\tdirect-image[" << *thumbnail.image_index << ']';
    }
    if (!thumbnail.image.compression.empty()) {
      std::cout << "\tcompression=" << thumbnail.image.compression;
    }
    if (!thumbnail.image.checksum.empty()) {
      std::cout << "\tchecksum=" << thumbnail.image.checksum;
    }
    std::cout << '\n';
    if (decode_thumbnails) {
      auto decoded = result.value().read_thumbnail(index);
      if (!decoded) {
        std::cerr << "thumbnail[" << index
                  << "] decode: " << mmxisf::to_string(decoded.error().code)
                  << ": " << decoded.error().message << '\n';
        return EXIT_FAILURE;
      }
      const auto digest = sha256(decoded.value().pixels);
      if (digest.empty()) {
        std::cerr << "thumbnail[" << index
                  << "] decode: unable to compute pixel SHA-256\n";
        return EXIT_FAILURE;
      }
      std::cout << "thumbnail[" << index
                << "] decoded-bytes: " << decoded.value().pixels.size()
                << " checksum: "
                << mmxisf::to_string(decoded.value().checksum_verification)
                << " pixel-sha256: " << digest << '\n';
    }
  }
  for (const auto &binding : document.thumbnail_bindings()) {
    std::cout << "thumbnail-binding\tthumbnail[" << binding.thumbnail_index
              << "]\timage[" << binding.image_index << "]\t"
              << (binding.by_reference ? "Reference" : "Direct") << '\n';
  }
  for (std::size_t index = 0; index < document.table_structures().size();
       ++index) {
    const auto &structure = document.table_structures()[index];
    std::cout << "table-structure[" << index
              << "]\tfields=" << structure.fields.size();
    if (!structure.uid.empty()) {
      std::cout << "\tuid=" << structure.uid;
    }
    if (structure.table_index) {
      std::cout << "\ttable[" << *structure.table_index << ']';
    }
    std::cout << '\n';
    for (std::size_t field_index = 0; field_index < structure.fields.size();
         ++field_index) {
      const auto &field = structure.fields[field_index];
      std::cout << "table-structure[" << index << "].field[" << field_index
                << "]\tid=" << field.id << "\ttype=" << field.type;
      if (!field.header.empty()) {
        std::cout << "\theader=" << std::quoted(field.header);
      }
      if (!field.format.empty()) {
        std::cout << "\tformat=" << std::quoted(field.format);
      }
      std::cout << '\n';
    }
  }
  for (std::size_t index = 0; index < document.tables().size(); ++index) {
    const auto &table = document.tables()[index];
    std::cout << "table[" << index << "]\tid=" << table.id
              << "\trows=" << table.rows.size();
    if (table.structure_index) {
      std::cout << "\tstructure[" << *table.structure_index << "]\t"
                << (table.structure_by_reference ? "Reference" : "Direct");
    }
    if (!table.uid.empty()) {
      std::cout << "\tuid=" << table.uid;
    }
    if (table.image_index) {
      std::cout << "\tdirect-image[" << *table.image_index << ']';
    }
    if (!table.caption.empty()) {
      std::cout << "\tcaption=" << std::quoted(table.caption);
    }
    std::cout << '\n';
  }
  for (const auto &binding : document.table_bindings()) {
    std::cout << "table-binding\ttable[" << binding.table_index << "]\timage["
              << binding.image_index << "]\t"
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
