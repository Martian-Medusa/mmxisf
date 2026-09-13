// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <expat.h>
#include <lz4.h>
#include <openssl/evp.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <locale>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace mmxisf {
namespace {

constexpr std::array<unsigned char, 8> kSignature{'X', 'I', 'S', 'F',
                                                  '0', '1', '0', '0'};
constexpr std::string_view kXisfNamespace = "http://www.pixinsight.com/xisf";
constexpr std::string_view kXmlDeclaration =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";

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

bool is_core_element(std::string_view name) {
  constexpr std::array<std::string_view, 13> kCoreElements{
      "Property",        "Structure",        "Table",      "Metadata",
      "Image",           "FITSKeyword",      "ICCProfile", "RGBWorkingSpace",
      "DisplayFunction", "ColorFilterArray", "Resolution", "Thumbnail",
      "Reference"};
  return std::find(kCoreElements.begin(), kCoreElements.end(), name) !=
         kCoreElements.end();
}

bool is_valid_unique_id(std::string_view value) {
  const auto is_ascii_letter = [](char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z');
  };
  if (value.empty() ||
      (value.front() != '_' && !is_ascii_letter(value.front()))) {
    return false;
  }
  return std::all_of(value.begin() + 1, value.end(), [&](char character) {
    return character == '_' || is_ascii_letter(character) ||
           (character >= '0' && character <= '9');
  });
}

bool contains_non_xml_whitespace(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char character) {
    return character != ' ' && character != '\t' && character != '\r' &&
           character != '\n';
  });
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
    std::istringstream input{std::string(token)};
    input.imbue(std::locale::classic());
    input >> std::noskipws >> value;
    if (input.fail()) {
      return false;
    }
    return input.peek() == std::char_traits<char>::eof() &&
           std::isfinite(value);
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

bool checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t &result) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
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

struct AttachedRange {
  std::uint64_t offset{0};
  std::uint64_t size{0};
};

struct XmlBuilder {
  enum class EmbeddedEncoding { none, base64, hex };

  std::string version;
  std::vector<ImageInfo> images;
  std::vector<std::vector<std::byte>> embedded_blocks;
  std::vector<bool> embedded_data_seen;
  std::vector<AttachedRange> attached_ranges;
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
  std::unordered_set<std::string> core_uids;
  std::vector<std::string> references;
  std::optional<std::size_t> embedded_image_index;
  EmbeddedEncoding embedded_encoding{EmbeddedEncoding::none};
  std::array<unsigned char, 4> base64_quartet{};
  std::size_t base64_quartet_size{0};
  bool base64_complete{false};
  std::optional<unsigned char> hex_high_nibble;
  std::size_t encoded_block_bytes{0};

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

std::optional<unsigned char> base64_value(char character) {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<unsigned char>(character - 'A');
  }
  if (character >= 'a' && character <= 'z') {
    return static_cast<unsigned char>(character - 'a' + 26);
  }
  if (character >= '0' && character <= '9') {
    return static_cast<unsigned char>(character - '0' + 52);
  }
  if (character == '+') {
    return 62;
  }
  if (character == '/') {
    return 63;
  }
  return std::nullopt;
}

bool append_embedded_byte(XmlBuilder &state, unsigned char value) {
  auto &output = state.embedded_blocks[*state.embedded_image_index];
  if (output.size() >= state.options.max_decoded_image_bytes) {
    state.fail(ErrorCode::resource_limit,
               "Embedded block exceeds the decoded byte limit", "Data");
    return false;
  }
  try {
    output.push_back(static_cast<std::byte>(value));
  } catch (const std::bad_alloc &) {
    state.fail(ErrorCode::resource_limit,
               "Memory allocation failed for embedded block", "Data");
    return false;
  }
  return true;
}

bool decode_base64_quartet(XmlBuilder &state) {
  const auto &q = state.base64_quartet;
  if (q[0] == 64 || q[1] == 64) {
    state.fail(ErrorCode::invalid_xisf, "Invalid Base64 padding", "Data");
    return false;
  }
  if (!append_embedded_byte(
          state, static_cast<unsigned char>((q[0] << 2U) | (q[1] >> 4U)))) {
    return false;
  }
  if (q[2] == 64) {
    if (q[3] != 64 || (q[1] & 0x0fU) != 0) {
      state.fail(ErrorCode::invalid_xisf,
                 "Invalid or noncanonical Base64 padding", "Data");
      return false;
    }
    state.base64_complete = true;
    return true;
  }
  if (!append_embedded_byte(
          state, static_cast<unsigned char>((q[1] << 4U) | (q[2] >> 2U)))) {
    return false;
  }
  if (q[3] == 64) {
    if ((q[2] & 0x03U) != 0) {
      state.fail(ErrorCode::invalid_xisf,
                 "Invalid or noncanonical Base64 padding", "Data");
      return false;
    }
    state.base64_complete = true;
    return true;
  }
  return append_embedded_byte(state,
                              static_cast<unsigned char>((q[2] << 6U) | q[3]));
}

void decode_embedded_text(XmlBuilder &state, std::string_view text) {
  for (const char character : text) {
    if (character == ' ' || character == '\t' || character == '\r' ||
        character == '\n') {
      continue;
    }
    if (state.encoded_block_bytes >= state.options.max_encoded_block_bytes) {
      state.fail(ErrorCode::resource_limit,
                 "Embedded block exceeds the encoded byte limit", "Data");
      return;
    }
    ++state.encoded_block_bytes;
    if (state.embedded_encoding == XmlBuilder::EmbeddedEncoding::hex) {
      unsigned char nibble = 0;
      if (character >= '0' && character <= '9') {
        nibble = static_cast<unsigned char>(character - '0');
      } else if (character >= 'a' && character <= 'f') {
        nibble = static_cast<unsigned char>(character - 'a' + 10);
      } else {
        state.fail(ErrorCode::invalid_xisf,
                   "Embedded Base16 data must use lowercase hexadecimal",
                   "Data");
        return;
      }
      if (!state.hex_high_nibble) {
        state.hex_high_nibble = nibble;
      } else {
        if (!append_embedded_byte(
                state, static_cast<unsigned char>(
                           (*state.hex_high_nibble << 4U) | nibble))) {
          return;
        }
        state.hex_high_nibble.reset();
      }
      continue;
    }
    if (state.base64_complete) {
      state.fail(ErrorCode::invalid_xisf, "Base64 data continues after padding",
                 "Data");
      return;
    }
    if (character == '=') {
      state.base64_quartet[state.base64_quartet_size++] = 64;
    } else {
      const auto value = base64_value(character);
      if (!value) {
        state.fail(ErrorCode::invalid_xisf,
                   "Embedded block contains an invalid Base64 character",
                   "Data");
        return;
      }
      state.base64_quartet[state.base64_quartet_size++] = *value;
    }
    if (state.base64_quartet_size == state.base64_quartet.size()) {
      if (!decode_base64_quartet(state)) {
        return;
      }
      state.base64_quartet_size = 0;
    }
  }
}

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

  const auto generic_location = attribute(attributes, "location");
  if (generic_location && generic_location->starts_with("attachment:")) {
    const auto block = parse_location(*generic_location);
    if (block.kind == BlockKind::attachment) {
      state.attached_ranges.push_back(AttachedRange{block.offset, block.size});
    } else if (is_xisf_element) {
      state.fail(ErrorCode::invalid_xisf,
                 "Invalid attached data block location", name, "location");
      return;
    }
  }

  if (state.embedded_image_index) {
    state.fail(ErrorCode::invalid_xisf,
               "Embedded Data cannot contain child elements", name);
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

  if (is_xisf_element && is_core_element(name)) {
    const auto uid = attribute(attributes, "uid");
    if (name == "Reference") {
      const auto reference = attribute(attributes, "ref");
      if (uid || !reference || !is_valid_unique_id(*reference)) {
        state.fail(ErrorCode::invalid_xisf,
                   "Reference requires a valid ref and cannot define uid", name,
                   uid ? "uid" : "ref");
        return;
      }
      state.references.emplace_back(*reference);
    } else if (uid) {
      if (!is_valid_unique_id(*uid)) {
        state.fail(ErrorCode::invalid_xisf,
                   "Core element uid has invalid syntax", name, "uid");
        return;
      }
      if (!state.core_uids.emplace(*uid).second) {
        state.fail(ErrorCode::invalid_xisf,
                   "Core element uid must be unique in the XISF unit", name,
                   "uid");
        return;
      }
    }
  }

  if (is_xisf_element && name == "Data") {
    const auto image_index = state.current_image();
    if (state.depth != 3 || parent != "Image" || !image_index ||
        state.images[*image_index].block.kind != BlockKind::embedded) {
      state.fail(ErrorCode::invalid_xisf,
                 "Data must be a direct child of an embedded Image", name);
      return;
    }
    if (state.embedded_data_seen[*image_index]) {
      state.fail(ErrorCode::invalid_xisf,
                 "Embedded Image must contain exactly one Data element", name);
      return;
    }
    const auto encoding = attribute(attributes, "encoding");
    if (!encoding || (*encoding != "base64" && *encoding != "hex")) {
      state.fail(ErrorCode::invalid_xisf,
                 "Data requires a base64 or hex encoding", name, "encoding");
      return;
    }
    auto &image = state.images[*image_index];
    if (!image.compression.empty() || !image.subblocks.empty() ||
        !image.checksum.empty()) {
      state.fail(ErrorCode::invalid_xisf,
                 "Embedded block attributes must be defined on Data", name);
      return;
    }
    image.compression =
        std::string(attribute(attributes, "compression").value_or(""));
    image.subblocks =
        std::string(attribute(attributes, "subblocks").value_or(""));
    image.checksum =
        std::string(attribute(attributes, "checksum").value_or(""));
    state.embedded_data_seen[*image_index] = true;
    state.embedded_image_index = *image_index;
    state.embedded_encoding = *encoding == "base64"
                                  ? XmlBuilder::EmbeddedEncoding::base64
                                  : XmlBuilder::EmbeddedEncoding::hex;
    state.base64_quartet_size = 0;
    state.base64_complete = false;
    state.hex_high_nibble.reset();
    state.encoded_block_bytes = 0;
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
    } else if (*sample_format == "UInt32") {
      image.sample_format = SampleFormat::uint32;
    } else if (*sample_format == "UInt64") {
      image.sample_format = SampleFormat::uint64;
    } else if (*sample_format == "Float32") {
      image.sample_format = SampleFormat::float32;
    } else if (*sample_format == "Float64") {
      image.sample_format = SampleFormat::float64;
    } else if (*sample_format == "Complex32") {
      image.sample_format = SampleFormat::complex32;
    } else if (*sample_format == "Complex64") {
      image.sample_format = SampleFormat::complex64;
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
    const auto channel_count = image.geometry.back();
    if ((image.color_space == "RGB" || image.color_space == "CIELab") &&
        channel_count < 3) {
      state.fail(ErrorCode::invalid_xisf,
                 "Color Image requires at least three nominal channels", name,
                 "geometry");
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
    image.subblocks =
        std::string(attribute(attributes, "subblocks").value_or(""));
    image.checksum =
        std::string(attribute(attributes, "checksum").value_or(""));
    state.images.push_back(std::move(image));
    state.embedded_blocks.emplace_back();
    state.embedded_data_seen.push_back(false);
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
    if (value && value->size() > state.options.max_metadata_value_bytes) {
      state.fail(ErrorCode::resource_limit,
                 "Metadata value exceeds the inspection limit", name, "value");
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
    entry.scope = entry.image_index
                      ? MetadataEntry::Scope::image
                      : parent == "Metadata"
                            ? MetadataEntry::Scope::xisf_unit
                            : MetadataEntry::Scope::standalone;
    entry.uid = std::string(attribute(attributes, "uid").value_or(""));
    entry.name = std::string(*identity);
    entry.type = std::string(attribute(attributes, "type").value_or(""));
    entry.value = std::string(attribute(attributes, "value").value_or(""));
    entry.comment = std::string(attribute(attributes, "comment").value_or(""));
    entry.format = std::string(attribute(attributes, "format").value_or(""));
    const auto location = attribute(attributes, "location");
    if (location) {
      entry.block = parse_location(*location);
      if (entry.block.kind == BlockKind::unknown) {
        state.fail(ErrorCode::invalid_xisf,
                   "Property has an invalid data block location", name,
                   "location");
        return;
      }
      entry.value_form = MetadataEntry::ValueForm::data_block;
    } else if (name == "Property" && !value) {
      entry.value_form = MetadataEntry::ValueForm::character_data;
    }
    const auto parse_extent = [&](std::string_view attribute_name,
                                  std::optional<std::uint64_t> &target) {
      const auto serialized = attribute(attributes, attribute_name);
      if (!serialized) {
        return true;
      }
      std::uint64_t parsed = 0;
      if (!parse_unsigned(*serialized, parsed)) {
        state.fail(ErrorCode::invalid_xisf,
                   "Property extent must be an unsigned integer", name,
                   std::string(attribute_name));
        return false;
      }
      target = parsed;
      return true;
    };
    if (!parse_extent("length", entry.length) ||
        !parse_extent("rows", entry.rows) ||
        !parse_extent("columns", entry.columns)) {
      return;
    }
    state.metadata.push_back(std::move(entry));
    if (name == "Property" && !value && !location) {
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
  if (is_xisf_element && name == "Data") {
    if (!state.embedded_image_index) {
      state.fail(ErrorCode::invalid_xisf, "Unexpected closing Data element",
                 name);
      return;
    }
    if ((state.embedded_encoding == XmlBuilder::EmbeddedEncoding::base64 &&
         state.base64_quartet_size != 0) ||
        (state.embedded_encoding == XmlBuilder::EmbeddedEncoding::hex &&
         state.hex_high_nibble)) {
      state.fail(ErrorCode::invalid_xisf,
                 "Embedded block has an incomplete encoded byte sequence",
                 name);
      return;
    }
    state.embedded_image_index.reset();
    state.embedded_encoding = XmlBuilder::EmbeddedEncoding::none;
  } else if (is_xisf_element && name == "Property") {
    state.text_metadata_index.reset();
  } else if (is_xisf_element && name == "Image") {
    if (!state.image_stack.empty()) {
      const auto image_index = state.image_stack.back();
      if (state.images[image_index].block.kind == BlockKind::embedded &&
          !state.embedded_data_seen[image_index]) {
        state.fail(ErrorCode::invalid_xisf,
                   "Embedded Image requires exactly one Data child", name);
        return;
      }
    }
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
  if (state.error || length <= 0) {
    return;
  }
  const std::string_view data(text, static_cast<std::size_t>(length));
  if (state.embedded_image_index) {
    decode_embedded_text(state, data);
    return;
  }
  if (!state.text_metadata_index) {
    if (state.depth == 1 && !state.element_stack.empty() &&
        state.element_stack.back() == "xisf" &&
        contains_non_xml_whitespace(data)) {
      state.fail(ErrorCode::invalid_xisf,
                 "The XISF root cannot contain character data", "xisf");
    } else if (state.current_image() &&
               state.images[*state.current_image()].block.kind ==
                   BlockKind::embedded &&
               contains_non_xml_whitespace(data)) {
      state.fail(ErrorCode::invalid_xisf,
                 "Embedded Image cannot contain text outside Data", "Image");
    }
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

struct ParsedHeader {
  Document document;
  std::vector<std::vector<std::byte>> embedded_blocks;
  std::vector<AttachedRange> attached_ranges;
};

Result<ParsedHeader> parse_header(std::string_view xml,
                                  const ReaderOptions &options,
                                  std::uint64_t file_size,
                                  std::uint32_t header_length) {
  if (!xml.starts_with(kXmlDeclaration)) {
    return make_error(
        ErrorCode::invalid_xisf,
        "XISF header must begin with the XML 1.0 UTF-8 declaration");
  }
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
  for (const auto &reference : state.references) {
    if (!state.core_uids.contains(reference)) {
      return make_error(ErrorCode::invalid_xisf,
                        "Reference points to an undefined core element uid");
    }
  }
  const auto header_end = 16ULL + static_cast<std::uint64_t>(header_length);
  std::sort(state.attached_ranges.begin(), state.attached_ranges.end(),
            [](const AttachedRange &left, const AttachedRange &right) {
              return left.offset < right.offset;
            });
  std::uint64_t previous_end = header_end;
  for (const auto &range : state.attached_ranges) {
    if (range.size == 0 || range.offset < header_end ||
        range.offset > file_size || range.size > file_size - range.offset) {
      return make_error(ErrorCode::invalid_block,
                        "Attachment range is outside the file payload");
    }
    if (range.offset < previous_end) {
      return make_error(ErrorCode::invalid_block,
                        "Attached data blocks overlap");
    }
    previous_end = range.offset + range.size;
  }
  Document document(std::move(state.version), std::move(state.images),
                    std::move(state.metadata), file_size, header_length);
  return ParsedHeader{std::move(document), std::move(state.embedded_blocks),
                      std::move(state.attached_ranges)};
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
  case SampleFormat::uint32:
    return 4;
  case SampleFormat::float32:
    return 4;
  case SampleFormat::float64:
    return 8;
  case SampleFormat::uint64:
  case SampleFormat::complex32:
  case SampleFormat::complex64:
  case SampleFormat::unsupported:
    return std::nullopt;
  }
  return std::nullopt;
}

enum class CompressionCodec { none, zlib, lz4, lz4hc };

struct CompressionSubblock {
  std::uint64_t compressed_size{0};
  std::uint64_t uncompressed_size{0};
};

struct CompressionPlan {
  CompressionCodec codec{CompressionCodec::none};
  bool byte_shuffled{false};
  std::uint64_t uncompressed_size{0};
  std::uint64_t item_size{1};
  std::vector<CompressionSubblock> subblocks;
};

enum class ChecksumAlgorithm { none, sha1, sha256, sha512 };

struct ChecksumPlan {
  ChecksumAlgorithm algorithm{ChecksumAlgorithm::none};
  std::vector<unsigned char> expected_digest;
};

std::optional<unsigned char> lowercase_hex_value(char character) {
  if (character >= '0' && character <= '9') {
    return static_cast<unsigned char>(character - '0');
  }
  if (character >= 'a' && character <= 'f') {
    return static_cast<unsigned char>(character - 'a' + 10);
  }
  return std::nullopt;
}

Result<ChecksumPlan> parse_checksum_plan(const ImageInfo &image) {
  if (image.checksum.empty()) {
    return ChecksumPlan{};
  }
  const auto separator = image.checksum.find(':');
  if (separator == std::string::npos ||
      image.checksum.find(':', separator + 1) != std::string::npos) {
    return make_error(ErrorCode::invalid_block, "Invalid checksum descriptor");
  }
  const auto algorithm = std::string_view(image.checksum).substr(0, separator);
  const auto encoded_digest =
      std::string_view(image.checksum).substr(separator + 1);
  ChecksumPlan plan;
  std::size_t digest_size = 0;
  if (algorithm == "sha-1" || algorithm == "sha1") {
    plan.algorithm = ChecksumAlgorithm::sha1;
    digest_size = 20;
  } else if (algorithm == "sha-256" || algorithm == "sha256") {
    plan.algorithm = ChecksumAlgorithm::sha256;
    digest_size = 32;
  } else if (algorithm == "sha-512" || algorithm == "sha512") {
    plan.algorithm = ChecksumAlgorithm::sha512;
    digest_size = 64;
  } else if (algorithm == "sha3-256" || algorithm == "sha3-512") {
    return make_error(ErrorCode::unsupported_feature,
                      "SHA-3 checksums are inspect-only in this profile");
  } else {
    return make_error(ErrorCode::unsupported_feature,
                      "Unsupported image checksum algorithm");
  }
  if (encoded_digest.size() != digest_size * 2) {
    return make_error(ErrorCode::invalid_block,
                      "Checksum digest has an invalid length");
  }
  plan.expected_digest.reserve(digest_size);
  for (std::size_t index = 0; index < encoded_digest.size(); index += 2) {
    const auto high = lowercase_hex_value(encoded_digest[index]);
    const auto low = lowercase_hex_value(encoded_digest[index + 1]);
    if (!high || !low) {
      return make_error(
          ErrorCode::invalid_block,
          "Checksum digest must use lowercase hexadecimal digits");
    }
    plan.expected_digest.push_back(
        static_cast<unsigned char>((*high << 4U) | *low));
  }
  return plan;
}

Result<CompressionPlan> parse_compression_plan(const ImageInfo &image,
                                               std::uint64_t serialized_bytes,
                                               std::uint64_t expected_bytes,
                                               const ReaderOptions &options,
                                               std::uint64_t sample_size) {
  if (serialized_bytes > options.max_serialized_image_bytes) {
    return make_error(ErrorCode::resource_limit,
                      "Serialized image block exceeds the configured limit");
  }
  if (serialized_bytes > std::numeric_limits<std::size_t>::max()) {
    return make_error(
        ErrorCode::resource_limit,
        "Serialized image block cannot fit in addressable memory");
  }
  if (image.compression.empty()) {
    if (!image.subblocks.empty()) {
      return make_error(ErrorCode::invalid_block,
                        "Compression subblocks require a compression codec");
    }
    if (serialized_bytes != expected_bytes) {
      return make_error(
          ErrorCode::invalid_block,
          "Image block size does not match geometry and sample format");
    }
    CompressionPlan plan;
    plan.uncompressed_size = expected_bytes;
    return plan;
  }

  std::array<std::string_view, 3> tokens{};
  std::size_t token_count = 0;
  std::size_t start = 0;
  while (start <= image.compression.size()) {
    if (token_count == tokens.size()) {
      return make_error(ErrorCode::invalid_block,
                        "Compression descriptor has too many components");
    }
    const auto end = image.compression.find(':', start);
    tokens[token_count++] =
        std::string_view(image.compression)
            .substr(start, end == std::string::npos
                               ? image.compression.size() - start
                               : end - start);
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  CompressionPlan plan;
  if (tokens[0] == "zlib") {
    plan.codec = CompressionCodec::zlib;
  } else if (tokens[0] == "zlib+sh") {
    plan.codec = CompressionCodec::zlib;
    plan.byte_shuffled = true;
  } else if (tokens[0] == "lz4") {
    plan.codec = CompressionCodec::lz4;
  } else if (tokens[0] == "lz4+sh") {
    plan.codec = CompressionCodec::lz4;
    plan.byte_shuffled = true;
  } else if (tokens[0] == "lz4hc") {
    plan.codec = CompressionCodec::lz4hc;
  } else if (tokens[0] == "lz4hc+sh") {
    plan.codec = CompressionCodec::lz4hc;
    plan.byte_shuffled = true;
  } else {
    return make_error(ErrorCode::unsupported_feature,
                      "Unsupported image compression codec");
  }
  const std::size_t expected_tokens = plan.byte_shuffled ? 3 : 2;
  if (token_count != expected_tokens ||
      !parse_unsigned(tokens[1], plan.uncompressed_size) ||
      plan.uncompressed_size == 0) {
    return make_error(ErrorCode::invalid_block,
                      "Invalid compression descriptor");
  }
  if (plan.uncompressed_size != expected_bytes) {
    return make_error(
        ErrorCode::invalid_block,
        "Declared uncompressed size does not match image geometry");
  }
  if (plan.byte_shuffled &&
      (!parse_unsigned(tokens[2], plan.item_size) || plan.item_size == 0 ||
       plan.item_size != sample_size)) {
    return make_error(
        ErrorCode::invalid_block,
        "Byte-shuffle item size must match the image sample size");
  }
  if (serialized_bytes == 0) {
    return make_error(ErrorCode::invalid_block,
                      "Compressed image block cannot be empty");
  }
  std::uint64_t maximum_output = 0;
  if (!checked_multiply(serialized_bytes, options.max_decompression_ratio,
                        maximum_output)) {
    maximum_output = std::numeric_limits<std::uint64_t>::max();
  }
  if (expected_bytes > maximum_output) {
    return make_error(ErrorCode::resource_limit,
                      "Image exceeds the configured decompression ratio");
  }

  if (image.subblocks.empty()) {
    plan.subblocks.push_back(
        CompressionSubblock{serialized_bytes, expected_bytes});
    return plan;
  }

  std::uint64_t total_compressed = 0;
  std::uint64_t total_uncompressed = 0;
  start = 0;
  while (start <= image.subblocks.size()) {
    if (plan.subblocks.size() >= options.max_compressed_subblocks) {
      return make_error(
          ErrorCode::resource_limit,
          "Compressed subblock count exceeds the configured limit");
    }
    const auto end = image.subblocks.find(':', start);
    const auto pair = std::string_view(image.subblocks)
                          .substr(start, end == std::string::npos
                                             ? image.subblocks.size() - start
                                             : end - start);
    const auto comma = pair.find(',');
    CompressionSubblock subblock;
    if (comma == std::string_view::npos ||
        pair.find(',', comma + 1) != std::string_view::npos ||
        !parse_unsigned(pair.substr(0, comma), subblock.compressed_size) ||
        !parse_unsigned(pair.substr(comma + 1), subblock.uncompressed_size) ||
        subblock.compressed_size == 0 || subblock.uncompressed_size == 0 ||
        (plan.byte_shuffled &&
         subblock.uncompressed_size % plan.item_size != 0)) {
      return make_error(ErrorCode::invalid_block,
                        "Invalid compression subblock descriptor");
    }
    std::uint64_t next_compressed = 0;
    std::uint64_t next_uncompressed = 0;
    if (!checked_add(total_compressed, subblock.compressed_size,
                     next_compressed) ||
        !checked_add(total_uncompressed, subblock.uncompressed_size,
                     next_uncompressed)) {
      return make_error(ErrorCode::overflow,
                        "Compression subblock sizes overflow");
    }
    total_compressed = next_compressed;
    total_uncompressed = next_uncompressed;
    plan.subblocks.push_back(subblock);
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  if (total_compressed != serialized_bytes ||
      total_uncompressed != expected_bytes) {
    return make_error(ErrorCode::invalid_block,
                      "Compression subblock totals do not match the block");
  }
  return plan;
}

struct ImageReadPlan {
  const ImageInfo *image{nullptr};
  const std::vector<std::byte> *embedded_block{nullptr};
  std::uint64_t channels{0};
  std::uint64_t sample_count{0};
  std::uint64_t sample_size{0};
  std::uint64_t expected_bytes{0};
  std::uint64_t serialized_bytes{0};
  CompressionPlan compression;
  ChecksumPlan checksum;
};

Result<ImageReadPlan>
plan_image_read(const Document &document, const ReaderOptions &options,
                const std::vector<std::vector<std::byte>> &embedded_blocks,
                std::size_t image_index) {
  if (image_index >= document.images().size()) {
    Error error = make_error(ErrorCode::invalid_argument,
                             "Image index is outside the document");
    error.image_index = image_index;
    return error;
  }
  const auto &image = document.images()[image_index];
  if (image.block.kind != BlockKind::attachment &&
      image.block.kind != BlockKind::embedded) {
    return make_error(ErrorCode::unsupported_feature,
                      "The M2 reader only reads attachment and embedded image "
                      "blocks");
  }
  if (image.geometry.size() != 3) {
    return make_error(ErrorCode::unsupported_feature,
                      "The PFI reader profile only reads 2-D images");
  }
  if (image.color_space == "CIELab") {
    return make_error(ErrorCode::unsupported_feature,
                      "CIELab conversion is outside the M2 reader profile");
  }
  const auto sample_size = bytes_per_sample(image.sample_format);
  if (!sample_size) {
    return make_error(ErrorCode::unsupported_feature,
                      "The M2 reader profile supports UInt8, UInt16, UInt32, "
                      "Float32, and Float64 samples");
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
  const std::vector<std::byte> *embedded_block = nullptr;
  const std::uint64_t serialized_bytes =
      image.block.kind == BlockKind::attachment
          ? image.block.size
          : static_cast<std::uint64_t>(embedded_blocks[image_index].size());
  auto compression = parse_compression_plan(
      image, serialized_bytes, expected_bytes, options, *sample_size);
  if (!compression) {
    return compression.error();
  }
  auto checksum = parse_checksum_plan(image);
  if (!checksum) {
    return checksum.error();
  }
  if (image.block.kind == BlockKind::attachment &&
      (image.block.offset > document.file_size() ||
       image.block.size > document.file_size() - image.block.offset)) {
    return make_error(ErrorCode::invalid_block,
                      "Attachment range extends beyond the source");
  }
  if (image.block.kind == BlockKind::embedded) {
    embedded_block = &embedded_blocks[image_index];
  }
  return ImageReadPlan{&image,
                       embedded_block,
                       channels,
                       sample_count,
                       *sample_size,
                       expected_bytes,
                       serialized_bytes,
                       std::move(compression).value(),
                       std::move(checksum).value()};
}

Result<PixelStorage>
resolve_pixel_storage(const ImageReadPlan &plan,
                      PixelStorageOutput requested_storage) {
  switch (requested_storage) {
  case PixelStorageOutput::source:
    return plan.image->pixel_storage;
  case PixelStorageOutput::planar:
    return PixelStorage::planar;
  case PixelStorageOutput::normal:
    return PixelStorage::normal;
  }
  return make_error(ErrorCode::invalid_argument,
                    "Invalid output pixel-storage option");
}

Result<ByteOrder> resolve_byte_order(const ImageReadPlan &plan,
                                     ByteOrderOutput requested_order) {
  switch (requested_order) {
  case ByteOrderOutput::source:
    return plan.image->byte_order;
  case ByteOrderOutput::native:
    if constexpr (std::endian::native == std::endian::little) {
      return ByteOrder::little;
    } else if constexpr (std::endian::native == std::endian::big) {
      return ByteOrder::big;
    } else {
      return make_error(ErrorCode::unsupported_feature,
                        "Mixed-endian hosts are not supported");
    }
  }
  return make_error(ErrorCode::invalid_argument,
                    "Invalid output byte-order option");
}

Result<std::size_t> read_serialized_chunk(const ByteSource &source,
                                          const ImageReadPlan &plan,
                                          std::size_t block_offset,
                                          std::span<std::byte> destination,
                                          std::stop_token stop_token,
                                          std::size_t image_index) {
  if (plan.embedded_block != nullptr) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    std::copy_n(plan.embedded_block->data() + block_offset, destination.size(),
                destination.data());
    return destination.size();
  }
  std::size_t total = 0;
  while (total < destination.size()) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    const auto output = destination.subspan(total);
    auto read =
        source.read_at(plan.image->block.offset + block_offset + total, output);
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
  }
  return total;
}

Result<std::size_t> copy_serialized_image(const ByteSource &source,
                                          const ImageReadPlan &plan,
                                          std::span<std::byte> destination,
                                          std::stop_token stop_token,
                                          std::size_t image_index) {
  constexpr std::size_t kReadChunkBytes = 8U * 1024U * 1024U;
  const auto expected = static_cast<std::size_t>(plan.serialized_bytes);
  std::size_t total = 0;
  while (total < expected) {
    const auto chunk = std::min(kReadChunkBytes, expected - total);
    auto read = read_serialized_chunk(source, plan, total,
                                      destination.subspan(total, chunk),
                                      stop_token, image_index);
    if (!read) {
      return read.error();
    }
    total += read.value();
  }
  return total;
}

Result<std::size_t> decompress_zlib(std::span<const std::byte> input,
                                    std::span<std::byte> output) {
  if (input.size() > std::numeric_limits<uInt>::max() ||
      output.size() > std::numeric_limits<uInt>::max()) {
    return make_error(ErrorCode::resource_limit,
                      "Zlib subblock exceeds the codec size limit");
  }
  z_stream stream{};
  if (inflateInit(&stream) != Z_OK) {
    return make_error(ErrorCode::internal_error,
                      "Unable to initialize the zlib decoder");
  }
  stream.next_in =
      reinterpret_cast<Bytef *>(const_cast<std::byte *>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  stream.next_out = reinterpret_cast<Bytef *>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());
  const auto status = inflate(&stream, Z_FINISH);
  const auto consumed = static_cast<std::size_t>(stream.total_in);
  const auto produced = static_cast<std::size_t>(stream.total_out);
  inflateEnd(&stream);
  if (status != Z_STREAM_END || consumed != input.size() ||
      produced != output.size()) {
    return make_error(ErrorCode::invalid_block,
                      "Invalid zlib stream or decompressed size mismatch");
  }
  return produced;
}

Result<std::size_t> decompress_lz4(std::span<const std::byte> input,
                                   std::span<std::byte> output) {
  if (input.size() >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      output.size() >
          static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return make_error(ErrorCode::resource_limit,
                      "LZ4 subblock exceeds the codec size limit");
  }
  const auto produced = LZ4_decompress_safe(
      reinterpret_cast<const char *>(input.data()),
      reinterpret_cast<char *>(output.data()), static_cast<int>(input.size()),
      static_cast<int>(output.size()));
  if (produced < 0 || static_cast<std::size_t>(produced) != output.size()) {
    return make_error(ErrorCode::invalid_block,
                      "Invalid LZ4 block or decompressed size mismatch");
  }
  return static_cast<std::size_t>(produced);
}

Result<std::size_t> decompress_subblock(CompressionCodec codec,
                                        std::span<const std::byte> input,
                                        std::span<std::byte> output) {
  switch (codec) {
  case CompressionCodec::zlib:
    return decompress_zlib(input, output);
  case CompressionCodec::lz4:
  case CompressionCodec::lz4hc:
    return decompress_lz4(input, output);
  case CompressionCodec::none:
    return make_error(ErrorCode::internal_error,
                      "Missing codec for compressed image block");
  }
  return make_error(ErrorCode::internal_error,
                    "Invalid compression codec state");
}

const EVP_MD *checksum_digest(ChecksumAlgorithm algorithm) {
  switch (algorithm) {
  case ChecksumAlgorithm::sha1:
    return EVP_sha1();
  case ChecksumAlgorithm::sha256:
    return EVP_sha256();
  case ChecksumAlgorithm::sha512:
    return EVP_sha512();
  case ChecksumAlgorithm::none:
    return nullptr;
  }
  return nullptr;
}

Result<bool> verify_checksum(const ChecksumPlan &plan,
                             std::span<const std::byte> serialized) {
  if (plan.algorithm == ChecksumAlgorithm::none) {
    return true;
  }
  const auto *digest = checksum_digest(plan.algorithm);
  if (digest == nullptr) {
    return make_error(ErrorCode::internal_error,
                      "Unable to resolve checksum implementation");
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> actual{};
  unsigned int actual_size = 0;
  if (EVP_Digest(serialized.data(), serialized.size(), actual.data(),
                 &actual_size, digest, nullptr) != 1) {
    return make_error(ErrorCode::internal_error, "Checksum computation failed");
  }
  if (actual_size != plan.expected_digest.size() ||
      !std::equal(actual.begin(), actual.begin() + actual_size,
                  plan.expected_digest.begin())) {
    return make_error(ErrorCode::checksum_mismatch,
                      "Image block checksum verification failed");
  }
  return true;
}

Result<std::size_t> unshuffle_bytes(std::span<const std::byte> shuffled,
                                    std::span<std::byte> output,
                                    std::size_t item_size,
                                    std::stop_token stop_token) {
  if (item_size == 0 || shuffled.size() != output.size() ||
      shuffled.size() % item_size != 0) {
    return make_error(ErrorCode::invalid_block,
                      "Invalid byte-shuffled block geometry");
  }
  const auto item_count = shuffled.size() / item_size;
  constexpr std::size_t kCancellationInterval = 1U << 20U;
  for (std::size_t item = 0; item < item_count; ++item) {
    if (item % kCancellationInterval == 0 && stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    for (std::size_t byte = 0; byte < item_size; ++byte) {
      output[item * item_size + byte] = shuffled[byte * item_count + item];
    }
  }
  return output.size();
}

Result<std::size_t> decode_compressed_image(
    std::span<const std::byte> serialized, const ImageReadPlan &plan,
    std::span<std::byte> destination, std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    return make_error(ErrorCode::cancelled, "Image read was cancelled");
  }
  std::size_t input_offset = 0;
  std::size_t output_offset = 0;
  std::vector<std::byte> shuffled;
  for (const auto &subblock : plan.compression.subblocks) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    const auto compressed_size =
        static_cast<std::size_t>(subblock.compressed_size);
    const auto uncompressed_size =
        static_cast<std::size_t>(subblock.uncompressed_size);
    const auto input = std::span<const std::byte>(serialized)
                           .subspan(input_offset, compressed_size);
    auto output = destination.subspan(output_offset, uncompressed_size);
    if (plan.compression.byte_shuffled) {
      shuffled.resize(uncompressed_size);
      auto decoded =
          decompress_subblock(plan.compression.codec, input, shuffled);
      if (!decoded) {
        return decoded.error();
      }
      auto unshuffled = unshuffle_bytes(
          shuffled, output,
          static_cast<std::size_t>(plan.compression.item_size), stop_token);
      if (!unshuffled) {
        return unshuffled.error();
      }
    } else {
      auto decoded = decompress_subblock(plan.compression.codec, input, output);
      if (!decoded) {
        return decoded.error();
      }
    }
    input_offset += compressed_size;
    output_offset += uncompressed_size;
  }
  return output_offset;
}

Result<std::size_t> transform_pixel_storage_from_buffer(
    const ImageReadPlan &plan, std::span<const std::byte> source,
    std::span<std::byte> destination, PixelStorage output_storage,
    ByteOrder output_byte_order, std::stop_token stop_token) {
  const auto sample_size = static_cast<std::size_t>(plan.sample_size);
  const auto pixel_count = plan.sample_count / plan.channels;
  constexpr std::uint64_t kCancellationInterval = 1U << 20U;
  for (std::uint64_t source_index = 0; source_index < plan.sample_count;
       ++source_index) {
    if (source_index % kCancellationInterval == 0 &&
        stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    const auto channel = plan.image->pixel_storage == PixelStorage::planar
                             ? source_index / pixel_count
                             : source_index % plan.channels;
    const auto pixel = plan.image->pixel_storage == PixelStorage::planar
                           ? source_index % pixel_count
                           : source_index / plan.channels;
    const auto output_index = output_storage == PixelStorage::planar
                                  ? channel * pixel_count + pixel
                                  : pixel * plan.channels + channel;
    const auto input_offset =
        static_cast<std::size_t>(source_index) * sample_size;
    const auto output_offset =
        static_cast<std::size_t>(output_index) * sample_size;
    for (std::size_t byte = 0; byte < sample_size; ++byte) {
      const auto input_byte = plan.image->byte_order == output_byte_order
                                  ? byte
                                  : sample_size - byte - 1;
      destination[output_offset + byte] = source[input_offset + input_byte];
    }
  }
  return destination.size();
}

Result<std::size_t> transform_pixel_storage(const ByteSource &source,
                                            const ImageReadPlan &plan,
                                            std::span<std::byte> destination,
                                            PixelStorage output_storage,
                                            ByteOrder output_byte_order,
                                            std::stop_token stop_token,
                                            std::size_t image_index) {
  constexpr std::size_t kReadChunkBytes = 8U * 1024U * 1024U;
  const auto sample_size = static_cast<std::size_t>(plan.sample_size);
  const auto maximum_chunk_samples = kReadChunkBytes / sample_size;
  std::vector<std::byte> staging(
      std::min<std::size_t>(static_cast<std::size_t>(plan.expected_bytes),
                            maximum_chunk_samples * sample_size));
  const auto pixel_count = plan.sample_count / plan.channels;
  std::uint64_t source_sample = 0;
  while (source_sample < plan.sample_count) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    const auto chunk_samples = std::min<std::uint64_t>(
        maximum_chunk_samples, plan.sample_count - source_sample);
    const auto chunk_bytes =
        static_cast<std::size_t>(chunk_samples * plan.sample_size);
    auto read = read_serialized_chunk(
        source, plan,
        static_cast<std::size_t>(source_sample * plan.sample_size),
        std::span(staging).first(chunk_bytes), stop_token, image_index);
    if (!read) {
      return read.error();
    }
    for (std::uint64_t local_sample = 0; local_sample < chunk_samples;
         ++local_sample) {
      const auto source_index = source_sample + local_sample;
      const auto channel = plan.image->pixel_storage == PixelStorage::planar
                               ? source_index / pixel_count
                               : source_index % plan.channels;
      const auto pixel = plan.image->pixel_storage == PixelStorage::planar
                             ? source_index % pixel_count
                             : source_index / plan.channels;
      const auto output_index = output_storage == PixelStorage::planar
                                    ? channel * pixel_count + pixel
                                    : pixel * plan.channels + channel;
      const auto input_offset =
          static_cast<std::size_t>(local_sample * plan.sample_size);
      const auto output_offset =
          static_cast<std::size_t>(output_index * plan.sample_size);
      for (std::size_t byte = 0; byte < sample_size; ++byte) {
        const auto input_byte = plan.image->byte_order == output_byte_order
                                    ? byte
                                    : sample_size - byte - 1;
        destination[output_offset + byte] = staging[input_offset + input_byte];
      }
    }
    source_sample += chunk_samples;
  }
  return static_cast<std::size_t>(plan.expected_bytes);
}

Result<std::size_t> swap_byte_order_in_place(std::span<std::byte> destination,
                                             std::size_t sample_size,
                                             std::stop_token stop_token) {
  constexpr std::size_t kSamplesPerCancellationCheck = 1U << 20U;
  const auto sample_count = destination.size() / sample_size;
  for (std::size_t sample = 0; sample < sample_count; ++sample) {
    if (sample % kSamplesPerCancellationCheck == 0 &&
        stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    const auto begin = destination.begin() + sample * sample_size;
    std::reverse(begin, begin + sample_size);
  }
  return destination.size();
}

Result<bool>
validate_unused_spaces(const ByteSource &source, std::uint64_t header_end,
                       std::uint64_t file_size,
                       const std::vector<AttachedRange> &attached_ranges,
                       const ReaderOptions &options) {
  std::array<std::byte, 64U * 1024U> buffer{};
  std::uint64_t validated_unused_bytes = 0;
  const auto validate_gap = [&](std::uint64_t offset,
                                std::uint64_t size) -> Result<bool> {
    if (size >
        options.max_unused_space_bytes -
            std::min(validated_unused_bytes, options.max_unused_space_bytes)) {
      return make_error(ErrorCode::resource_limit,
                        "Unused-space validation exceeds its byte budget");
    }
    validated_unused_bytes += size;
    std::uint64_t checked = 0;
    while (checked < size) {
      const auto chunk = static_cast<std::size_t>(
          std::min<std::uint64_t>(buffer.size(), size - checked));
      auto read =
          read_exact(source, offset + checked, std::span(buffer).first(chunk));
      if (!read) {
        return read.error();
      }
      if (!std::all_of(buffer.begin(), buffer.begin() + chunk,
                       [](std::byte value) { return value == std::byte{0}; })) {
        return make_error(ErrorCode::invalid_block,
                          "Unused monolithic file space must be zero-filled");
      }
      checked += chunk;
    }
    return true;
  };

  std::uint64_t cursor = header_end;
  for (const auto &range : attached_ranges) {
    if (range.offset > cursor) {
      auto valid = validate_gap(cursor, range.offset - cursor);
      if (!valid) {
        return valid.error();
      }
    }
    cursor = range.offset + range.size;
  }
  if (cursor < file_size) {
    auto valid = validate_gap(cursor, file_size - cursor);
    if (!valid) {
      return valid.error();
    }
  }
  return true;
}

} // namespace

struct Reader::Impl {
  std::shared_ptr<const ByteSource> source;
  ReaderOptions options;
  Document document;
  std::vector<std::vector<std::byte>> embedded_blocks;
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
    auto parsed_header = std::move(parsed).value();
    auto unused_spaces = validate_unused_spaces(
        *source, 16ULL + static_cast<std::uint64_t>(header_length), source_size,
        parsed_header.attached_ranges, options);
    if (!unused_spaces) {
      return unused_spaces.error();
    }
    auto impl = std::make_unique<Impl>();
    impl->source = std::move(source);
    impl->options = options;
    impl->document = std::move(parsed_header.document);
    impl->embedded_blocks = std::move(parsed_header.embedded_blocks);
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
  return read_image(image_index, ImageReadOptions{}, stop_token);
}

Result<RawImage> Reader::read_image(std::size_t image_index,
                                    ImageReadOptions read_options,
                                    std::stop_token stop_token) const {
  try {
    auto plan = plan_image_read(impl_->document, impl_->options,
                                impl_->embedded_blocks, image_index);
    if (!plan) {
      return plan.error();
    }
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    auto output_storage =
        resolve_pixel_storage(plan.value(), read_options.pixel_storage);
    if (!output_storage) {
      return output_storage.error();
    }
    auto output_byte_order =
        resolve_byte_order(plan.value(), read_options.byte_order);
    if (!output_byte_order) {
      return output_byte_order.error();
    }
    RawImage result;
    result.width = plan.value().image->geometry[0];
    result.height = plan.value().image->geometry[1];
    result.channels = plan.value().channels;
    result.sample_format = plan.value().image->sample_format;
    result.lower_bound = plan.value().image->lower_bound;
    result.upper_bound = plan.value().image->upper_bound;
    result.pixel_storage = output_storage.value();
    result.byte_order = output_byte_order.value();
    result.pixels.resize(static_cast<std::size_t>(plan.value().expected_bytes));
    auto read =
        read_image_into(image_index, result.pixels, read_options, stop_token);
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
  return read_image_into(image_index, destination, ImageReadOptions{},
                         stop_token);
}

Result<std::size_t> Reader::read_image_into(std::size_t image_index,
                                            std::span<std::byte> destination,
                                            ImageReadOptions read_options,
                                            std::stop_token stop_token) const {
  try {
    auto plan = plan_image_read(impl_->document, impl_->options,
                                impl_->embedded_blocks, image_index);
    if (!plan) {
      return plan.error();
    }
    const auto expected = static_cast<std::size_t>(plan.value().expected_bytes);
    if (destination.size() < expected) {
      return make_error(ErrorCode::invalid_argument,
                        "Destination buffer is smaller than the image block");
    }
    auto output_storage =
        resolve_pixel_storage(plan.value(), read_options.pixel_storage);
    if (!output_storage) {
      return output_storage.error();
    }
    auto output_byte_order =
        resolve_byte_order(plan.value(), read_options.byte_order);
    if (!output_byte_order) {
      return output_byte_order.error();
    }
    auto output = destination.first(expected);
    const bool needs_serialized_staging =
        plan.value().compression.codec != CompressionCodec::none ||
        plan.value().checksum.algorithm != ChecksumAlgorithm::none;
    if (needs_serialized_staging) {
      std::vector<std::byte> serialized(
          static_cast<std::size_t>(plan.value().serialized_bytes));
      auto copied = copy_serialized_image(*impl_->source, plan.value(),
                                          serialized, stop_token, image_index);
      if (!copied) {
        return copied.error();
      }
      auto verified = verify_checksum(plan.value().checksum, serialized);
      if (!verified) {
        return verified.error();
      }

      if (plan.value().compression.codec != CompressionCodec::none) {
        if (output_storage.value() != plan.value().image->pixel_storage) {
          std::vector<std::byte> source_pixels(expected);
          auto decoded = decode_compressed_image(serialized, plan.value(),
                                                 source_pixels, stop_token);
          if (!decoded) {
            return decoded.error();
          }
          return transform_pixel_storage_from_buffer(
              plan.value(), source_pixels, output, output_storage.value(),
              output_byte_order.value(), stop_token);
        }
        auto decoded = decode_compressed_image(serialized, plan.value(), output,
                                               stop_token);
        if (!decoded) {
          return decoded.error();
        }
      } else if (output_storage.value() != plan.value().image->pixel_storage) {
        return transform_pixel_storage_from_buffer(
            plan.value(), serialized, output, output_storage.value(),
            output_byte_order.value(), stop_token);
      } else {
        std::copy(serialized.begin(), serialized.end(), output.begin());
      }

      if (plan.value().sample_size > 1 &&
          output_byte_order.value() != plan.value().image->byte_order) {
        return swap_byte_order_in_place(
            output, static_cast<std::size_t>(plan.value().sample_size),
            stop_token);
      }
      return expected;
    }
    if (output_storage.value() != plan.value().image->pixel_storage) {
      return transform_pixel_storage(
          *impl_->source, plan.value(), output, output_storage.value(),
          output_byte_order.value(), stop_token, image_index);
    }
    auto copied = copy_serialized_image(*impl_->source, plan.value(), output,
                                        stop_token, image_index);
    if (!copied) {
      return copied.error();
    }
    if (plan.value().sample_size > 1 &&
        output_byte_order.value() != plan.value().image->byte_order) {
      return swap_byte_order_in_place(
          output, static_cast<std::size_t>(plan.value().sample_size),
          stop_token);
    }
    return copied.value();
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while transforming image");
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
  case ErrorCode::checksum_mismatch:
    return "checksum_mismatch";
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
  case SampleFormat::uint32:
    return "UInt32";
  case SampleFormat::uint64:
    return "UInt64";
  case SampleFormat::float32:
    return "Float32";
  case SampleFormat::float64:
    return "Float64";
  case SampleFormat::complex32:
    return "Complex32";
  case SampleFormat::complex64:
    return "Complex64";
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

const char *to_string(MetadataEntry::Scope scope) noexcept {
  switch (scope) {
  case MetadataEntry::Scope::standalone:
    return "Standalone";
  case MetadataEntry::Scope::xisf_unit:
    return "XISF unit";
  case MetadataEntry::Scope::image:
    return "Image";
  }
  return "Standalone";
}

const char *to_string(MetadataEntry::ValueForm value_form) noexcept {
  switch (value_form) {
  case MetadataEntry::ValueForm::attribute:
    return "Attribute";
  case MetadataEntry::ValueForm::character_data:
    return "Character data";
  case MetadataEntry::ValueForm::data_block:
    return "Data block";
  }
  return "Attribute";
}

} // namespace mmxisf
