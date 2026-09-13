// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <expat.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace mmxisf {
namespace {

constexpr std::array<unsigned char, 8> kSignature{'X', 'I', 'S', 'F',
                                                  '0', '1', '0', '0'};
constexpr std::string_view kXisfNamespace = "http://www.pixinsight.com/xisf";

Error make_error(ErrorCode code, std::string message) {
  Error error;
  error.code = code;
  error.message = std::move(message);
  return error;
}

std::string_view local_name(std::string_view qualified) {
  const auto separator = qualified.rfind('|');
  return separator == std::string_view::npos ? qualified
                                             : qualified.substr(separator + 1);
}

std::string_view namespace_name(std::string_view qualified) {
  const auto separator = qualified.rfind('|');
  return separator == std::string_view::npos ? std::string_view{}
                                             : qualified.substr(0, separator);
}

std::optional<std::string_view> attribute(const XML_Char **attributes,
                                          std::string_view wanted) {
  for (std::size_t i = 0; attributes[i] != nullptr; i += 2) {
    if (namespace_name(attributes[i]).empty() &&
        local_name(attributes[i]) == wanted) {
      return std::string_view(attributes[i + 1]);
    }
  }
  return std::nullopt;
}

template <typename T> bool parse_unsigned(std::string_view text, T &result) {
  if (text.empty()) {
    return false;
  }
  T value{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
    return false;
  }
  result = value;
  return true;
}

Result<std::pair<double, double>> parse_bounds(std::string_view text) {
  const auto separator = text.find(':');
  if (separator == std::string_view::npos ||
      text.find(':', separator + 1) != std::string_view::npos) {
    return make_error(ErrorCode::invalid_xisf,
                      "Image bounds must contain two values");
  }
  const auto parse_value = [](std::string_view token, double &value) -> bool {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = token.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
      return false;
    }
    token.remove_prefix(first);
    const auto last = token.find_last_not_of(whitespace);
    token = token.substr(0, last + 1);
    if (token.starts_with('+')) {
      token.remove_prefix(1);
    }
    if (token.empty()) {
      return false;
    }
    const auto parsed =
        std::from_chars(token.data(), token.data() + token.size(), value,
                        std::chars_format::general);
    return parsed.ec == std::errc{} &&
           parsed.ptr == token.data() + token.size() && std::isfinite(value);
  };
  double lower = 0;
  double upper = 0;
  if (!parse_value(text.substr(0, separator), lower) ||
      !parse_value(text.substr(separator + 1), upper) || !(lower < upper)) {
    return make_error(ErrorCode::invalid_xisf,
                      "Image bounds are not a finite increasing range");
  }
  return std::pair<double, double>{lower, upper};
}

bool checked_multiply(std::uint64_t left, std::uint64_t right,
                      std::uint64_t &result) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

class FileByteSource final : public ByteSource {
public:
  static Result<std::shared_ptr<const ByteSource>>
  open(const std::filesystem::path &path) {
    auto source = std::shared_ptr<FileByteSource>(new FileByteSource(path));
    if (!source->input_) {
      return make_error(ErrorCode::io_error, "Unable to open XISF file");
    }
    source->input_.seekg(0, std::ios::end);
    const auto end = source->input_.tellg();
    if (end < 0) {
      return make_error(ErrorCode::io_error,
                        "Unable to determine XISF file size");
    }
    source->source_size_ = static_cast<std::uint64_t>(end);
    source->input_.clear();
    return std::shared_ptr<const ByteSource>(std::move(source));
  }

  Result<std::uint64_t> size() const override { return source_size_; }

  Result<std::size_t> read_at(std::uint64_t offset,
                              std::span<std::byte> destination) const override {
    if (offset > source_size_ || destination.size() > source_size_ - offset) {
      return make_error(ErrorCode::io_error,
                        "ByteSource read range is outside the file");
    }
    if (destination.empty()) {
      return std::size_t{0};
    }
    if (offset > static_cast<std::uint64_t>(
                     std::numeric_limits<std::streamoff>::max()) ||
        destination.size() > static_cast<std::size_t>(
                                 std::numeric_limits<std::streamsize>::max())) {
      return make_error(
          ErrorCode::overflow,
          "ByteSource read cannot be represented by the stream API");
    }
    std::lock_guard lock(mutex_);
    input_.clear();
    input_.seekg(static_cast<std::streamoff>(offset));
    if (!input_) {
      return make_error(ErrorCode::io_error, "Unable to seek in XISF file");
    }
    input_.read(reinterpret_cast<char *>(destination.data()),
                static_cast<std::streamsize>(destination.size()));
    const auto count = input_.gcount();
    if (count < 0) {
      return make_error(ErrorCode::io_error, "Invalid XISF file read count");
    }
    return static_cast<std::size_t>(count);
  }

private:
  explicit FileByteSource(const std::filesystem::path &path)
      : input_(path, std::ios::binary) {}

  mutable std::ifstream input_;
  std::uint64_t source_size_{0};
  mutable std::mutex mutex_;
};

Result<std::size_t> read_exact(const ByteSource &source, std::uint64_t offset,
                               std::span<std::byte> destination) {
  std::size_t total = 0;
  while (total < destination.size()) {
    const auto total_offset = static_cast<std::uint64_t>(total);
    if (offset > std::numeric_limits<std::uint64_t>::max() - total_offset) {
      return make_error(ErrorCode::overflow,
                        "ByteSource read offset overflows");
    }
    auto read =
        source.read_at(offset + total_offset, destination.subspan(total));
    if (!read) {
      return read.error();
    }
    if (read.value() == 0 || read.value() > destination.size() - total) {
      return make_error(ErrorCode::io_error,
                        "ByteSource returned an invalid short read");
    }
    total += read.value();
  }
  return total;
}

Result<std::vector<std::uint64_t>> parse_geometry(std::string_view text) {
  std::vector<std::uint64_t> axes;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find(':', start);
    const auto token =
        text.substr(start, end == std::string_view::npos ? text.size() - start
                                                         : end - start);
    std::uint64_t value = 0;
    if (!parse_unsigned(token, value) || value == 0) {
      return make_error(ErrorCode::invalid_xisf,
                        "Image geometry contains an invalid axis");
    }
    axes.push_back(value);
    if (axes.size() > 9) {
      return make_error(ErrorCode::resource_limit,
                        "Image geometry exceeds the dimension limit");
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  if (axes.size() < 2) {
    return make_error(ErrorCode::invalid_xisf,
                      "Image geometry must contain a dimension and channels");
  }
  return axes;
}

BlockLocation parse_location(std::string_view text) {
  BlockLocation result;
  result.raw = std::string(text);
  constexpr std::string_view prefix = "attachment:";
  if (text.starts_with(prefix)) {
    const auto tail = text.substr(prefix.size());
    const auto separator = tail.find(':');
    if (separator == std::string_view::npos) {
      return result;
    }
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    if (parse_unsigned(tail.substr(0, separator), offset) &&
        parse_unsigned(tail.substr(separator + 1), size)) {
      result.kind = BlockKind::attachment;
      result.offset = offset;
      result.size = size;
    }
  } else if (text == "embedded") {
    result.kind = BlockKind::embedded;
  } else if (text.starts_with("inline:")) {
    result.kind = BlockKind::inline_data;
  } else if (text.starts_with("url(") || text.starts_with("path(")) {
    result.kind = BlockKind::external;
  }
  return result;
}

struct XmlBuilder {
  std::string version;
  std::vector<ImageInfo> images;
  std::vector<MetadataEntry> metadata;
  ReaderOptions options;
  XML_Parser parser{nullptr};
  std::optional<Error> error;
  std::size_t depth{0};
  std::size_t nodes{0};
  bool saw_root{false};
  bool root_closed{false};
  std::vector<std::size_t> image_stack;
  std::vector<std::string> element_stack;
  std::optional<std::size_t> text_metadata_index;
  std::size_t metadata_count{0};
  bool saw_creation_time{false};
  bool saw_creator_application{false};

  void fail(ErrorCode code, std::string message, std::string element = {},
            std::string attribute_name = {}) {
    if (error) {
      return;
    }
    Error detail;
    detail.code = code;
    detail.message = std::move(message);
    detail.element = std::move(element);
    detail.attribute = std::move(attribute_name);
    error = std::move(detail);
    XML_StopParser(parser, XML_FALSE);
  }

  std::optional<std::size_t> current_image() const {
    return image_stack.empty() ? std::nullopt
                               : std::optional<std::size_t>(image_stack.back());
  }
};

void XMLCALL start_element(void *user_data, const XML_Char *qualified_name,
                           const XML_Char **attributes) {
  auto &state = *static_cast<XmlBuilder *>(user_data);
  if (state.error) {
    return;
  }
  ++state.depth;
  ++state.nodes;
  const std::string name(local_name(qualified_name));
  const bool is_xisf_element = namespace_name(qualified_name) == kXisfNamespace;
  const std::string parent =
      state.element_stack.empty() ? std::string{} : state.element_stack.back();
  if (state.depth > state.options.max_xml_depth) {
    state.fail(ErrorCode::resource_limit, "XML depth limit exceeded", name);
    return;
  }
  if (state.nodes > state.options.max_xml_nodes) {
    state.fail(ErrorCode::resource_limit, "XML node limit exceeded", name);
    return;
  }
  std::size_t attribute_count = 0;
  for (; attributes[attribute_count * 2] != nullptr; ++attribute_count) {
  }
  if (attribute_count > state.options.max_attributes_per_element) {
    state.fail(ErrorCode::resource_limit, "XML attribute limit exceeded", name);
    return;
  }
  state.element_stack.push_back(is_xisf_element ? name : std::string{});

  if (state.depth == 1) {
    if (state.saw_root || name != "xisf" ||
        namespace_name(qualified_name) != kXisfNamespace) {
      state.fail(ErrorCode::invalid_xisf,
                 "The XML root must be xisf in the XISF namespace", name);
      return;
    }
    state.saw_root = true;
    const auto version = attribute(attributes, "version");
    if (!version || *version != "1.0") {
      state.fail(ErrorCode::invalid_xisf, "Only XISF version 1.0 is accepted",
                 name, "version");
      return;
    }
    state.version = std::string(*version);
    return;
  }

  if (is_xisf_element && name == "Image") {
    if (state.depth != 2 || parent != "xisf") {
      state.fail(ErrorCode::invalid_xisf,
                 "Image must be a direct child of the XISF root", name);
      return;
    }
    if (state.images.size() >= state.options.max_images) {
      state.fail(ErrorCode::resource_limit, "Image count limit exceeded", name);
      return;
    }
    const auto geometry = attribute(attributes, "geometry");
    const auto sample_format = attribute(attributes, "sampleFormat");
    const auto color_space = attribute(attributes, "colorSpace");
    const auto location = attribute(attributes, "location");
    if (!geometry || !sample_format || !location) {
      state.fail(ErrorCode::invalid_xisf,
                 "Image is missing a required attribute", name);
      return;
    }
    auto parsed_geometry = parse_geometry(*geometry);
    if (!parsed_geometry) {
      state.fail(parsed_geometry.error().code, parsed_geometry.error().message,
                 name, "geometry");
      return;
    }
    if (parsed_geometry.value().size() - 1 >
        state.options.max_image_dimensions) {
      state.fail(ErrorCode::resource_limit,
                 "Image geometry exceeds the configured dimension limit", name,
                 "geometry");
      return;
    }
    if (parsed_geometry.value().back() > state.options.max_inspected_channels) {
      state.fail(ErrorCode::resource_limit,
                 "Image channel count exceeds the inspection limit", name,
                 "geometry");
      return;
    }
    ImageInfo image;
    image.geometry = std::move(parsed_geometry).value();
    image.sample_format_name = std::string(*sample_format);
    if (*sample_format == "UInt8") {
      image.sample_format = SampleFormat::uint8;
    } else if (*sample_format == "UInt16") {
      image.sample_format = SampleFormat::uint16;
    } else if (*sample_format == "Float32") {
      image.sample_format = SampleFormat::float32;
    }
    constexpr std::array<std::string_view, 8> kSampleFormats{
        "UInt8",   "UInt16",  "UInt32",    "UInt64",
        "Float32", "Float64", "Complex32", "Complex64"};
    if (std::find(kSampleFormats.begin(), kSampleFormats.end(),
                  *sample_format) == kSampleFormats.end()) {
      state.fail(ErrorCode::invalid_xisf, "Invalid Image sampleFormat value",
                 name, "sampleFormat");
      return;
    }
    const auto bounds = attribute(attributes, "bounds");
    if ((*sample_format == "Float32" || *sample_format == "Float64") &&
        !bounds) {
      state.fail(ErrorCode::invalid_xisf,
                 "Floating-point Image requires bounds", name, "bounds");
      return;
    }
    if (bounds) {
      auto parsed_bounds = parse_bounds(*bounds);
      if (!parsed_bounds) {
        state.fail(parsed_bounds.error().code, parsed_bounds.error().message,
                   name, "bounds");
        return;
      }
      image.lower_bound = parsed_bounds.value().first;
      image.upper_bound = parsed_bounds.value().second;
    }
    image.color_space = std::string(color_space.value_or("Gray"));
    if (image.color_space != "Gray" && image.color_space != "RGB" &&
        image.color_space != "CIELab") {
      state.fail(ErrorCode::invalid_xisf, "Invalid Image colorSpace value",
                 name, "colorSpace");
      return;
    }
    image.id = std::string(attribute(attributes, "id").value_or(""));
    const auto pixel_storage =
        attribute(attributes, "pixelStorage").value_or("Planar");
    if (pixel_storage != "Planar" && pixel_storage != "Normal") {
      state.fail(ErrorCode::invalid_xisf, "Invalid Image pixelStorage value",
                 name, "pixelStorage");
      return;
    }
    image.pixel_storage =
        pixel_storage == "Normal" ? PixelStorage::normal : PixelStorage::planar;
    const auto byte_order =
        attribute(attributes, "byteOrder").value_or("little");
    if (byte_order != "little" && byte_order != "big") {
      state.fail(ErrorCode::invalid_xisf, "Invalid Image byteOrder value", name,
                 "byteOrder");
      return;
    }
    image.byte_order = byte_order == "big" ? ByteOrder::big : ByteOrder::little;
    image.block = parse_location(*location);
    if (image.block.kind == BlockKind::inline_data) {
      state.fail(ErrorCode::invalid_xisf,
                 "Image pixel data cannot use an inline block", name,
                 "location");
      return;
    }
    if (image.block.kind == BlockKind::unknown) {
      state.fail(ErrorCode::invalid_xisf, "Invalid Image block location", name,
                 "location");
      return;
    }
    image.compression =
        std::string(attribute(attributes, "compression").value_or(""));
    image.checksum =
        std::string(attribute(attributes, "checksum").value_or(""));
    state.images.push_back(std::move(image));
    state.image_stack.push_back(state.images.size() - 1);
    return;
  }

  if (is_xisf_element && name == "Metadata") {
    if (state.depth != 2 || parent != "xisf" || state.metadata_count != 0) {
      state.fail(ErrorCode::invalid_xisf,
                 "Exactly one Metadata element must be a direct root child",
                 name);
      return;
    }
    ++state.metadata_count;
    return;
  }

  if (is_xisf_element && (name == "Property" || name == "FITSKeyword")) {
    const bool valid_parent =
        name == "Property"
            ? (parent == "xisf" || parent == "Image" || parent == "Metadata")
            : (parent == "xisf" || parent == "Image");
    if (!valid_parent) {
      state.fail(ErrorCode::invalid_xisf,
                 name + " has an invalid parent element", name);
      return;
    }
    if (state.metadata.size() >= state.options.max_metadata_entries) {
      state.fail(ErrorCode::resource_limit, "Metadata entry limit exceeded",
                 name);
      return;
    }
    const auto identity =
        attribute(attributes, name == "Property" ? "id" : "name");
    const auto type = attribute(attributes, "type");
    const auto value = attribute(attributes, "value");
    const auto comment = attribute(attributes, "comment");
    if (!identity || identity->empty() ||
        (name == "Property" && (!type || type->empty())) ||
        (name == "FITSKeyword" && (!value || !comment))) {
      state.fail(ErrorCode::invalid_xisf,
                 name + " is missing a mandatory attribute", name);
      return;
    }
    if (name == "Property" && parent == "Metadata") {
      if (!identity->starts_with("XISF:")) {
        state.fail(ErrorCode::invalid_xisf,
                   "Metadata Property identifiers must use the XISF namespace",
                   name, "id");
        return;
      }
      if (*identity == "XISF:CreationTime") {
        const bool compatible_type = *type == "TimePoint" || *type == "String";
        if (!compatible_type || state.saw_creation_time) {
          state.fail(ErrorCode::invalid_xisf,
                     "Invalid or duplicate XISF:CreationTime property", name,
                     "type");
          return;
        }
        state.saw_creation_time = true;
      } else if (*identity == "XISF:CreatorApplication") {
        if (*type != "String" || state.saw_creator_application) {
          state.fail(ErrorCode::invalid_xisf,
                     "Invalid or duplicate XISF:CreatorApplication property",
                     name, "type");
          return;
        }
        state.saw_creator_application = true;
      }
    }
    MetadataEntry entry;
    entry.kind = name == "Property" ? MetadataEntry::Kind::property
                                    : MetadataEntry::Kind::fits_keyword;
    entry.image_index = state.current_image();
    entry.name = std::string(*identity);
    entry.type = std::string(attribute(attributes, "type").value_or(""));
    entry.value = std::string(attribute(attributes, "value").value_or(""));
    entry.comment = std::string(attribute(attributes, "comment").value_or(""));
    state.metadata.push_back(std::move(entry));
    if (name == "Property" && !attribute(attributes, "value")) {
      state.text_metadata_index = state.metadata.size() - 1;
    }
  }
}

void XMLCALL end_element(void *user_data, const XML_Char *qualified_name) {
  auto &state = *static_cast<XmlBuilder *>(user_data);
  if (state.error) {
    return;
  }
  const std::string name(local_name(qualified_name));
  const bool is_xisf_element = namespace_name(qualified_name) == kXisfNamespace;
  if (is_xisf_element && name == "Property") {
    state.text_metadata_index.reset();
  } else if (is_xisf_element && name == "Image") {
    if (!state.image_stack.empty()) {
      state.image_stack.pop_back();
    }
  }
  if (state.depth == 1) {
    state.root_closed = true;
  }
  if (state.depth > 0) {
    --state.depth;
  }
  if (!state.element_stack.empty()) {
    state.element_stack.pop_back();
  }
}

void XMLCALL character_data(void *user_data, const XML_Char *text, int length) {
  auto &state = *static_cast<XmlBuilder *>(user_data);
  if (state.error || !state.text_metadata_index || length <= 0) {
    return;
  }
  auto &value = state.metadata[*state.text_metadata_index].value;
  const auto limit = state.options.max_metadata_value_bytes;
  const auto available = limit - std::min(value.size(), limit);
  if (static_cast<std::size_t>(length) > available) {
    state.fail(ErrorCode::resource_limit,
               "Inline metadata value exceeds the inspection limit",
               "Property");
    return;
  }
  value.append(text, static_cast<std::size_t>(length));
}

void XMLCALL reject_doctype(void *user_data, const XML_Char *, const XML_Char *,
                            const XML_Char *, int) {
  auto &state = *static_cast<XmlBuilder *>(user_data);
  state.fail(ErrorCode::malformed_xml,
             "DOCTYPE is not allowed in XISF headers");
}

Result<Document> parse_header(std::string_view xml,
                              const ReaderOptions &options,
                              std::uint64_t file_size,
                              std::uint32_t header_length) {
  XmlBuilder state;
  state.options = options;
  state.parser = XML_ParserCreateNS("UTF-8", '|');
  if (state.parser == nullptr) {
    return make_error(ErrorCode::internal_error, "Unable to create XML parser");
  }
  XML_SetUserData(state.parser, &state);
  XML_SetElementHandler(state.parser, start_element, end_element);
  XML_SetCharacterDataHandler(state.parser, character_data);
  XML_SetStartDoctypeDeclHandler(state.parser, reject_doctype);

  constexpr std::size_t kXmlChunkBytes = 1024U * 1024U;
  std::size_t offset = 0;
  while (offset < xml.size()) {
    const auto chunk = std::min(kXmlChunkBytes, xml.size() - offset);
    const bool is_final = offset + chunk == xml.size();
    const auto status =
        XML_Parse(state.parser, xml.data() + offset, static_cast<int>(chunk),
                  is_final ? XML_TRUE : XML_FALSE);
    if (status == XML_STATUS_ERROR) {
      if (!state.error) {
        Error detail =
            make_error(ErrorCode::malformed_xml,
                       std::string("Malformed XML: ") +
                           XML_ErrorString(XML_GetErrorCode(state.parser)));
        detail.byte_offset =
            static_cast<std::uint64_t>(XML_GetCurrentByteIndex(state.parser));
        state.error = std::move(detail);
      }
      break;
    }
    offset += chunk;
  }
  XML_ParserFree(state.parser);
  state.parser = nullptr;
  if (state.error) {
    return std::move(*state.error);
  }
  if (!state.saw_root || !state.root_closed) {
    return make_error(ErrorCode::invalid_xisf, "Incomplete XISF root element");
  }
  if (state.metadata_count != 1) {
    return make_error(ErrorCode::invalid_xisf,
                      "XISF header must contain exactly one Metadata element");
  }
  if (!state.saw_creation_time || !state.saw_creator_application) {
    return make_error(
        ErrorCode::invalid_xisf,
        "Metadata must define XISF:CreationTime and XISF:CreatorApplication");
  }
  const auto header_end = 16ULL + static_cast<std::uint64_t>(header_length);
  for (std::size_t index = 0; index < state.images.size(); ++index) {
    const auto &block = state.images[index].block;
    if (block.kind != BlockKind::attachment) {
      continue;
    }
    if (block.size == 0 || block.offset < header_end ||
        block.offset > file_size || block.size > file_size - block.offset) {
      Error error =
          make_error(ErrorCode::invalid_block,
                     "Image attachment range is outside the file payload");
      error.image_index = index;
      return error;
    }
  }
  return Document(std::move(state.version), std::move(state.images),
                  std::move(state.metadata), file_size, header_length);
}

std::uint32_t read_le_u32(const unsigned char *bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::optional<std::uint64_t> bytes_per_sample(SampleFormat format) {
  switch (format) {
  case SampleFormat::uint8:
    return 1;
  case SampleFormat::uint16:
    return 2;
  case SampleFormat::float32:
    return 4;
  case SampleFormat::unsupported:
    return std::nullopt;
  }
  return std::nullopt;
}

struct ImageReadPlan {
  const ImageInfo *image{nullptr};
  std::uint64_t channels{0};
  std::uint64_t expected_bytes{0};
};

Result<ImageReadPlan> plan_image_read(const Document &document,
                                      const ReaderOptions &options,
                                      std::size_t image_index) {
  if (image_index >= document.images().size()) {
    Error error = make_error(ErrorCode::invalid_argument,
                             "Image index is outside the document");
    error.image_index = image_index;
    return error;
  }
  const auto &image = document.images()[image_index];
  if (image.block.kind != BlockKind::attachment) {
    return make_error(ErrorCode::unsupported_feature,
                      "M1 PoC only reads local attachment image blocks");
  }
  if (!image.compression.empty()) {
    return make_error(ErrorCode::unsupported_feature,
                      "Compressed image blocks are scheduled for M3");
  }
  if (!image.checksum.empty()) {
    return make_error(ErrorCode::unsupported_feature,
                      "Checksummed image blocks are scheduled for M3");
  }
  if (image.geometry.size() != 3) {
    return make_error(ErrorCode::unsupported_feature,
                      "M1 PoC only reads 2-D images");
  }
  const auto sample_size = bytes_per_sample(image.sample_format);
  if (!sample_size) {
    return make_error(ErrorCode::unsupported_feature,
                      "M1 PoC supports UInt8, UInt16, and Float32 samples");
  }
  const std::uint64_t channels = image.geometry[2];
  if (channels > options.max_decoded_channels) {
    return make_error(ErrorCode::resource_limit,
                      "Image channel count exceeds the decode limit");
  }
  std::uint64_t sample_count = 0;
  std::uint64_t expected_bytes = 0;
  if (!checked_multiply(image.geometry[0], image.geometry[1], sample_count) ||
      !checked_multiply(sample_count, channels, sample_count) ||
      !checked_multiply(sample_count, *sample_size, expected_bytes)) {
    return make_error(ErrorCode::overflow,
                      "Image geometry overflows the byte-size calculation");
  }
  if (sample_count > options.max_samples_per_image) {
    return make_error(ErrorCode::resource_limit,
                      "Image exceeds the configured sample-count limit");
  }
  if (expected_bytes > options.max_decoded_image_bytes ||
      expected_bytes > std::numeric_limits<std::size_t>::max()) {
    return make_error(ErrorCode::resource_limit,
                      "Image exceeds the configured decoded byte limit");
  }
  if (image.block.size != expected_bytes) {
    return make_error(
        ErrorCode::invalid_block,
        "Attachment size does not match image geometry and sample format");
  }
  if (image.block.offset > document.file_size() ||
      image.block.size > document.file_size() - image.block.offset) {
    return make_error(ErrorCode::invalid_block,
                      "Attachment range extends beyond the source");
  }
  return ImageReadPlan{&image, channels, expected_bytes};
}

} // namespace

struct Reader::Impl {
  std::shared_ptr<const ByteSource> source;
  ReaderOptions options;
  Document document;
};

Reader::Reader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Reader::Reader(Reader &&) noexcept = default;
Reader &Reader::operator=(Reader &&) noexcept = default;
Reader::~Reader() = default;

Result<Reader> Reader::open_file(const std::filesystem::path &path,
                                 ReaderOptions options) {
  try {
    auto source = FileByteSource::open(path);
    if (!source) {
      return source.error();
    }
    return open_source(std::move(source).value(), options);
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while opening XISF");
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected file-source failure: ") +
                          exception.what());
  }
}

Result<Reader> Reader::open_source(std::shared_ptr<const ByteSource> source,
                                   ReaderOptions options) {
  try {
    if (!source) {
      return make_error(ErrorCode::invalid_argument,
                        "ByteSource must not be null");
    }
    auto size_result = source->size();
    if (!size_result) {
      return size_result.error();
    }
    const auto source_size = size_result.value();
    if (source_size < 16) {
      return make_error(ErrorCode::invalid_preamble,
                        "File is shorter than the XISF preamble");
    }
    std::array<unsigned char, 16> preamble{};
    auto preamble_read =
        read_exact(*source, 0, std::as_writable_bytes(std::span(preamble)));
    if (!preamble_read) {
      return preamble_read.error();
    }
    if (!std::equal(kSignature.begin(), kSignature.end(), preamble.begin())) {
      return make_error(ErrorCode::invalid_signature,
                        "File does not have the XISF0100 signature");
    }
    const auto header_length = read_le_u32(preamble.data() + 8);
    if (preamble[12] != 0 || preamble[13] != 0 || preamble[14] != 0 ||
        preamble[15] != 0) {
      return make_error(ErrorCode::invalid_preamble,
                        "Reserved XISF preamble bytes must be zero");
    }
    if (header_length == 0 || header_length > options.max_header_bytes) {
      return make_error(
          ErrorCode::header_too_large,
          "XISF XML header length is outside the configured limit");
    }
    if (static_cast<std::uint64_t>(header_length) > source_size - 16) {
      return make_error(ErrorCode::invalid_preamble,
                        "XISF XML header extends beyond the file");
    }
    if constexpr (sizeof(std::size_t) < sizeof(header_length)) {
      if (header_length > std::numeric_limits<std::size_t>::max()) {
        return make_error(ErrorCode::resource_limit,
                          "XISF XML header cannot fit in addressable memory");
      }
    }
    std::string xml(header_length, '\0');
    auto header_read =
        read_exact(*source, 16, std::as_writable_bytes(std::span(xml)));
    if (!header_read) {
      return header_read.error();
    }
    auto parsed = parse_header(xml, options, source_size, header_length);
    if (!parsed) {
      return parsed.error();
    }
    auto impl = std::make_unique<Impl>();
    impl->source = std::move(source);
    impl->options = options;
    impl->document = std::move(parsed).value();
    return Reader(std::move(impl));
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while opening XISF");
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected source-reader failure: ") +
                          exception.what());
  }
}

const Document &Reader::document() const noexcept { return impl_->document; }

Result<RawImage> Reader::read_image(std::size_t image_index,
                                    std::stop_token stop_token) const {
  try {
    auto plan = plan_image_read(impl_->document, impl_->options, image_index);
    if (!plan) {
      return plan.error();
    }
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    RawImage result;
    result.width = plan.value().image->geometry[0];
    result.height = plan.value().image->geometry[1];
    result.channels = plan.value().channels;
    result.sample_format = plan.value().image->sample_format;
    result.lower_bound = plan.value().image->lower_bound;
    result.upper_bound = plan.value().image->upper_bound;
    result.pixel_storage = plan.value().image->pixel_storage;
    result.byte_order = plan.value().image->byte_order;
    result.pixels.resize(static_cast<std::size_t>(plan.value().expected_bytes));
    auto read = read_image_into(image_index, result.pixels, stop_token);
    if (!read) {
      return read.error();
    }
    return result;
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while reading image");
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected image read failure: ") +
                          exception.what());
  }
}

Result<std::size_t> Reader::read_image_into(std::size_t image_index,
                                            std::span<std::byte> destination,
                                            std::stop_token stop_token) const {
  try {
    auto plan = plan_image_read(impl_->document, impl_->options, image_index);
    if (!plan) {
      return plan.error();
    }
    const auto expected = static_cast<std::size_t>(plan.value().expected_bytes);
    if (destination.size() < expected) {
      return make_error(ErrorCode::invalid_argument,
                        "Destination buffer is smaller than the image block");
    }
    constexpr std::size_t kReadChunkBytes = 8U * 1024U * 1024U;
    std::size_t total = 0;
    while (total < expected) {
      if (stop_token.stop_requested()) {
        return make_error(ErrorCode::cancelled, "Image read was cancelled");
      }
      const auto chunk = std::min(kReadChunkBytes, expected - total);
      std::size_t chunk_read = 0;
      while (chunk_read < chunk) {
        if (stop_token.stop_requested()) {
          return make_error(ErrorCode::cancelled, "Image read was cancelled");
        }
        const auto output = destination.subspan(total, chunk - chunk_read);
        auto read = impl_->source->read_at(
            plan.value().image->block.offset + total, output);
        if (!read) {
          auto error = read.error();
          if (!error.image_index) {
            error.image_index = image_index;
          }
          return error;
        }
        if (read.value() == 0 || read.value() > output.size()) {
          Error error = make_error(ErrorCode::io_error,
                                   "ByteSource returned an invalid short read");
          error.image_index = image_index;
          return error;
        }
        total += read.value();
        chunk_read += read.value();
      }
    }
    return total;
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected image buffer read failure: ") +
                          exception.what());
  }
}

const char *to_string(ErrorCode code) noexcept {
  switch (code) {
  case ErrorCode::io_error:
    return "io_error";
  case ErrorCode::invalid_signature:
    return "invalid_signature";
  case ErrorCode::invalid_preamble:
    return "invalid_preamble";
  case ErrorCode::header_too_large:
    return "header_too_large";
  case ErrorCode::malformed_xml:
    return "malformed_xml";
  case ErrorCode::resource_limit:
    return "resource_limit";
  case ErrorCode::invalid_xisf:
    return "invalid_xisf";
  case ErrorCode::unsupported_feature:
    return "unsupported_feature";
  case ErrorCode::invalid_block:
    return "invalid_block";
  case ErrorCode::invalid_argument:
    return "invalid_argument";
  case ErrorCode::overflow:
    return "overflow";
  case ErrorCode::cancelled:
    return "cancelled";
  case ErrorCode::internal_error:
    return "internal_error";
  }
  return "unknown";
}

const char *to_string(SampleFormat format) noexcept {
  switch (format) {
  case SampleFormat::uint8:
    return "UInt8";
  case SampleFormat::uint16:
    return "UInt16";
  case SampleFormat::float32:
    return "Float32";
  case SampleFormat::unsupported:
    return "Unsupported";
  }
  return "Unsupported";
}

const char *to_string(PixelStorage storage) noexcept {
  return storage == PixelStorage::planar ? "Planar" : "Normal";
}

const char *to_string(ByteOrder order) noexcept {
  return order == ByteOrder::little ? "Little-endian" : "Big-endian";
}

const char *to_string(BlockKind kind) noexcept {
  switch (kind) {
  case BlockKind::attachment:
    return "Attachment";
  case BlockKind::embedded:
    return "Embedded";
  case BlockKind::inline_data:
    return "Inline";
  case BlockKind::external:
    return "External";
  case BlockKind::unknown:
    return "Unknown";
  }
  return "Unknown";
}

} // namespace mmxisf
