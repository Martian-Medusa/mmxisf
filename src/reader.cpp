// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <expat.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <limits>
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

bool checked_multiply(std::uint64_t left, std::uint64_t right,
                      std::uint64_t &result) {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
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
    if (axes.size() > 8) {
      return make_error(ErrorCode::resource_limit,
                        "Image geometry exceeds the axis limit");
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  if (axes.size() < 2) {
    return make_error(ErrorCode::invalid_xisf,
                      "Image geometry must contain at least two axes");
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
  std::optional<std::size_t> text_metadata_index;

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

  const bool is_xisf_element = namespace_name(qualified_name) == kXisfNamespace;

  if (is_xisf_element && name == "Image") {
    if (state.images.size() >= state.options.max_images) {
      state.fail(ErrorCode::resource_limit, "Image count limit exceeded", name);
      return;
    }
    const auto geometry = attribute(attributes, "geometry");
    const auto sample_format = attribute(attributes, "sampleFormat");
    const auto color_space = attribute(attributes, "colorSpace");
    const auto location = attribute(attributes, "location");
    if (!geometry || !sample_format || !color_space || !location) {
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
    if (parsed_geometry.value().size() > state.options.max_image_axes) {
      state.fail(ErrorCode::resource_limit,
                 "Image geometry exceeds the configured axis limit", name,
                 "geometry");
      return;
    }
    if (parsed_geometry.value().size() == 3 &&
        parsed_geometry.value()[2] > state.options.max_inspected_channels) {
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
    image.color_space = std::string(*color_space);
    image.id = std::string(attribute(attributes, "id").value_or(""));
    const auto pixel_storage =
        attribute(attributes, "pixelStorage").value_or("planar");
    if (pixel_storage != "planar" && pixel_storage != "normal") {
      state.fail(ErrorCode::invalid_xisf, "Invalid Image pixelStorage value",
                 name, "pixelStorage");
      return;
    }
    image.pixel_storage =
        pixel_storage == "normal" ? PixelStorage::normal : PixelStorage::planar;
    const auto byte_order =
        attribute(attributes, "byteOrder").value_or("little");
    if (byte_order != "little" && byte_order != "big") {
      state.fail(ErrorCode::invalid_xisf, "Invalid Image byteOrder value", name,
                 "byteOrder");
      return;
    }
    image.byte_order = byte_order == "big" ? ByteOrder::big : ByteOrder::little;
    image.block = parse_location(*location);
    image.compression =
        std::string(attribute(attributes, "compression").value_or(""));
    image.checksum =
        std::string(attribute(attributes, "checksum").value_or(""));
    state.images.push_back(std::move(image));
    state.image_stack.push_back(state.images.size() - 1);
    return;
  }

  if (is_xisf_element && (name == "Property" || name == "FITSKeyword")) {
    if (state.metadata.size() >= state.options.max_metadata_entries) {
      state.fail(ErrorCode::resource_limit, "Metadata entry limit exceeded",
                 name);
      return;
    }
    MetadataEntry entry;
    entry.kind = name == "Property" ? MetadataEntry::Kind::property
                                    : MetadataEntry::Kind::fits_keyword;
    entry.image_index = state.current_image();
    entry.name = std::string(
        attribute(attributes, name == "Property" ? "id" : "name").value_or(""));
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

  const auto status = XML_Parse(state.parser, xml.data(),
                                static_cast<int>(xml.size()), XML_TRUE);
  if (status == XML_STATUS_ERROR && !state.error) {
    Error detail =
        make_error(ErrorCode::malformed_xml,
                   std::string("Malformed XML: ") +
                       XML_ErrorString(XML_GetErrorCode(state.parser)));
    detail.byte_offset =
        static_cast<std::uint64_t>(XML_GetCurrentByteIndex(state.parser));
    state.error = std::move(detail);
  }
  XML_ParserFree(state.parser);
  state.parser = nullptr;
  if (state.error) {
    return std::move(*state.error);
  }
  if (!state.saw_root || !state.root_closed) {
    return make_error(ErrorCode::invalid_xisf, "Incomplete XISF root element");
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

} // namespace

struct Reader::Impl {
  std::filesystem::path path;
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
    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(path, size_error);
    if (size_error) {
      return make_error(ErrorCode::io_error,
                        "Unable to determine XISF file size: " +
                            size_error.message());
    }
    if (file_size < 16) {
      return make_error(ErrorCode::invalid_preamble,
                        "File is shorter than the XISF preamble");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      return make_error(ErrorCode::io_error, "Unable to open XISF file");
    }
    std::array<unsigned char, 16> preamble{};
    input.read(reinterpret_cast<char *>(preamble.data()), preamble.size());
    if (input.gcount() != static_cast<std::streamsize>(preamble.size())) {
      return make_error(ErrorCode::io_error, "Unable to read XISF preamble");
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
    if (static_cast<std::uint64_t>(header_length) > file_size - 16) {
      return make_error(ErrorCode::invalid_preamble,
                        "XISF XML header extends beyond the file");
    }
    std::string xml(header_length, '\0');
    input.read(xml.data(), static_cast<std::streamsize>(header_length));
    if (input.gcount() != static_cast<std::streamsize>(header_length)) {
      return make_error(ErrorCode::io_error, "Unable to read XISF XML header");
    }
    auto parsed = parse_header(xml, options, file_size, header_length);
    if (!parsed) {
      return parsed.error();
    }
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    impl->options = options;
    impl->document = std::move(parsed).value();
    return Reader(std::move(impl));
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while opening XISF");
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected reader failure: ") +
                          exception.what());
  }
}

const Document &Reader::document() const noexcept { return impl_->document; }

Result<RawImage> Reader::read_image(std::size_t image_index) const {
  try {
    if (image_index >= impl_->document.images().size()) {
      Error error = make_error(ErrorCode::invalid_xisf,
                               "Image index is outside the document");
      error.image_index = image_index;
      return error;
    }
    const auto &image = impl_->document.images()[image_index];
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
    if (image.geometry.size() < 2 || image.geometry.size() > 3) {
      return make_error(ErrorCode::unsupported_feature,
                        "M1 PoC only reads 2-D images with optional channels");
    }
    const auto sample_size = bytes_per_sample(image.sample_format);
    if (!sample_size) {
      return make_error(ErrorCode::unsupported_feature,
                        "M1 PoC supports UInt8, UInt16, and Float32 samples");
    }
    const std::uint64_t channels =
        image.geometry.size() == 3 ? image.geometry[2] : 1;
    if (channels > impl_->options.max_decoded_channels) {
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
    if (sample_count > impl_->options.max_samples_per_image) {
      return make_error(ErrorCode::resource_limit,
                        "Image exceeds the configured sample-count limit");
    }
    if (expected_bytes > impl_->options.max_decoded_image_bytes) {
      return make_error(ErrorCode::resource_limit,
                        "Image exceeds the configured decoded byte limit");
    }
    if (image.block.size != expected_bytes) {
      return make_error(
          ErrorCode::invalid_block,
          "Attachment size does not match image geometry and sample format");
    }
    if (image.block.offset > impl_->document.file_size() ||
        image.block.size > impl_->document.file_size() - image.block.offset) {
      return make_error(ErrorCode::invalid_block,
                        "Attachment range extends beyond the file");
    }
    std::ifstream input(impl_->path, std::ios::binary);
    if (!input) {
      return make_error(ErrorCode::io_error, "Unable to reopen XISF file");
    }
    input.seekg(static_cast<std::streamoff>(image.block.offset));
    if (!input) {
      return make_error(ErrorCode::io_error,
                        "Unable to seek to image attachment");
    }
    RawImage result;
    result.width = image.geometry[0];
    result.height = image.geometry[1];
    result.channels = channels;
    result.sample_format = image.sample_format;
    result.pixel_storage = image.pixel_storage;
    result.byte_order = image.byte_order;
    result.pixels.resize(static_cast<std::size_t>(expected_bytes));
    input.read(reinterpret_cast<char *>(result.pixels.data()),
               static_cast<std::streamsize>(expected_bytes));
    if (input.gcount() != static_cast<std::streamsize>(expected_bytes)) {
      return make_error(ErrorCode::io_error,
                        "Unable to read complete image attachment");
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
