// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"
#include "property_types.hpp"

#include <expat.h>
#include <lz4.h>
#include <openssl/evp.h>
#include <zlib.h>
#include <zstd.h>

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
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mmxisf {
namespace {

using detail::classify_property_type;
using detail::property_element_layout;
using detail::PropertyCategory;
using detail::PropertyElementLayout;

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

bool is_core_element(std::string_view name) {
  constexpr std::array<std::string_view, 13> kCoreElements{
      "Property",        "Structure",        "Table",      "Metadata",
      "Image",           "FITSKeyword",      "ICCProfile", "RGBWorkingSpace",
      "DisplayFunction", "ColorFilterArray", "Resolution", "Thumbnail",
      "Reference"};
  return std::find(kCoreElements.begin(), kCoreElements.end(), name) !=
         kCoreElements.end();
}

std::optional<AncillaryKind> ancillary_kind(std::string_view name) {
  if (name == "RGBWorkingSpace") {
    return AncillaryKind::rgb_working_space;
  }
  if (name == "DisplayFunction") {
    return AncillaryKind::display_function;
  }
  if (name == "ColorFilterArray") {
    return AncillaryKind::color_filter_array;
  }
  if (name == "Resolution") {
    return AncillaryKind::resolution;
  }
  return std::nullopt;
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

bool is_valid_fits_keyword_name(std::string_view value) {
  return !value.empty() && value.size() <= 8 &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= 'A' && character <= 'Z') ||
                  (character >= '0' && character <= '9') || character == '_' ||
                  character == '-';
         });
}

bool is_valid_property_identifier(std::string_view value) {
  const auto is_start = [](char character) {
    return character == '_' || (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z');
  };
  const auto is_continue = [&](char character) {
    return is_start(character) || (character >= '0' && character <= '9');
  };
  if (value.empty()) {
    return false;
  }
  bool expect_start = true;
  for (const char character : value) {
    if (character == ':') {
      if (expect_start) {
        return false;
      }
      expect_start = true;
      continue;
    }
    if (expect_start ? !is_start(character) : !is_continue(character)) {
      return false;
    }
    expect_start = false;
  }
  return !expect_start;
}

bool contains_non_xml_whitespace(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char character) {
    return character != ' ' && character != '\t' && character != '\r' &&
           character != '\n';
  });
}

std::string_view trim_xml_whitespace(std::string_view text) {
  constexpr std::string_view whitespace = " \t\r\n";
  const auto first = text.find_first_not_of(whitespace);
  if (first == std::string_view::npos) {
    return {};
  }
  const auto last = text.find_last_not_of(whitespace);
  return text.substr(first, last - first + 1);
}

bool is_ascii_digit(char character) {
  return character >= '0' && character <= '9';
}

bool decimal_magnitude_fits(std::string_view digits, std::string_view maximum) {
  return digits.size() < maximum.size() ||
         (digits.size() == maximum.size() && digits <= maximum);
}

std::string_view maximum_decimal_magnitude(unsigned bit_width, bool is_signed,
                                           bool is_negative) {
  if (!is_signed) {
    switch (bit_width) {
    case 8:
      return "255";
    case 16:
      return "65535";
    case 32:
      return "4294967295";
    case 64:
      return "18446744073709551615";
    case 128:
      return "340282366920938463463374607431768211455";
    default:
      return {};
    }
  }
  switch (bit_width) {
  case 8:
    return is_negative ? "128" : "127";
  case 16:
    return is_negative ? "32768" : "32767";
  case 32:
    return is_negative ? "2147483648" : "2147483647";
  case 64:
    return is_negative ? "9223372036854775808" : "9223372036854775807";
  case 128:
    return is_negative ? "170141183460469231731687303715884105728"
                       : "170141183460469231731687303715884105727";
  default:
    return {};
  }
}

bool prefixed_integer_fits(std::string_view digits, unsigned base,
                           unsigned bit_width) {
  const auto first_significant = digits.find_first_not_of('0');
  if (first_significant == std::string_view::npos) {
    return true;
  }
  digits.remove_prefix(first_significant);
  if (base == 2) {
    return digits.size() <= bit_width;
  }
  if (base == 16) {
    return digits.size() <= bit_width / 4;
  }
  if (base != 8) {
    return false;
  }
  const auto maximum_digits = (bit_width + 2) / 3;
  if (digits.size() != maximum_digits) {
    return digits.size() < maximum_digits;
  }
  const auto leading_bits = bit_width % 3;
  if (leading_bits == 0) {
    return true;
  }
  const auto maximum_leading_digit = (1U << leading_bits) - 1U;
  return static_cast<unsigned>(digits.front() - '0') <= maximum_leading_digit;
}

bool is_valid_integer_value(std::string_view text, bool is_signed,
                            unsigned bit_width) {
  text = trim_xml_whitespace(text);
  if (text.empty()) {
    return false;
  }

  if (text.size() >= 2 && text.front() == '0') {
    const char prefix = text[1];
    unsigned base = 0;
    if (prefix == 'b' || prefix == 'B') {
      base = 2;
    } else if (prefix == 'o' || prefix == 'O') {
      base = 8;
    } else if (prefix == 'x' || prefix == 'X') {
      base = 16;
    }
    if (base != 0) {
      text.remove_prefix(2);
      if (text.empty()) {
        return false;
      }
      const bool valid_digits =
          std::all_of(text.begin(), text.end(), [base](char character) {
            if (character >= '0' && character <= '9') {
              return static_cast<unsigned>(character - '0') < base;
            }
            if (base == 16 && character >= 'a' && character <= 'f') {
              return true;
            }
            return base == 16 && character >= 'A' && character <= 'F';
          });
      return valid_digits && prefixed_integer_fits(text, base, bit_width);
    }
  }

  bool is_negative = false;
  if (text.front() == '+' || text.front() == '-') {
    is_negative = text.front() == '-';
    if (!is_signed && is_negative) {
      return false;
    }
    text.remove_prefix(1);
  }
  if (text.empty() || (text.size() > 1 && text.front() == '0')) {
    return false;
  }
  if (!std::all_of(text.begin(), text.end(), is_ascii_digit)) {
    return false;
  }
  const auto maximum =
      maximum_decimal_magnitude(bit_width, is_signed, is_negative);
  return !maximum.empty() && decimal_magnitude_fits(text, maximum);
}

bool is_valid_floating_point_value(std::string_view text) {
  text = trim_xml_whitespace(text);
  if (text == "NaN" || text == "+Inf" || text == "-Inf") {
    return true;
  }
  if (text.empty()) {
    return false;
  }
  if (text.front() == '+' || text.front() == '-') {
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return false;
  }

  std::size_t position = 0;
  while (position < text.size() && is_ascii_digit(text[position])) {
    ++position;
  }
  const bool has_integer_digits = position != 0;
  bool has_fraction_digits = false;
  if (position < text.size() && text[position] == '.') {
    ++position;
    const auto fraction_start = position;
    while (position < text.size() && is_ascii_digit(text[position])) {
      ++position;
    }
    has_fraction_digits = position != fraction_start;
    if (!has_fraction_digits) {
      return false;
    }
  }
  if (!has_integer_digits && !has_fraction_digits) {
    return false;
  }
  if (position < text.size() &&
      (text[position] == 'e' || text[position] == 'E')) {
    ++position;
    if (position < text.size() &&
        (text[position] == '+' || text[position] == '-')) {
      ++position;
    }
    const auto exponent_start = position;
    while (position < text.size() && is_ascii_digit(text[position])) {
      ++position;
    }
    if (position == exponent_start) {
      return false;
    }
  }
  return position == text.size();
}

bool is_valid_complex_value(std::string_view text) {
  text = trim_xml_whitespace(text);
  if (text.size() < 5 || text.front() != '(' || text.back() != ')') {
    return false;
  }
  text.remove_prefix(1);
  text.remove_suffix(1);
  const auto separator = text.find(',');
  return separator != std::string_view::npos &&
         text.find(',', separator + 1) == std::string_view::npos &&
         is_valid_floating_point_value(text.substr(0, separator)) &&
         is_valid_floating_point_value(text.substr(separator + 1));
}

bool is_leap_year(unsigned year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool parse_fixed_digits(std::string_view text, std::size_t position,
                        std::size_t count, unsigned &value) {
  if (position > text.size() || count > text.size() - position) {
    return false;
  }
  value = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const char character = text[position + index];
    if (!is_ascii_digit(character)) {
      return false;
    }
    value = value * 10 + static_cast<unsigned>(character - '0');
  }
  return true;
}

bool is_valid_time_point_value(std::string_view text) {
  text = trim_xml_whitespace(text);
  if (text.size() < 19 || text[4] != '-' || text[7] != '-' ||
      (text[10] != 'T' && text[10] != 't') || text[13] != ':' ||
      text[16] != ':') {
    return false;
  }

  unsigned year = 0;
  unsigned month = 0;
  unsigned day = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  if (!parse_fixed_digits(text, 0, 4, year) ||
      !parse_fixed_digits(text, 5, 2, month) ||
      !parse_fixed_digits(text, 8, 2, day) ||
      !parse_fixed_digits(text, 11, 2, hour) ||
      !parse_fixed_digits(text, 14, 2, minute) ||
      !parse_fixed_digits(text, 17, 2, second)) {
    return false;
  }
  constexpr std::array<unsigned, 12> month_lengths{31, 28, 31, 30, 31, 30,
                                                   31, 31, 30, 31, 30, 31};
  if (month == 0 || month > month_lengths.size()) {
    return false;
  }
  unsigned maximum_day = month_lengths[month - 1];
  if (month == 2 && is_leap_year(year)) {
    maximum_day = 29;
  }
  if (day == 0 || day > maximum_day || hour > 23 || minute > 59 ||
      second > 60) {
    return false;
  }

  std::size_t position = 19;
  if (position < text.size() && text[position] == '.') {
    ++position;
    const auto fraction_start = position;
    while (position < text.size() && is_ascii_digit(text[position])) {
      ++position;
    }
    if (position == fraction_start) {
      return false;
    }
  }
  if (position == text.size()) {
    return true;
  }
  if (text[position] == 'Z' || text[position] == 'z') {
    return position + 1 == text.size();
  }
  if ((text[position] != '+' && text[position] != '-') ||
      text.size() - position != 6 || text[position + 3] != ':') {
    return false;
  }
  unsigned offset_hour = 0;
  unsigned offset_minute = 0;
  return parse_fixed_digits(text, position + 1, 2, offset_hour) &&
         parse_fixed_digits(text, position + 4, 2, offset_minute) &&
         offset_hour <= 23 && offset_minute <= 59;
}

enum class PropertyValueKind {
  boolean,
  signed_integer,
  unsigned_integer,
  floating_point,
  complex,
  other
};

PropertyValueKind classify_property_value_kind(std::string_view type) {
  if (type == "Boolean") {
    return PropertyValueKind::boolean;
  }
  constexpr std::array<std::string_view, 7> kSignedIntegerTypes{
      "Int8", "Int16", "Short", "Int32", "Int", "Int64", "Int128"};
  if (std::find(kSignedIntegerTypes.begin(), kSignedIntegerTypes.end(), type) !=
      kSignedIntegerTypes.end()) {
    return PropertyValueKind::signed_integer;
  }
  constexpr std::array<std::string_view, 8> kUnsignedIntegerTypes{
      "UInt8",  "Byte", "UInt16", "UShort",
      "UInt32", "UInt", "UInt64", "UInt128"};
  if (std::find(kUnsignedIntegerTypes.begin(), kUnsignedIntegerTypes.end(),
                type) != kUnsignedIntegerTypes.end()) {
    return PropertyValueKind::unsigned_integer;
  }
  constexpr std::array<std::string_view, 6> kFloatingPointTypes{
      "Float32", "Float", "Float64", "Double", "Float128", "Quad"};
  if (std::find(kFloatingPointTypes.begin(), kFloatingPointTypes.end(), type) !=
      kFloatingPointTypes.end()) {
    return PropertyValueKind::floating_point;
  }
  constexpr std::array<std::string_view, 4> kComplexTypes{
      "Complex32", "Complex64", "Complex", "Complex128"};
  if (std::find(kComplexTypes.begin(), kComplexTypes.end(), type) !=
      kComplexTypes.end()) {
    return PropertyValueKind::complex;
  }
  return PropertyValueKind::other;
}

unsigned integer_bit_width(std::string_view type) {
  if (type == "Int8" || type == "UInt8" || type == "Byte") {
    return 8;
  }
  if (type == "Int16" || type == "Short" || type == "UInt16" ||
      type == "UShort") {
    return 16;
  }
  if (type == "Int32" || type == "Int" || type == "UInt32" || type == "UInt") {
    return 32;
  }
  if (type == "Int64" || type == "UInt64") {
    return 64;
  }
  if (type == "Int128" || type == "UInt128") {
    return 128;
  }
  return 0;
}

bool is_valid_scalar_or_complex_value(std::string_view type,
                                      std::string_view value) {
  switch (classify_property_value_kind(type)) {
  case PropertyValueKind::boolean: {
    const auto trimmed = trim_xml_whitespace(value);
    return trimmed == "true" || trimmed == "false" || trimmed == "0" ||
           trimmed == "1";
  }
  case PropertyValueKind::signed_integer:
    return is_valid_integer_value(value, true, integer_bit_width(type));
  case PropertyValueKind::unsigned_integer:
    return is_valid_integer_value(value, false, integer_bit_width(type));
  case PropertyValueKind::floating_point:
    return is_valid_floating_point_value(value);
  case PropertyValueKind::complex:
    return is_valid_complex_value(value);
  case PropertyValueKind::other:
    return false;
  }
  return false;
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

bool parse_finite_double(std::string_view text, double &value) {
  text = trim_xml_whitespace(text);
  if (text.starts_with('+')) {
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return false;
  }
  std::istringstream input{std::string(text)};
  input.imbue(std::locale::classic());
  input >> std::noskipws >> value;
  return !input.fail() && input.peek() == std::char_traits<char>::eof() &&
         std::isfinite(value);
}

template <std::size_t Count>
bool parse_finite_tuple(std::string_view text,
                        std::array<double, Count> &values) {
  std::size_t offset = 0;
  for (std::size_t index = 0; index < Count; ++index) {
    const auto separator = text.find(':', offset);
    const bool last = index + 1 == Count;
    if ((last && separator != std::string_view::npos) ||
        (!last && separator == std::string_view::npos)) {
      return false;
    }
    const auto end = last ? text.size() : separator;
    if (!parse_finite_double(text.substr(offset, end - offset),
                             values[index])) {
      return false;
    }
    offset = end + 1;
  }
  return true;
}

Result<std::pair<double, double>> parse_bounds(std::string_view text) {
  const auto separator = text.find(':');
  if (separator == std::string_view::npos ||
      text.find(':', separator + 1) != std::string_view::npos) {
    return make_error(ErrorCode::invalid_xisf,
                      "Image bounds must contain two values");
  }
  double lower = 0;
  double upper = 0;
  if (!parse_finite_double(text.substr(0, separator), lower) ||
      !parse_finite_double(text.substr(separator + 1), upper) ||
      !(lower < upper)) {
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

struct MetadataBindingEvent {
  std::optional<std::size_t> metadata_index;
  std::string reference;
  MetadataBinding::Scope scope{MetadataBinding::Scope::xisf_unit};
  std::optional<std::size_t> image_index;
  bool by_reference{false};
};

struct AncillaryBindingEvent {
  std::size_t object_index{0};
  std::size_t image_index{0};
};

struct IccProfileBindingEvent {
  std::size_t profile_index{0};
  std::size_t image_index{0};
};

struct ThumbnailBindingEvent {
  std::size_t thumbnail_index{0};
  std::size_t image_index{0};
};

struct TableBindingEvent {
  std::size_t table_index{0};
  std::size_t image_index{0};
};

struct TableStructureReferenceEvent {
  std::size_t table_index{0};
  std::string reference;
};

struct XmlElementName {
  std::string namespace_uri;
  std::string name;
};

struct XmlBuilder {
  enum class EmbeddedEncoding { none, base64, hex };

  std::string version;
  std::vector<ImageInfo> images;
  std::vector<std::vector<std::byte>> embedded_blocks;
  std::vector<std::vector<std::byte>> inline_metadata_blocks;
  std::vector<std::vector<std::byte>> inline_icc_profile_blocks;
  std::vector<ThumbnailInfo> thumbnails;
  std::vector<std::vector<std::byte>> embedded_thumbnail_blocks;
  std::vector<bool> embedded_thumbnail_data_seen;
  std::unordered_map<std::string, std::size_t> thumbnail_uids;
  std::vector<ThumbnailBindingEvent> thumbnail_binding_events;
  std::vector<TableStructureInfo> table_structures;
  std::unordered_map<std::string, std::size_t> table_structure_uids;
  std::unordered_map<std::string, std::size_t> standalone_structure_uids;
  std::vector<TableInfo> tables;
  std::unordered_map<std::string, std::size_t> table_uids;
  std::vector<TableBindingEvent> table_binding_events;
  std::vector<TableStructureReferenceEvent> table_structure_references;
  std::size_t table_field_count{0};
  std::size_t table_row_count{0};
  std::size_t table_cell_count{0};
  std::size_t table_text_bytes{0};
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
  bool saw_xml_declaration{false};
  std::vector<std::size_t> image_stack;
  std::vector<std::string> element_stack;
  std::vector<XmlElementName> qualified_element_stack;
  std::vector<std::optional<std::size_t>> extension_stack;
  std::vector<ExtensionElement> extension_elements;
  std::size_t extension_attribute_count{0};
  std::size_t extension_bytes{0};
  std::vector<AncillaryObject> ancillary_objects;
  std::unordered_map<std::string, std::size_t> ancillary_uids;
  std::vector<AncillaryBindingEvent> ancillary_binding_events;
  std::size_t ancillary_attribute_count{0};
  std::size_t ancillary_bytes{0};
  std::vector<IccProfileInfo> icc_profiles;
  std::unordered_map<std::string, std::size_t> icc_profile_uids;
  std::vector<IccProfileBindingEvent> icc_profile_binding_events;
  std::optional<std::size_t> text_metadata_index;
  std::size_t metadata_count{0};
  bool saw_creation_time{false};
  bool saw_creator_application{false};
  std::unordered_set<std::string> core_uids;
  std::vector<std::string> references;
  std::vector<std::string> references_from_thumbnails;
  std::unordered_map<std::string, std::size_t> metadata_uids;
  std::vector<MetadataBindingEvent> metadata_binding_events;
  std::optional<std::size_t> embedded_image_index;
  std::optional<std::size_t> inline_metadata_index;
  std::optional<std::size_t> inline_icc_profile_index;
  std::optional<std::size_t> embedded_thumbnail_index;
  std::optional<std::size_t> open_thumbnail_index;
  std::optional<std::size_t> open_table_index;
  std::optional<std::size_t> open_structure_index;
  std::optional<std::size_t> open_table_row_index;
  std::optional<std::size_t> open_table_cell_index;
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

bool consume_extension_bytes(XmlBuilder &state, std::size_t count,
                             std::string_view element) {
  const auto limit = state.options.max_extension_bytes;
  const auto available = limit - std::min(state.extension_bytes, limit);
  if (count > available) {
    state.fail(ErrorCode::resource_limit,
               "Extension inventory byte limit exceeded", std::string(element));
    return false;
  }
  state.extension_bytes += count;
  return true;
}

bool consume_ancillary_bytes(XmlBuilder &state, std::size_t count,
                             std::string_view element) {
  const auto limit = state.options.max_ancillary_bytes;
  const auto available = limit - std::min(state.ancillary_bytes, limit);
  if (count > available) {
    state.fail(ErrorCode::resource_limit,
               "Ancillary metadata byte limit exceeded", std::string(element));
    return false;
  }
  state.ancillary_bytes += count;
  return true;
}

bool consume_table_text_bytes(XmlBuilder &state, std::size_t count,
                              std::string_view element) {
  const auto limit = state.options.max_table_text_bytes;
  const auto available = limit - std::min(state.table_text_bytes, limit);
  if (count > available) {
    state.fail(ErrorCode::resource_limit,
               "Table text byte limit exceeded", std::string(element));
    return false;
  }
  state.table_text_bytes += count;
  return true;
}

bool ascii_case_equal(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    const auto fold = [](char character) {
      return character >= 'A' && character <= 'Z'
                 ? static_cast<char>(character - 'A' + 'a')
                 : character;
    };
    if (fold(left[index]) != fold(right[index])) {
      return false;
    }
  }
  return true;
}

void XMLCALL xml_declaration(void *user_data, const XML_Char *version,
                             const XML_Char *encoding, int standalone) {
  auto &state = *static_cast<XmlBuilder *>(user_data);
  state.saw_xml_declaration = true;
  if (version == nullptr || std::string_view(version) != "1.0") {
    state.fail(ErrorCode::invalid_xisf, "XISF requires an XML 1.0 declaration");
    return;
  }
  if (encoding == nullptr || !(ascii_case_equal(encoding, "UTF-8") ||
                               ascii_case_equal(encoding, "UTF8"))) {
    state.fail(ErrorCode::invalid_xisf,
               "XISF requires a UTF-8 XML declaration");
    return;
  }
  if (standalone != -1) {
    state.fail(ErrorCode::invalid_xisf,
               "XISF XML declaration cannot specify standalone");
  }
}

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

bool append_inline_byte(XmlBuilder &state, unsigned char value) {
  const bool metadata_block = state.inline_metadata_index.has_value();
  const bool icc_profile_block = state.inline_icc_profile_index.has_value();
  const bool thumbnail_block = state.embedded_thumbnail_index.has_value();
  auto &output =
      metadata_block
          ? state.inline_metadata_blocks[*state.inline_metadata_index]
      : icc_profile_block
          ? state.inline_icc_profile_blocks[*state.inline_icc_profile_index]
      : thumbnail_block
          ? state.embedded_thumbnail_blocks[*state.embedded_thumbnail_index]
          : state.embedded_blocks[*state.embedded_image_index];
  const auto limit =
      metadata_block      ? state.options.max_serialized_property_bytes
      : icc_profile_block ? state.options.max_serialized_icc_profile_bytes
      : thumbnail_block   ? state.options.max_serialized_thumbnail_bytes
                          : state.options.max_decoded_image_bytes;
  const auto element = metadata_block      ? "Property"
                       : icc_profile_block ? "ICCProfile"
                                           : "Data";
  if (output.size() >= limit) {
    state.fail(ErrorCode::resource_limit,
               "Encoded block exceeds its serialized byte limit", element);
    return false;
  }
  try {
    output.push_back(static_cast<std::byte>(value));
  } catch (const std::bad_alloc &) {
    state.fail(ErrorCode::resource_limit,
               "Memory allocation failed for encoded block", element);
    return false;
  }
  return true;
}

bool decode_base64_quartet(XmlBuilder &state) {
  const auto &q = state.base64_quartet;
  const auto element = state.inline_metadata_index.has_value() ? "Property"
                       : state.inline_icc_profile_index.has_value()
                           ? "ICCProfile"
                           : "Data";
  if (q[0] == 64 || q[1] == 64) {
    state.fail(ErrorCode::invalid_xisf, "Invalid Base64 padding", element);
    return false;
  }
  if (!append_inline_byte(
          state, static_cast<unsigned char>((q[0] << 2U) | (q[1] >> 4U)))) {
    return false;
  }
  if (q[2] == 64) {
    if (q[3] != 64 || (q[1] & 0x0fU) != 0) {
      state.fail(ErrorCode::invalid_xisf,
                 "Invalid or noncanonical Base64 padding", element);
      return false;
    }
    state.base64_complete = true;
    return true;
  }
  if (!append_inline_byte(
          state, static_cast<unsigned char>((q[1] << 4U) | (q[2] >> 2U)))) {
    return false;
  }
  if (q[3] == 64) {
    if ((q[2] & 0x03U) != 0) {
      state.fail(ErrorCode::invalid_xisf,
                 "Invalid or noncanonical Base64 padding", element);
      return false;
    }
    state.base64_complete = true;
    return true;
  }
  return append_inline_byte(state,
                            static_cast<unsigned char>((q[2] << 6U) | q[3]));
}

void decode_embedded_text(XmlBuilder &state, std::string_view text) {
  const bool metadata_block = state.inline_metadata_index.has_value();
  const auto element = metadata_block ? "Property"
                       : state.inline_icc_profile_index.has_value()
                           ? "ICCProfile"
                           : "Data";
  for (const char character : text) {
    if (character == ' ' || character == '\t' || character == '\r' ||
        character == '\n') {
      continue;
    }
    if (state.encoded_block_bytes >= state.options.max_encoded_block_bytes) {
      state.fail(ErrorCode::resource_limit,
                 "Encoded block exceeds the encoded byte limit", element);
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
                   "Base16 data must use lowercase hexadecimal", element);
        return;
      }
      if (!state.hex_high_nibble) {
        state.hex_high_nibble = nibble;
      } else {
        if (!append_inline_byte(state,
                                static_cast<unsigned char>(
                                    (*state.hex_high_nibble << 4U) | nibble))) {
          return;
        }
        state.hex_high_nibble.reset();
      }
      continue;
    }
    if (state.base64_complete) {
      state.fail(ErrorCode::invalid_xisf, "Base64 data continues after padding",
                 element);
      return;
    }
    if (character == '=') {
      state.base64_quartet[state.base64_quartet_size++] = 64;
    } else {
      const auto value = base64_value(character);
      if (!value) {
        state.fail(ErrorCode::invalid_xisf,
                   "Encoded block contains an invalid Base64 character",
                   element);
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
  const std::string_view namespace_uri = namespace_name(qualified_name);
  const bool is_xisf_element = namespace_uri == kXisfNamespace;
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

  std::optional<std::size_t> extension_index;
  if (state.depth > 1 && !is_xisf_element) {
    if (state.extension_elements.size() >=
        state.options.max_extension_elements) {
      state.fail(ErrorCode::resource_limit,
                 "Extension element count limit exceeded", name);
      return;
    }
    const auto attribute_limit = state.options.max_extension_attributes;
    const auto attribute_capacity =
        attribute_limit -
        std::min(state.extension_attribute_count, attribute_limit);
    if (attribute_count > attribute_capacity) {
      state.fail(ErrorCode::resource_limit,
                 "Extension attribute count limit exceeded", name);
      return;
    }

    const auto parent_namespace_uri =
        state.qualified_element_stack.empty()
            ? std::string_view{}
            : std::string_view(
                  state.qualified_element_stack.back().namespace_uri);
    const auto parent_name =
        state.qualified_element_stack.empty()
            ? std::string_view{}
            : std::string_view(state.qualified_element_stack.back().name);
    std::size_t copied_bytes = namespace_uri.size();
    const auto add_copied_bytes = [&](std::size_t count) {
      if (count > std::numeric_limits<std::size_t>::max() - copied_bytes) {
        return false;
      }
      copied_bytes += count;
      return true;
    };
    if (!add_copied_bytes(name.size()) ||
        !add_copied_bytes(parent_namespace_uri.size()) ||
        !add_copied_bytes(parent_name.size())) {
      state.fail(ErrorCode::resource_limit,
                 "Extension inventory byte count overflow", name);
      return;
    }
    for (std::size_t index = 0; index < attribute_count; ++index) {
      if (!add_copied_bytes(namespace_name(attributes[index * 2]).size()) ||
          !add_copied_bytes(local_name(attributes[index * 2]).size()) ||
          !add_copied_bytes(
              std::string_view(attributes[index * 2 + 1]).size())) {
        state.fail(ErrorCode::resource_limit,
                   "Extension inventory byte count overflow", name);
        return;
      }
    }
    if (!consume_extension_bytes(state, copied_bytes, name)) {
      return;
    }

    ExtensionElement extension;
    extension.namespace_uri = std::string(namespace_uri);
    extension.name = name;
    extension.parent_namespace_uri = std::string(parent_namespace_uri);
    extension.parent_name = std::string(parent_name);
    if (!state.extension_stack.empty()) {
      extension.parent_extension_index = state.extension_stack.back();
    }
    extension.image_index = state.current_image();
    extension.attributes.reserve(attribute_count);
    for (std::size_t index = 0; index < attribute_count; ++index) {
      XmlAttribute entry;
      entry.namespace_uri = std::string(namespace_name(attributes[index * 2]));
      entry.name = std::string(local_name(attributes[index * 2]));
      entry.value = std::string(attributes[index * 2 + 1]);
      extension.attributes.push_back(std::move(entry));
    }
    state.extension_attribute_count += attribute_count;
    extension_index = state.extension_elements.size();
    state.extension_elements.push_back(std::move(extension));
  }
  state.element_stack.push_back(is_xisf_element ? name : std::string{});
  state.qualified_element_stack.push_back(
      XmlElementName{std::string(namespace_uri), name});
  state.extension_stack.push_back(extension_index);

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

  if (state.embedded_image_index || state.inline_metadata_index ||
      state.inline_icc_profile_index || state.embedded_thumbnail_index) {
    state.fail(ErrorCode::invalid_xisf,
               "Encoded data blocks cannot contain child elements", name);
    return;
  }

  if (parent == "Cell" || parent == "Field" ||
      (parent == "Row" && (!is_xisf_element || name != "Cell"))) {
    state.fail(ErrorCode::invalid_xisf,
               "Table Field and Cell cannot have children, and Row accepts "
               "only Cell children",
               name);
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
      if (parent == "Thumbnail") {
        state.references_from_thumbnails.emplace_back(*reference);
      }
      if (parent == "Table") {
        if (!state.open_table_index) {
          state.fail(ErrorCode::invalid_xisf,
                     "Reference has no open Table parent", name);
          return;
        }
        if (state.table_structure_references.size() >=
            state.options.max_tables) {
          state.fail(ErrorCode::resource_limit,
                     "Table structure reference limit exceeded", name);
          return;
        }
        state.table_structure_references.push_back(
            TableStructureReferenceEvent{*state.open_table_index,
                                         std::string(*reference)});
      }
      if ((parent == "Image" && state.current_image()) ||
          parent == "Metadata") {
        if (state.metadata_binding_events.size() >=
            state.options.max_metadata_entries) {
          state.fail(ErrorCode::resource_limit,
                     "Metadata binding limit exceeded", name);
          return;
        }
        MetadataBindingEvent event;
        event.reference = std::string(*reference);
        event.scope = parent == "Image" ? MetadataBinding::Scope::image
                                        : MetadataBinding::Scope::xisf_unit;
        event.image_index = state.current_image();
        event.by_reference = true;
        state.metadata_binding_events.push_back(std::move(event));
      }
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

  if (is_xisf_element && name == "Table") {
    const auto image_index = state.current_image();
    const bool root_child = state.depth == 2 && parent == "xisf";
    const bool image_child =
        state.depth == 3 && parent == "Image" && image_index.has_value();
    if (!root_child && !image_child) {
      state.fail(ErrorCode::invalid_xisf,
                 "Table must be a direct child of xisf or Image", name);
      return;
    }
    if (state.tables.size() >= state.options.max_tables) {
      state.fail(ErrorCode::resource_limit, "Table count limit exceeded",
                 name);
      return;
    }
    const auto identity = attribute(attributes, "id");
    if (!identity || !is_valid_property_identifier(*identity)) {
      state.fail(ErrorCode::invalid_xisf,
                 "Table requires a valid property identifier", name, "id");
      return;
    }
    TableInfo table;
    table.uid = std::string(attribute(attributes, "uid").value_or(""));
    table.image_index = image_index;
    table.id = std::string(*identity);
    table.caption =
        std::string(attribute(attributes, "caption").value_or(""));
    table.comment =
        std::string(attribute(attributes, "comment").value_or(""));
    const auto parse_declared_extent = [&](std::string_view attribute_name,
                                           std::optional<std::uint64_t> &out) {
      const auto value = attribute(attributes, attribute_name);
      if (!value) {
        return true;
      }
      std::uint64_t parsed = 0;
      if (!parse_unsigned(*value, parsed)) {
        state.fail(ErrorCode::invalid_xisf,
                   "Table extent must be an unsigned integer", name,
                   std::string(attribute_name));
        return false;
      }
      out = parsed;
      return true;
    };
    if (!parse_declared_extent("rows", table.declared_rows) ||
        !parse_declared_extent("columns", table.declared_columns)) {
      return;
    }
    const auto copied_bytes = table.uid.size() + table.id.size() +
                              table.caption.size() + table.comment.size();
    if (!consume_table_text_bytes(state, copied_bytes, name)) {
      return;
    }
    const auto table_index = state.tables.size();
    state.tables.push_back(std::move(table));
    state.open_table_index = table_index;
    if (!state.tables.back().uid.empty()) {
      state.table_uids.emplace(state.tables.back().uid, table_index);
    }
    if (image_index) {
      if (state.table_binding_events.size() >=
          state.options.max_table_bindings) {
        state.fail(ErrorCode::resource_limit,
                   "Table binding limit exceeded", name);
        return;
      }
      state.table_binding_events.push_back(
          TableBindingEvent{table_index, *image_index});
    }
    return;
  }

  if (is_xisf_element && name == "Structure") {
    const bool root_child = state.depth == 2 && parent == "xisf";
    const bool table_child = state.depth >= 3 && parent == "Table" &&
                             state.open_table_index.has_value();
    if (!root_child && !table_child) {
      state.fail(ErrorCode::invalid_xisf,
                 "Structure must be a direct child of xisf or Table", name);
      return;
    }
    if (state.table_structures.size() >= state.options.max_table_structures) {
      state.fail(ErrorCode::resource_limit,
                 "Table Structure count limit exceeded", name);
      return;
    }
    const auto uid = attribute(attributes, "uid");
    if (root_child && !uid) {
      state.fail(ErrorCode::invalid_xisf,
                 "Standalone Structure requires a uid", name, "uid");
      return;
    }
    if (table_child &&
        state.tables[*state.open_table_index].structure_index.has_value()) {
      state.fail(ErrorCode::invalid_xisf,
                 "Table cannot contain more than one Structure", name);
      return;
    }
    TableStructureInfo structure;
    structure.uid = std::string(uid.value_or(""));
    structure.table_index =
        table_child ? state.open_table_index : std::nullopt;
    if (!consume_table_text_bytes(state, structure.uid.size(), name)) {
      return;
    }
    const auto structure_index = state.table_structures.size();
    state.table_structures.push_back(std::move(structure));
    state.open_structure_index = structure_index;
    if (!state.table_structures.back().uid.empty()) {
      state.table_structure_uids.emplace(state.table_structures.back().uid,
                                         structure_index);
    }
    if (root_child) {
      state.standalone_structure_uids.emplace(
          state.table_structures.back().uid, structure_index);
    } else {
      state.tables[*state.open_table_index].structure_index = structure_index;
    }
    return;
  }

  if (is_xisf_element && name == "Field") {
    if (parent != "Structure" || !state.open_structure_index) {
      state.fail(ErrorCode::invalid_xisf,
                 "Field must be a direct child of Structure", name);
      return;
    }
    if (state.table_field_count >= state.options.max_table_fields) {
      state.fail(ErrorCode::resource_limit,
                 "Table field limit exceeded", name);
      return;
    }
    const auto identity = attribute(attributes, "id");
    const auto type = attribute(attributes, "type");
    if (!identity || !is_valid_property_identifier(*identity) || !type ||
        classify_property_type(*type) == PropertyCategory::unknown) {
      state.fail(ErrorCode::invalid_xisf,
                 "Field requires a valid id and non-Table property type",
                 name);
      return;
    }
    TableFieldInfo field;
    field.id = std::string(*identity);
    field.type = std::string(*type);
    field.format =
        std::string(attribute(attributes, "format").value_or(""));
    field.header =
        std::string(attribute(attributes, "header").value_or(""));
    const auto copied_bytes = field.id.size() + field.type.size() +
                              field.format.size() + field.header.size();
    if (!consume_table_text_bytes(state, copied_bytes, name)) {
      return;
    }
    state.table_structures[*state.open_structure_index].fields.push_back(
        std::move(field));
    ++state.table_field_count;
    return;
  }

  if (is_xisf_element && name == "Row") {
    if (parent != "Table" || !state.open_table_index ||
        state.open_table_row_index) {
      state.fail(ErrorCode::invalid_xisf,
                 "Row must be a direct child of Table", name);
      return;
    }
    if (state.table_row_count >= state.options.max_table_rows) {
      state.fail(ErrorCode::resource_limit, "Table row limit exceeded", name);
      return;
    }
    auto &rows = state.tables[*state.open_table_index].rows;
    rows.emplace_back();
    state.open_table_row_index = rows.size() - 1;
    ++state.table_row_count;
    return;
  }

  if (is_xisf_element && name == "Cell") {
    if (parent != "Row" || !state.open_table_index ||
        !state.open_table_row_index || state.open_table_cell_index) {
      state.fail(ErrorCode::invalid_xisf,
                 "Cell must be a direct child of Row", name);
      return;
    }
    if (state.table_cell_count >= state.options.max_table_cells) {
      state.fail(ErrorCode::resource_limit, "Table cell limit exceeded",
                 name);
      return;
    }
    if (attribute(attributes, "id") || attribute(attributes, "type") ||
        attribute(attributes, "format")) {
      state.fail(ErrorCode::invalid_xisf,
                 "Cell cannot declare id, type, or format", name);
      return;
    }
    const auto value = attribute(attributes, "value");
    const auto location = attribute(attributes, "location");
    const auto byte_order_attribute = attribute(attributes, "byteOrder");
    const auto compression_attribute = attribute(attributes, "compression");
    const auto subblocks_attribute = attribute(attributes, "subblocks");
    const auto checksum_attribute = attribute(attributes, "checksum");
    if (value && location) {
      state.fail(ErrorCode::invalid_xisf,
                 "Cell cannot declare both value and location", name);
      return;
    }
    if (!location && (byte_order_attribute || compression_attribute ||
                      subblocks_attribute || checksum_attribute)) {
      state.fail(ErrorCode::invalid_xisf,
                 "Cell data-block attributes require a location", name);
      return;
    }
    TableCellInfo cell;
    cell.value = std::string(value.value_or(""));
    if (!consume_table_text_bytes(state, cell.value.size(), name)) {
      return;
    }
    if (location) {
      cell.block = parse_location(*location);
      if (cell.block.kind == BlockKind::unknown ||
          cell.block.kind == BlockKind::embedded) {
        state.fail(ErrorCode::invalid_xisf,
                   "Cell has an invalid data block location", name,
                   "location");
        return;
      }
      if (cell.block.kind == BlockKind::inline_data &&
          cell.block.raw != "inline:base64" &&
          cell.block.raw != "inline:hex") {
        state.fail(ErrorCode::unsupported_feature,
                   "Unsupported inline Cell block encoding", name,
                   "location");
        return;
      }
      cell.value_form = TableCellInfo::ValueForm::data_block;
    } else if (!value) {
      cell.value_form = TableCellInfo::ValueForm::character_data;
    }
    const auto byte_order = byte_order_attribute.value_or("little");
    if (byte_order != "little" && byte_order != "big") {
      state.fail(ErrorCode::invalid_xisf,
                 "Cell has an invalid byteOrder", name, "byteOrder");
      return;
    }
    cell.byte_order = byte_order == "big" ? ByteOrder::big : ByteOrder::little;
    cell.compression = std::string(compression_attribute.value_or(""));
    cell.subblocks = std::string(subblocks_attribute.value_or(""));
    cell.checksum = std::string(checksum_attribute.value_or(""));
    if (!consume_table_text_bytes(state, cell.block.raw.size(), name) ||
        !consume_table_text_bytes(state, cell.compression.size(), name) ||
        !consume_table_text_bytes(state, cell.subblocks.size(), name) ||
        !consume_table_text_bytes(state, cell.checksum.size(), name)) {
      return;
    }
    const auto parse_extent = [&](std::string_view attribute_name,
                                  std::optional<std::uint64_t> &out) {
      const auto serialized = attribute(attributes, attribute_name);
      if (!serialized) {
        return true;
      }
      std::uint64_t parsed = 0;
      if (!parse_unsigned(*serialized, parsed)) {
        state.fail(ErrorCode::invalid_xisf,
                   "Cell extent must be an unsigned integer", name,
                   std::string(attribute_name));
        return false;
      }
      out = parsed;
      return true;
    };
    if (!parse_extent("length", cell.length) ||
        !parse_extent("rows", cell.rows) ||
        !parse_extent("columns", cell.columns)) {
      return;
    }
    auto &cells = state.tables[*state.open_table_index]
                      .rows[*state.open_table_row_index]
                      .cells;
    cells.push_back(std::move(cell));
    state.open_table_cell_index = cells.size() - 1;
    ++state.table_cell_count;
    return;
  }

  if (is_xisf_element && name == "ICCProfile") {
    const auto image_index = state.current_image();
    const bool root_child = state.depth == 2 && parent == "xisf";
    const bool image_child =
        state.depth == 3 && parent == "Image" && image_index.has_value();
    if (!root_child && !image_child) {
      state.fail(ErrorCode::invalid_xisf,
                 "ICCProfile must be a direct child of xisf or Image", name);
      return;
    }
    if (state.icc_profiles.size() >= state.options.max_icc_profiles) {
      state.fail(ErrorCode::resource_limit, "ICC profile count limit exceeded",
                 name);
      return;
    }
    const auto location = attribute(attributes, "location");
    if (!location) {
      state.fail(ErrorCode::invalid_xisf,
                 "ICCProfile requires a data block location", name, "location");
      return;
    }
    if (attribute(attributes, "byteOrder")) {
      state.fail(ErrorCode::invalid_xisf, "ICCProfile cannot declare byteOrder",
                 name, "byteOrder");
      return;
    }
    IccProfileInfo profile;
    profile.uid = std::string(attribute(attributes, "uid").value_or(""));
    profile.image_index = image_index;
    profile.block = parse_location(*location);
    if (profile.block.kind == BlockKind::unknown ||
        profile.block.kind == BlockKind::embedded) {
      state.fail(ErrorCode::invalid_xisf,
                 "ICCProfile has an invalid data block location", name,
                 "location");
      return;
    }
    if (profile.block.kind == BlockKind::inline_data &&
        profile.block.raw != "inline:base64" &&
        profile.block.raw != "inline:hex") {
      state.fail(ErrorCode::unsupported_feature,
                 "Unsupported inline ICCProfile block encoding", name,
                 "location");
      return;
    }
    profile.compression =
        std::string(attribute(attributes, "compression").value_or(""));
    profile.subblocks =
        std::string(attribute(attributes, "subblocks").value_or(""));
    profile.checksum =
        std::string(attribute(attributes, "checksum").value_or(""));
    const auto profile_index = state.icc_profiles.size();
    state.icc_profiles.push_back(std::move(profile));
    state.inline_icc_profile_blocks.emplace_back();
    if (!state.icc_profiles.back().uid.empty()) {
      state.icc_profile_uids.emplace(state.icc_profiles.back().uid,
                                     profile_index);
    }
    if (image_index) {
      if (state.icc_profile_binding_events.size() >=
          state.options.max_icc_profile_bindings) {
        state.fail(ErrorCode::resource_limit,
                   "ICC profile binding limit exceeded", name);
        return;
      }
      state.icc_profile_binding_events.push_back(
          IccProfileBindingEvent{profile_index, *image_index});
    }
    if (state.icc_profiles.back().block.kind == BlockKind::inline_data) {
      state.inline_icc_profile_index = profile_index;
      state.embedded_encoding =
          state.icc_profiles.back().block.raw == "inline:base64"
              ? XmlBuilder::EmbeddedEncoding::base64
              : XmlBuilder::EmbeddedEncoding::hex;
      state.base64_quartet_size = 0;
      state.base64_complete = false;
      state.hex_high_nibble.reset();
      state.encoded_block_bytes = 0;
    }
    return;
  }

  const auto parsed_ancillary_kind =
      is_xisf_element ? ancillary_kind(name) : std::nullopt;
  if (parsed_ancillary_kind) {
    const auto image_index = state.current_image();
    const bool root_child = state.depth == 2 && parent == "xisf";
    const bool image_child =
        state.depth == 3 && parent == "Image" && image_index.has_value();
    if (!root_child && !image_child) {
      state.fail(ErrorCode::invalid_xisf,
                 "Ancillary metadata must be a direct child of xisf or Image",
                 name);
      return;
    }
    if (state.ancillary_objects.size() >= state.options.max_ancillary_objects) {
      state.fail(ErrorCode::resource_limit,
                 "Ancillary metadata object limit exceeded", name);
      return;
    }
    const auto attribute_limit = state.options.max_ancillary_attributes;
    const auto attribute_capacity =
        attribute_limit -
        std::min(state.ancillary_attribute_count, attribute_limit);
    if (attribute_count > attribute_capacity) {
      state.fail(ErrorCode::resource_limit,
                 "Ancillary metadata attribute limit exceeded", name);
      return;
    }

    const auto require = [&](std::string_view attribute_name) {
      const auto value = attribute(attributes, attribute_name);
      if (!value) {
        state.fail(ErrorCode::invalid_xisf,
                   "Ancillary metadata is missing a required attribute", name,
                   std::string(attribute_name));
      }
      return value;
    };
    switch (*parsed_ancillary_kind) {
    case AncillaryKind::rgb_working_space: {
      const auto gamma = require("gamma");
      const auto x = require("x");
      const auto y = require("y");
      const auto luminance = require("Y");
      if (!gamma || !x || !y || !luminance) {
        return;
      }
      double gamma_value = 0;
      if (!ascii_case_equal(*gamma, "sRGB") &&
          (!parse_finite_double(*gamma, gamma_value) || gamma_value <= 0)) {
        state.fail(ErrorCode::invalid_xisf,
                   "RGBWorkingSpace gamma must be sRGB or a positive finite "
                   "number",
                   name, "gamma");
        return;
      }
      std::array<double, 3> x_values{};
      std::array<double, 3> y_values{};
      std::array<double, 3> luminance_values{};
      if (!parse_finite_tuple(*x, x_values) ||
          !parse_finite_tuple(*y, y_values) ||
          !parse_finite_tuple(*luminance, luminance_values) ||
          !std::all_of(x_values.begin(), x_values.end(),
                       [](double value) { return value >= 0 && value <= 1; }) ||
          !std::all_of(y_values.begin(), y_values.end(),
                       [](double value) { return value >= 0 && value <= 1; }) ||
          !std::all_of(luminance_values.begin(), luminance_values.end(),
                       [](double value) { return value >= 0 && value <= 1; })) {
        state.fail(ErrorCode::invalid_xisf,
                   "RGBWorkingSpace x, y, and Y must be finite normalized "
                   "three-component vectors",
                   name);
        return;
      }
      break;
    }
    case AncillaryKind::display_function: {
      constexpr std::array<std::string_view, 5> kVectorAttributes{"m", "s", "h",
                                                                  "l", "r"};
      for (const auto attribute_name : kVectorAttributes) {
        const auto value = require(attribute_name);
        std::array<double, 4> parsed{};
        if (!value) {
          return;
        }
        if (!parse_finite_tuple(*value, parsed)) {
          state.fail(ErrorCode::invalid_xisf,
                     "DisplayFunction parameters must be finite "
                     "four-component vectors",
                     name, std::string(attribute_name));
          return;
        }
      }
      break;
    }
    case AncillaryKind::color_filter_array: {
      const auto pattern = require("pattern");
      const auto width_text = require("width");
      const auto height_text = require("height");
      if (!pattern || !width_text || !height_text) {
        return;
      }
      std::uint64_t width = 0;
      std::uint64_t height = 0;
      std::uint64_t element_count = 0;
      constexpr std::string_view kPatternCharacters = "0RGBWCMY";
      if (!parse_unsigned(*width_text, width) || width == 0 ||
          !parse_unsigned(*height_text, height) || height == 0 ||
          !checked_multiply(width, height, element_count) ||
          element_count != pattern->size() ||
          !std::all_of(pattern->begin(), pattern->end(),
                       [kPatternCharacters](char character) {
                         return kPatternCharacters.find(character) !=
                                std::string_view::npos;
                       })) {
        state.fail(ErrorCode::invalid_xisf,
                   "ColorFilterArray requires a valid width by height pattern",
                   name);
        return;
      }
      break;
    }
    case AncillaryKind::resolution: {
      const auto horizontal = require("horizontal");
      const auto vertical = require("vertical");
      if (!horizontal || !vertical) {
        return;
      }
      double horizontal_value = 0;
      double vertical_value = 0;
      const auto unit = attribute(attributes, "unit").value_or("inch");
      if (!parse_finite_double(*horizontal, horizontal_value) ||
          horizontal_value <= 0 ||
          !parse_finite_double(*vertical, vertical_value) ||
          vertical_value <= 0 || (unit != "inch" && unit != "cm")) {
        state.fail(ErrorCode::invalid_xisf,
                   "Resolution values must be positive and unit must be inch "
                   "or cm",
                   name);
        return;
      }
      break;
    }
    }

    const auto uid = attribute(attributes, "uid").value_or("");
    std::size_t copied_bytes = uid.size();
    const auto add_copied_bytes = [&](std::size_t count) {
      if (count > std::numeric_limits<std::size_t>::max() - copied_bytes) {
        return false;
      }
      copied_bytes += count;
      return true;
    };
    for (std::size_t index = 0; index < attribute_count; ++index) {
      if (!add_copied_bytes(namespace_name(attributes[index * 2]).size()) ||
          !add_copied_bytes(local_name(attributes[index * 2]).size()) ||
          !add_copied_bytes(
              std::string_view(attributes[index * 2 + 1]).size())) {
        state.fail(ErrorCode::resource_limit,
                   "Ancillary metadata byte count overflow", name);
        return;
      }
    }
    if (!consume_ancillary_bytes(state, copied_bytes, name)) {
      return;
    }

    AncillaryObject object;
    object.kind = *parsed_ancillary_kind;
    object.uid = std::string(uid);
    object.image_index = image_index;
    object.attributes.reserve(attribute_count);
    for (std::size_t index = 0; index < attribute_count; ++index) {
      XmlAttribute entry;
      entry.namespace_uri = std::string(namespace_name(attributes[index * 2]));
      entry.name = std::string(local_name(attributes[index * 2]));
      entry.value = std::string(attributes[index * 2 + 1]);
      object.attributes.push_back(std::move(entry));
    }
    state.ancillary_attribute_count += attribute_count;
    const auto object_index = state.ancillary_objects.size();
    state.ancillary_objects.push_back(std::move(object));
    if (!uid.empty()) {
      state.ancillary_uids.emplace(uid, object_index);
    }
    if (image_index) {
      if (state.ancillary_binding_events.size() >=
          state.options.max_ancillary_bindings) {
        state.fail(ErrorCode::resource_limit,
                   "Ancillary binding limit exceeded", name);
        return;
      }
      AncillaryBindingEvent event;
      event.object_index = object_index;
      event.image_index = *image_index;
      state.ancillary_binding_events.push_back(std::move(event));
    }
    return;
  }

  if (is_xisf_element && name == "Thumbnail") {
    const auto containing_image = state.current_image();
    const bool root_child = state.depth == 2 && parent == "xisf";
    const bool image_child =
        state.depth == 3 && parent == "Image" && containing_image.has_value();
    if (!root_child && !image_child) {
      state.fail(ErrorCode::invalid_xisf,
                 "Thumbnail must be a direct child of xisf or Image", name);
      return;
    }
    if (state.thumbnails.size() >= state.options.max_thumbnails) {
      state.fail(ErrorCode::resource_limit, "Thumbnail count limit exceeded",
                 name);
      return;
    }
    const auto geometry = attribute(attributes, "geometry");
    const auto sample_format = attribute(attributes, "sampleFormat");
    const auto location = attribute(attributes, "location");
    if (!geometry || !sample_format || !location) {
      state.fail(ErrorCode::invalid_xisf,
                 "Thumbnail is missing a required attribute", name);
      return;
    }
    if (attribute(attributes, "bounds")) {
      state.fail(ErrorCode::invalid_xisf, "Thumbnail cannot declare bounds",
                 name, "bounds");
      return;
    }
    auto parsed_geometry = parse_geometry(*geometry);
    if (!parsed_geometry || parsed_geometry.value().size() != 3) {
      state.fail(ErrorCode::invalid_xisf,
                 "Thumbnail must have two dimensions and channels", name,
                 "geometry");
      return;
    }
    if (parsed_geometry.value()[0] > state.options.max_thumbnail_dimension ||
        parsed_geometry.value()[1] > state.options.max_thumbnail_dimension) {
      state.fail(ErrorCode::resource_limit,
                 "Thumbnail dimensions exceed the configured limit", name,
                 "geometry");
      return;
    }
    if (*sample_format != "UInt8" && *sample_format != "UInt16") {
      state.fail(ErrorCode::invalid_xisf,
                 "Thumbnail sampleFormat must be UInt8 or UInt16", name,
                 "sampleFormat");
      return;
    }
    ImageInfo image;
    image.geometry = std::move(parsed_geometry).value();
    image.sample_format_name = std::string(*sample_format);
    image.sample_format =
        *sample_format == "UInt8" ? SampleFormat::uint8 : SampleFormat::uint16;
    image.color_space =
        std::string(attribute(attributes, "colorSpace").value_or("Gray"));
    const auto channels = image.geometry[2];
    if ((image.color_space == "Gray" && channels != 1 && channels != 2) ||
        (image.color_space == "RGB" && channels != 3 && channels != 4) ||
        (image.color_space != "Gray" && image.color_space != "RGB")) {
      state.fail(ErrorCode::invalid_xisf,
                 "Thumbnail colorSpace and channel count are incompatible",
                 name, "geometry");
      return;
    }
    if (image.color_space == "RGB") {
      image.nominal_channel_order =
          NominalChannelOrder::red_green_blue_then_alpha;
    }
    const auto orientation = attribute(attributes, "orientation");
    if (orientation) {
      if (*orientation == "0") {
        image.orientation = ImageOrientation::identity;
      } else if (*orientation == "flip") {
        image.orientation = ImageOrientation::flip;
      } else if (*orientation == "90") {
        image.orientation = ImageOrientation::rotate_90;
      } else if (*orientation == "90;flip") {
        image.orientation = ImageOrientation::rotate_90_flip;
      } else if (*orientation == "-90") {
        image.orientation = ImageOrientation::rotate_minus_90;
      } else if (*orientation == "-90;flip") {
        image.orientation = ImageOrientation::rotate_minus_90_flip;
      } else if (*orientation == "180") {
        image.orientation = ImageOrientation::rotate_180;
      } else if (*orientation == "180;flip") {
        image.orientation = ImageOrientation::rotate_180_flip;
      } else {
        state.fail(ErrorCode::invalid_xisf,
                   "Invalid Thumbnail orientation value", name, "orientation");
        return;
      }
    }
    image.id = std::string(attribute(attributes, "id").value_or(""));
    const auto pixel_storage =
        attribute(attributes, "pixelStorage").value_or("Planar");
    if (pixel_storage != "Planar" && pixel_storage != "Normal") {
      state.fail(ErrorCode::invalid_xisf,
                 "Invalid Thumbnail pixelStorage value", name, "pixelStorage");
      return;
    }
    image.pixel_storage =
        pixel_storage == "Normal" ? PixelStorage::normal : PixelStorage::planar;
    const auto byte_order =
        attribute(attributes, "byteOrder").value_or("little");
    if (byte_order != "little" && byte_order != "big") {
      state.fail(ErrorCode::invalid_xisf, "Invalid Thumbnail byteOrder value",
                 name, "byteOrder");
      return;
    }
    image.byte_order = byte_order == "big" ? ByteOrder::big : ByteOrder::little;
    image.block = parse_location(*location);
    if (image.block.kind == BlockKind::inline_data ||
        image.block.kind == BlockKind::unknown) {
      state.fail(ErrorCode::invalid_xisf,
                 "Thumbnail has an invalid data block location", name,
                 "location");
      return;
    }
    image.compression =
        std::string(attribute(attributes, "compression").value_or(""));
    image.subblocks =
        std::string(attribute(attributes, "subblocks").value_or(""));
    image.checksum =
        std::string(attribute(attributes, "checksum").value_or(""));

    ThumbnailInfo thumbnail;
    thumbnail.uid = std::string(attribute(attributes, "uid").value_or(""));
    thumbnail.image_index = containing_image;
    thumbnail.image = std::move(image);
    const auto thumbnail_index = state.thumbnails.size();
    state.thumbnails.push_back(std::move(thumbnail));
    state.embedded_thumbnail_blocks.emplace_back();
    state.embedded_thumbnail_data_seen.push_back(false);
    state.open_thumbnail_index = thumbnail_index;
    if (!state.thumbnails.back().uid.empty()) {
      state.thumbnail_uids.emplace(state.thumbnails.back().uid,
                                   thumbnail_index);
    }
    if (containing_image) {
      if (state.thumbnail_binding_events.size() >=
          state.options.max_thumbnail_bindings) {
        state.fail(ErrorCode::resource_limit,
                   "Thumbnail binding limit exceeded", name);
        return;
      }
      state.thumbnail_binding_events.push_back(
          ThumbnailBindingEvent{thumbnail_index, *containing_image});
    }
    return;
  }

  if (is_xisf_element && name == "Data") {
    const auto image_index = state.current_image();
    const bool image_data =
        state.depth == 3 && parent == "Image" && image_index &&
        state.images[*image_index].block.kind == BlockKind::embedded;
    const bool thumbnail_data =
        parent == "Thumbnail" && state.open_thumbnail_index &&
        state.thumbnails[*state.open_thumbnail_index].image.block.kind ==
            BlockKind::embedded;
    if (!image_data && !thumbnail_data) {
      state.fail(ErrorCode::invalid_xisf,
                 "Data must be a direct child of an embedded Image or "
                 "Thumbnail",
                 name);
      return;
    }
    if ((image_data && state.embedded_data_seen[*image_index]) ||
        (thumbnail_data &&
         state.embedded_thumbnail_data_seen[*state.open_thumbnail_index])) {
      state.fail(ErrorCode::invalid_xisf,
                 "Embedded Image or Thumbnail must contain exactly one Data "
                 "element",
                 name);
      return;
    }
    const auto encoding = attribute(attributes, "encoding");
    if (!encoding || (*encoding != "base64" && *encoding != "hex")) {
      state.fail(ErrorCode::invalid_xisf,
                 "Data requires a base64 or hex encoding", name, "encoding");
      return;
    }
    auto &image = image_data
                      ? state.images[*image_index]
                      : state.thumbnails[*state.open_thumbnail_index].image;
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
    if (image_data) {
      state.embedded_data_seen[*image_index] = true;
      state.embedded_image_index = *image_index;
    } else {
      state.embedded_thumbnail_data_seen[*state.open_thumbnail_index] = true;
      state.embedded_thumbnail_index = *state.open_thumbnail_index;
    }
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
    if (image.color_space == "RGB") {
      image.nominal_channel_order =
          NominalChannelOrder::red_green_blue_then_alpha;
    } else if (image.color_space == "CIELab") {
      image.nominal_channel_order = NominalChannelOrder::cie_l_a_b_then_alpha;
    }
    const auto orientation = attribute(attributes, "orientation");
    if (orientation) {
      if (*orientation == "0") {
        image.orientation = ImageOrientation::identity;
      } else if (*orientation == "flip") {
        image.orientation = ImageOrientation::flip;
      } else if (*orientation == "90") {
        image.orientation = ImageOrientation::rotate_90;
      } else if (*orientation == "90;flip") {
        image.orientation = ImageOrientation::rotate_90_flip;
      } else if (*orientation == "-90") {
        image.orientation = ImageOrientation::rotate_minus_90;
      } else if (*orientation == "-90;flip") {
        image.orientation = ImageOrientation::rotate_minus_90_flip;
      } else if (*orientation == "180") {
        image.orientation = ImageOrientation::rotate_180;
      } else if (*orientation == "180;flip") {
        image.orientation = ImageOrientation::rotate_180_flip;
      } else {
        state.fail(ErrorCode::invalid_xisf, "Invalid Image orientation value",
                   name, "orientation");
        return;
      }
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
    const auto location = name == "Property"
                              ? attribute(attributes, "location")
                              : std::optional<std::string_view>{};
    const auto byte_order_attribute = attribute(attributes, "byteOrder");
    const auto compression_attribute = attribute(attributes, "compression");
    const auto subblocks_attribute = attribute(attributes, "subblocks");
    const auto checksum_attribute = attribute(attributes, "checksum");
    if (!identity || identity->empty() ||
        (name == "Property" && (!type || type->empty())) ||
        (name == "FITSKeyword" && (!value || !comment))) {
      state.fail(ErrorCode::invalid_xisf,
                 name + " is missing a mandatory attribute", name);
      return;
    }
    if (name == "FITSKeyword" && !is_valid_fits_keyword_name(*identity)) {
      state.fail(ErrorCode::invalid_xisf,
                 "FITSKeyword name has invalid FITS syntax", name, "name");
      return;
    }
    if (name == "Property" && !is_valid_property_identifier(*identity)) {
      state.fail(ErrorCode::invalid_xisf, "Property id has invalid XISF syntax",
                 name, "id");
      return;
    }
    if (value && value->size() > state.options.max_metadata_value_bytes) {
      state.fail(ErrorCode::resource_limit,
                 "Metadata value exceeds the inspection limit", name, "value");
      return;
    }
    if (name == "Property" && !location &&
        (byte_order_attribute || compression_attribute || subblocks_attribute ||
         checksum_attribute)) {
      state.fail(ErrorCode::invalid_xisf,
                 "Property data-block attributes require a location", name);
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
    entry.scope = entry.image_index      ? MetadataEntry::Scope::image
                  : parent == "Metadata" ? MetadataEntry::Scope::xisf_unit
                                         : MetadataEntry::Scope::standalone;
    entry.uid = std::string(attribute(attributes, "uid").value_or(""));
    entry.name = std::string(*identity);
    entry.type = std::string(attribute(attributes, "type").value_or(""));
    entry.value = std::string(attribute(attributes, "value").value_or(""));
    entry.comment = std::string(attribute(attributes, "comment").value_or(""));
    entry.format = std::string(attribute(attributes, "format").value_or(""));
    const auto byte_order = byte_order_attribute.value_or("little");
    if (byte_order != "little" && byte_order != "big") {
      state.fail(ErrorCode::invalid_xisf, "Invalid Property byteOrder value",
                 name, "byteOrder");
      return;
    }
    entry.byte_order = byte_order == "big" ? ByteOrder::big : ByteOrder::little;
    entry.compression = std::string(compression_attribute.value_or(""));
    entry.subblocks = std::string(subblocks_attribute.value_or(""));
    entry.checksum = std::string(checksum_attribute.value_or(""));
    if (location) {
      entry.block = parse_location(*location);
      if (entry.block.kind == BlockKind::unknown ||
          entry.block.kind == BlockKind::embedded) {
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
    if (name == "Property") {
      const auto category = classify_property_type(*type);
      const bool has_dimensions = entry.length || entry.rows || entry.columns;
      const auto reject_form = [&](std::string message,
                                   std::string attribute_name = {}) {
        state.fail(ErrorCode::invalid_xisf, std::move(message), name,
                   std::move(attribute_name));
      };
      switch (category) {
      case PropertyCategory::scalar_or_complex:
        if (!value || location || has_dimensions) {
          reject_form("Scalar and complex Property values require only a "
                      "value attribute");
          return;
        }
        if (!is_valid_scalar_or_complex_value(*type, *value)) {
          reject_form("Scalar or complex Property value has invalid XISF "
                      "plain-text syntax",
                      "value");
          return;
        }
        break;
      case PropertyCategory::string:
        if (value || has_dimensions) {
          reject_form("String Property values cannot use value or extent "
                      "attributes",
                      value ? "value" : "length");
          return;
        }
        break;
      case PropertyCategory::time_point:
        if (!value || location || has_dimensions || !entry.format.empty()) {
          reject_form("TimePoint Property values require a value attribute "
                      "and cannot use a format or data block");
          return;
        }
        if (!is_valid_time_point_value(*value)) {
          reject_form("TimePoint Property value has invalid ISO 8601 "
                      "extended syntax",
                      "value");
          return;
        }
        break;
      case PropertyCategory::vector:
        if (value || !location || !entry.length || entry.rows ||
            entry.columns) {
          reject_form("Vector Property values require length and location "
                      "attributes only");
          return;
        }
        break;
      case PropertyCategory::matrix:
        if (value || !location || entry.length || !entry.rows ||
            !entry.columns) {
          reject_form("Matrix Property values require rows, columns, and "
                      "location attributes only");
          return;
        }
        break;
      case PropertyCategory::unknown:
        reject_form("Property declares an unknown XISF type", "type");
        return;
      }
    }
    const auto metadata_index = state.metadata.size();
    if (!entry.uid.empty()) {
      state.metadata_uids.emplace(entry.uid, metadata_index);
    }
    state.metadata.push_back(std::move(entry));
    state.inline_metadata_blocks.emplace_back();
    if (state.metadata.back().scope != MetadataEntry::Scope::standalone) {
      if (state.metadata_binding_events.size() >=
          state.options.max_metadata_entries) {
        state.fail(ErrorCode::resource_limit, "Metadata binding limit exceeded",
                   name);
        return;
      }
      MetadataBindingEvent event;
      event.metadata_index = metadata_index;
      event.scope = state.metadata.back().scope == MetadataEntry::Scope::image
                        ? MetadataBinding::Scope::image
                        : MetadataBinding::Scope::xisf_unit;
      event.image_index = state.metadata.back().image_index;
      state.metadata_binding_events.push_back(std::move(event));
    }
    if (name == "Property" && !value && !location) {
      state.text_metadata_index = metadata_index;
    } else if (name == "Property" &&
               state.metadata.back().block.kind == BlockKind::inline_data) {
      const auto &raw = state.metadata.back().block.raw;
      if (raw != "inline:base64" && raw != "inline:hex") {
        state.fail(ErrorCode::unsupported_feature,
                   "Unsupported inline Property block encoding", name,
                   "location");
        return;
      }
      state.inline_metadata_index = metadata_index;
      state.embedded_encoding = raw == "inline:base64"
                                    ? XmlBuilder::EmbeddedEncoding::base64
                                    : XmlBuilder::EmbeddedEncoding::hex;
      state.base64_quartet_size = 0;
      state.base64_complete = false;
      state.hex_high_nibble.reset();
      state.encoded_block_bytes = 0;
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
    if (!state.embedded_image_index && !state.embedded_thumbnail_index) {
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
    state.embedded_thumbnail_index.reset();
    state.embedded_encoding = XmlBuilder::EmbeddedEncoding::none;
  } else if (is_xisf_element && name == "ICCProfile") {
    if (state.inline_icc_profile_index) {
      if ((state.embedded_encoding == XmlBuilder::EmbeddedEncoding::base64 &&
           state.base64_quartet_size != 0) ||
          (state.embedded_encoding == XmlBuilder::EmbeddedEncoding::hex &&
           state.hex_high_nibble)) {
        state.fail(
            ErrorCode::invalid_xisf,
            "Inline ICCProfile block has an incomplete encoded byte sequence",
            name);
        return;
      }
      state.inline_icc_profile_index.reset();
      state.embedded_encoding = XmlBuilder::EmbeddedEncoding::none;
    }
  } else if (is_xisf_element && name == "Property") {
    if (state.inline_metadata_index) {
      if ((state.embedded_encoding == XmlBuilder::EmbeddedEncoding::base64 &&
           state.base64_quartet_size != 0) ||
          (state.embedded_encoding == XmlBuilder::EmbeddedEncoding::hex &&
           state.hex_high_nibble)) {
        state.fail(
            ErrorCode::invalid_xisf,
            "Inline Property block has an incomplete encoded byte sequence",
            name);
        return;
      }
      state.inline_metadata_index.reset();
      state.embedded_encoding = XmlBuilder::EmbeddedEncoding::none;
    }
    state.text_metadata_index.reset();
  } else if (is_xisf_element && name == "Cell") {
    if (!state.open_table_cell_index) {
      state.fail(ErrorCode::invalid_xisf,
                 "Unexpected closing Cell element", name);
      return;
    }
    state.open_table_cell_index.reset();
  } else if (is_xisf_element && name == "Row") {
    if (!state.open_table_index || !state.open_table_row_index) {
      state.fail(ErrorCode::invalid_xisf,
                 "Unexpected closing Row element", name);
      return;
    }
    const auto &row = state.tables[*state.open_table_index]
                          .rows[*state.open_table_row_index];
    if (row.cells.empty()) {
      state.fail(ErrorCode::invalid_xisf,
                 "Table Row must contain at least one Cell", name);
      return;
    }
    state.open_table_row_index.reset();
  } else if (is_xisf_element && name == "Structure") {
    if (!state.open_structure_index) {
      state.fail(ErrorCode::invalid_xisf,
                 "Unexpected closing Structure element", name);
      return;
    }
    if (state.table_structures[*state.open_structure_index].fields.empty()) {
      state.fail(ErrorCode::invalid_xisf,
                 "Structure must contain at least one Field", name);
      return;
    }
    state.open_structure_index.reset();
  } else if (is_xisf_element && name == "Table") {
    if (!state.open_table_index) {
      state.fail(ErrorCode::invalid_xisf,
                 "Unexpected closing Table element", name);
      return;
    }
    state.open_table_index.reset();
  } else if (is_xisf_element && name == "Thumbnail") {
    if (!state.open_thumbnail_index) {
      state.fail(ErrorCode::invalid_xisf,
                 "Unexpected closing Thumbnail element", name);
      return;
    }
    const auto thumbnail_index = *state.open_thumbnail_index;
    if (state.thumbnails[thumbnail_index].image.block.kind ==
            BlockKind::embedded &&
        !state.embedded_thumbnail_data_seen[thumbnail_index]) {
      state.fail(ErrorCode::invalid_xisf,
                 "Embedded Thumbnail requires exactly one Data child", name);
      return;
    }
    state.open_thumbnail_index.reset();
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
  if (!state.qualified_element_stack.empty()) {
    state.qualified_element_stack.pop_back();
  }
  if (!state.extension_stack.empty()) {
    state.extension_stack.pop_back();
  }
}

void XMLCALL character_data(void *user_data, const XML_Char *text, int length) {
  auto &state = *static_cast<XmlBuilder *>(user_data);
  if (state.error || length <= 0) {
    return;
  }
  const std::string_view data(text, static_cast<std::size_t>(length));
  if (state.embedded_image_index || state.inline_metadata_index ||
      state.inline_icc_profile_index || state.embedded_thumbnail_index) {
    decode_embedded_text(state, data);
    return;
  }
  if (state.open_table_index && state.open_table_row_index &&
      state.open_table_cell_index) {
    auto &cell = state.tables[*state.open_table_index]
                     .rows[*state.open_table_row_index]
                     .cells[*state.open_table_cell_index];
    const bool accepts_text =
        cell.value_form == TableCellInfo::ValueForm::character_data ||
        (cell.value_form == TableCellInfo::ValueForm::data_block &&
         cell.block.kind == BlockKind::inline_data);
    if (!accepts_text) {
      if (contains_non_xml_whitespace(data)) {
        state.fail(ErrorCode::invalid_xisf,
                   "Cell form cannot contain character data", "Cell");
      }
      return;
    }
    if (!consume_table_text_bytes(state, static_cast<std::size_t>(length),
                                  "Cell")) {
      return;
    }
    cell.value.append(text, static_cast<std::size_t>(length));
    return;
  }
  if (!state.extension_stack.empty() && state.extension_stack.back()) {
    const auto extension_index = *state.extension_stack.back();
    if (!consume_extension_bytes(
            state, static_cast<std::size_t>(length),
            state.extension_elements[extension_index].name)) {
      return;
    }
    state.extension_elements[extension_index].text.append(
        text, static_cast<std::size_t>(length));
    return;
  }
  if (!state.qualified_element_stack.empty() &&
      state.qualified_element_stack.back().namespace_uri == kXisfNamespace &&
      ancillary_kind(state.qualified_element_stack.back().name) &&
      contains_non_xml_whitespace(data)) {
    state.fail(ErrorCode::invalid_xisf,
               "Attribute-based ancillary metadata cannot contain text",
               state.qualified_element_stack.back().name);
    return;
  }
  if (!state.qualified_element_stack.empty() &&
      state.qualified_element_stack.back().namespace_uri == kXisfNamespace &&
      state.qualified_element_stack.back().name == "ICCProfile" &&
      contains_non_xml_whitespace(data)) {
    state.fail(ErrorCode::invalid_xisf,
               "Non-inline ICCProfile cannot contain character data",
               "ICCProfile");
    return;
  }
  if (!state.text_metadata_index) {
    if (state.depth == 1 && !state.element_stack.empty() &&
        state.element_stack.back() == "xisf" &&
        contains_non_xml_whitespace(data)) {
      state.fail(ErrorCode::invalid_xisf,
                 "The XISF root cannot contain character data", "xisf");
    } else if (state.open_thumbnail_index &&
               contains_non_xml_whitespace(data)) {
      state.fail(ErrorCode::invalid_xisf,
                 "Thumbnail cannot contain text outside Data", "Thumbnail");
    } else if ((state.open_table_row_index || state.open_structure_index ||
                state.open_table_index) &&
               contains_non_xml_whitespace(data)) {
      state.fail(ErrorCode::invalid_xisf,
                 "Table, Structure, and Row cannot contain text outside Cell",
                 state.open_table_row_index   ? "Row"
                 : state.open_structure_index ? "Structure"
                                              : "Table");
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
  std::vector<std::vector<std::byte>> inline_metadata_blocks;
  std::vector<std::vector<std::byte>> inline_icc_profile_blocks;
  std::vector<std::vector<std::byte>> embedded_thumbnail_blocks;
  std::vector<AttachedRange> attached_ranges;
};

Result<ParsedHeader> parse_header(std::string_view xml,
                                  const ReaderOptions &options,
                                  std::uint64_t file_size,
                                  std::uint32_t header_length) {
  if (!xml.starts_with("<?xml")) {
    return make_error(ErrorCode::invalid_xisf,
                      "XISF header must begin with an XML declaration");
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
  XML_SetXmlDeclHandler(state.parser, xml_declaration);

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
  if (!state.saw_xml_declaration) {
    return make_error(ErrorCode::invalid_xisf,
                      "XISF header is missing its XML declaration");
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
  for (const auto &reference : state.references_from_thumbnails) {
    if (state.thumbnail_uids.contains(reference)) {
      return make_error(
          ErrorCode::invalid_xisf,
          "Thumbnail cannot contain a Reference to another Thumbnail");
    }
  }
  for (const auto &structure : state.table_structures) {
    std::unordered_set<std::string> field_ids;
    for (const auto &field : structure.fields) {
      if (!field_ids.emplace(field.id).second) {
        return make_error(ErrorCode::invalid_xisf,
                          "Table field identifiers must be unique");
      }
    }
  }
  std::vector<std::size_t> table_structure_reference_counts(
      state.tables.size());
  for (const auto &event : state.table_structure_references) {
    if (event.table_index >= state.tables.size()) {
      return make_error(ErrorCode::internal_error,
                        "Table structure reference is outside the document");
    }
    auto &count = table_structure_reference_counts[event.table_index];
    ++count;
    if (count > 1 || state.tables[event.table_index].structure_index) {
      return make_error(
          ErrorCode::invalid_xisf,
          "Table requires exactly one inline or referenced Structure");
    }
    const auto target = state.standalone_structure_uids.find(event.reference);
    if (target == state.standalone_structure_uids.end()) {
      return make_error(
          ErrorCode::invalid_xisf,
          "Table Reference must target a standalone Structure");
    }
    state.tables[event.table_index].structure_index = target->second;
    state.tables[event.table_index].structure_by_reference = true;
  }
  for (auto &table : state.tables) {
    if (!table.structure_index ||
        *table.structure_index >= state.table_structures.size()) {
      return make_error(
          ErrorCode::invalid_xisf,
          "Table requires exactly one inline or referenced Structure");
    }
    const auto &fields = state.table_structures[*table.structure_index].fields;
    if (table.declared_rows && *table.declared_rows != table.rows.size()) {
      return make_error(ErrorCode::invalid_xisf,
                        "Table rows attribute does not match its Row count");
    }
    if (table.declared_columns &&
        *table.declared_columns != fields.size()) {
      return make_error(
          ErrorCode::invalid_xisf,
          "Table columns attribute does not match its Structure field count");
    }
    for (const auto &row : table.rows) {
      if (row.cells.size() != fields.size()) {
        return make_error(
            ErrorCode::invalid_xisf,
            "Table Row cell count does not match its Structure field count");
      }
      for (std::size_t column = 0; column < row.cells.size(); ++column) {
        const auto &cell = row.cells[column];
        const auto category = classify_property_type(fields[column].type);
        const bool has_extents = cell.length || cell.rows || cell.columns;
        switch (category) {
        case PropertyCategory::scalar_or_complex:
          if (cell.value_form != TableCellInfo::ValueForm::attribute ||
              has_extents ||
              !is_valid_scalar_or_complex_value(fields[column].type,
                                                cell.value)) {
            return make_error(
                ErrorCode::invalid_xisf,
                "Scalar or complex Table Cell has an invalid value form");
          }
          break;
        case PropertyCategory::string:
          if (has_extents) {
            return make_error(ErrorCode::invalid_xisf,
                              "String Table Cell cannot declare extents");
          }
          break;
        case PropertyCategory::time_point:
          if (cell.value_form != TableCellInfo::ValueForm::attribute ||
              has_extents || !is_valid_time_point_value(cell.value)) {
            return make_error(ErrorCode::invalid_xisf,
                              "TimePoint Table Cell has an invalid value");
          }
          break;
        case PropertyCategory::vector:
          if (cell.value_form != TableCellInfo::ValueForm::data_block ||
              !cell.length || cell.rows || cell.columns) {
            return make_error(
                ErrorCode::invalid_xisf,
                "Vector Table Cell requires length and location");
          }
          break;
        case PropertyCategory::matrix:
          if (cell.value_form != TableCellInfo::ValueForm::data_block ||
              cell.length || !cell.rows || !cell.columns) {
            return make_error(
                ErrorCode::invalid_xisf,
                "Matrix Table Cell requires rows, columns, and location");
          }
          break;
        case PropertyCategory::unknown:
          return make_error(ErrorCode::invalid_xisf,
                            "Table Field declares an unknown property type");
        }
      }
    }
  }
  std::vector<MetadataBinding> metadata_bindings;
  metadata_bindings.reserve(state.metadata_binding_events.size());
  for (const auto &event : state.metadata_binding_events) {
    if (event.by_reference &&
        state.table_structure_uids.contains(event.reference)) {
      return make_error(
          ErrorCode::invalid_xisf,
          "Structure References can only be direct children of Table");
    }
    auto metadata_index = event.metadata_index;
    if (!metadata_index) {
      const auto target = state.metadata_uids.find(event.reference);
      if (target == state.metadata_uids.end()) {
        continue;
      }
      metadata_index = target->second;
    }
    if (*metadata_index >= state.metadata.size()) {
      return make_error(ErrorCode::internal_error,
                        "Metadata binding index is outside the document");
    }
    if (event.scope == MetadataBinding::Scope::xisf_unit &&
        state.metadata[*metadata_index].kind ==
            MetadataEntry::Kind::fits_keyword) {
      return make_error(
          ErrorCode::invalid_xisf,
          "FITSKeyword references can only associate keywords with images");
    }
    MetadataBinding binding;
    binding.metadata_index = *metadata_index;
    binding.scope = event.scope;
    binding.image_index = event.image_index;
    binding.by_reference = event.by_reference;
    metadata_bindings.push_back(std::move(binding));
  }
  std::vector<AncillaryBinding> ancillary_bindings;
  ancillary_bindings.reserve(state.ancillary_binding_events.size());
  for (const auto &event : state.ancillary_binding_events) {
    if (event.object_index >= state.ancillary_objects.size() ||
        event.image_index >= state.images.size()) {
      return make_error(ErrorCode::internal_error,
                        "Ancillary binding is outside the document");
    }
    ancillary_bindings.push_back(
        AncillaryBinding{event.object_index, event.image_index, false});
  }
  for (const auto &event : state.metadata_binding_events) {
    if (!event.by_reference || event.scope != MetadataBinding::Scope::image ||
        !event.image_index) {
      continue;
    }
    const auto target = state.ancillary_uids.find(event.reference);
    if (target == state.ancillary_uids.end()) {
      continue;
    }
    if (ancillary_bindings.size() >= options.max_ancillary_bindings) {
      return make_error(ErrorCode::resource_limit,
                        "Ancillary binding limit exceeded");
    }
    ancillary_bindings.push_back(
        AncillaryBinding{target->second, *event.image_index, true});
  }
  std::vector<IccProfileBinding> icc_profile_bindings;
  icc_profile_bindings.reserve(state.icc_profile_binding_events.size());
  for (const auto &event : state.icc_profile_binding_events) {
    if (event.profile_index >= state.icc_profiles.size() ||
        event.image_index >= state.images.size()) {
      return make_error(ErrorCode::internal_error,
                        "ICC profile binding is outside the document");
    }
    icc_profile_bindings.push_back(
        IccProfileBinding{event.profile_index, event.image_index, false});
  }
  for (const auto &event : state.metadata_binding_events) {
    if (!event.by_reference || event.scope != MetadataBinding::Scope::image ||
        !event.image_index) {
      continue;
    }
    const auto target = state.icc_profile_uids.find(event.reference);
    if (target == state.icc_profile_uids.end()) {
      continue;
    }
    if (icc_profile_bindings.size() >= options.max_icc_profile_bindings) {
      return make_error(ErrorCode::resource_limit,
                        "ICC profile binding limit exceeded");
    }
    icc_profile_bindings.push_back(
        IccProfileBinding{target->second, *event.image_index, true});
  }
  std::vector<ThumbnailBinding> thumbnail_bindings;
  thumbnail_bindings.reserve(state.thumbnail_binding_events.size());
  for (const auto &event : state.thumbnail_binding_events) {
    if (event.thumbnail_index >= state.thumbnails.size() ||
        event.image_index >= state.images.size()) {
      return make_error(ErrorCode::internal_error,
                        "Thumbnail binding is outside the document");
    }
    thumbnail_bindings.push_back(
        ThumbnailBinding{event.thumbnail_index, event.image_index, false});
  }
  for (const auto &event : state.metadata_binding_events) {
    if (!event.by_reference || event.scope != MetadataBinding::Scope::image ||
        !event.image_index) {
      continue;
    }
    const auto target = state.thumbnail_uids.find(event.reference);
    if (target == state.thumbnail_uids.end()) {
      continue;
    }
    if (thumbnail_bindings.size() >= options.max_thumbnail_bindings) {
      return make_error(ErrorCode::resource_limit,
                        "Thumbnail binding limit exceeded");
    }
    thumbnail_bindings.push_back(
        ThumbnailBinding{target->second, *event.image_index, true});
  }
  std::vector<TableBinding> table_bindings;
  table_bindings.reserve(state.table_binding_events.size());
  for (const auto &event : state.table_binding_events) {
    if (event.table_index >= state.tables.size() ||
        event.image_index >= state.images.size()) {
      return make_error(ErrorCode::internal_error,
                        "Table binding is outside the document");
    }
    table_bindings.push_back(
        TableBinding{event.table_index, event.image_index, false});
  }
  for (const auto &event : state.metadata_binding_events) {
    if (!event.by_reference) {
      continue;
    }
    const auto target = state.table_uids.find(event.reference);
    if (target == state.table_uids.end()) {
      continue;
    }
    if (event.scope != MetadataBinding::Scope::image || !event.image_index) {
      return make_error(ErrorCode::invalid_xisf,
                        "Table References can only associate Tables with "
                        "Images");
    }
    if (table_bindings.size() >= options.max_table_bindings) {
      return make_error(ErrorCode::resource_limit,
                        "Table binding limit exceeded");
    }
    table_bindings.push_back(
        TableBinding{target->second, *event.image_index, true});
  }
  std::unordered_set<std::string> unit_property_ids;
  std::vector<std::unordered_set<std::string>> image_property_ids(
      state.images.size());
  for (const auto &binding : metadata_bindings) {
    const auto &entry = state.metadata[binding.metadata_index];
    if (entry.kind != MetadataEntry::Kind::property) {
      continue;
    }
    bool inserted = false;
    if (binding.scope == MetadataBinding::Scope::xisf_unit) {
      inserted = unit_property_ids.emplace(entry.name).second;
    } else {
      if (!binding.image_index || *binding.image_index >= state.images.size()) {
        return make_error(ErrorCode::internal_error,
                          "Image metadata binding has no valid image index");
      }
      inserted =
          image_property_ids[*binding.image_index].emplace(entry.name).second;
    }
    if (!inserted) {
      return make_error(
          ErrorCode::invalid_xisf,
          "Property identifiers must be unique within each association");
    }
  }
  for (const auto &binding : table_bindings) {
    if (binding.table_index >= state.tables.size() ||
        binding.image_index >= image_property_ids.size() ||
        !image_property_ids[binding.image_index]
             .emplace(state.tables[binding.table_index].id)
             .second) {
      return make_error(
          ErrorCode::invalid_xisf,
          "Property identifiers must be unique within each association");
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
  Document document(
      std::move(state.version), std::move(state.images),
      std::move(state.metadata), file_size, header_length,
      std::move(metadata_bindings), std::move(state.extension_elements),
      std::move(state.ancillary_objects), std::move(ancillary_bindings),
      std::move(state.icc_profiles), std::move(icc_profile_bindings),
      std::move(state.thumbnails), std::move(thumbnail_bindings),
      std::move(state.table_structures), std::move(state.tables),
      std::move(table_bindings));
  return ParsedHeader{std::move(document),
                      std::move(state.embedded_blocks),
                      std::move(state.inline_metadata_blocks),
                      std::move(state.inline_icc_profile_blocks),
                      std::move(state.embedded_thumbnail_blocks),
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
  case SampleFormat::uint64:
    return 8;
  case SampleFormat::float32:
    return 4;
  case SampleFormat::float64:
    return 8;
  case SampleFormat::complex32:
    return 8;
  case SampleFormat::complex64:
    return 16;
  case SampleFormat::unsupported:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<std::uint64_t> endian_component_size(SampleFormat format) {
  switch (format) {
  case SampleFormat::uint8:
    return 1;
  case SampleFormat::uint16:
    return 2;
  case SampleFormat::uint32:
  case SampleFormat::float32:
  case SampleFormat::complex32:
    return 4;
  case SampleFormat::uint64:
  case SampleFormat::float64:
  case SampleFormat::complex64:
    return 8;
  case SampleFormat::unsupported:
    return std::nullopt;
  }
  return std::nullopt;
}

enum class CompressionCodec { none, zlib, lz4, lz4hc, zstd };

struct CompressionSubblock {
  std::uint64_t compressed_size{0};
  std::uint64_t uncompressed_size{0};
};

struct CompressionPlan {
  CompressionCodec codec{CompressionCodec::none};
  bool byte_shuffled{false};
  std::uint64_t uncompressed_size{0};
  std::uint64_t item_size{1};
  std::uint64_t max_zstd_window_bytes{0};
  std::vector<CompressionSubblock> subblocks;
};

enum class ChecksumAlgorithm { none, sha1, sha256, sha512, sha3_256, sha3_512 };

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

Result<ChecksumPlan> parse_checksum_plan(std::string_view checksum) {
  if (checksum.empty()) {
    return ChecksumPlan{};
  }
  const auto separator = checksum.find(':');
  if (separator == std::string::npos ||
      checksum.find(':', separator + 1) != std::string::npos) {
    return make_error(ErrorCode::invalid_block, "Invalid checksum descriptor");
  }
  const auto algorithm = checksum.substr(0, separator);
  const auto encoded_digest = checksum.substr(separator + 1);
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
  } else if (algorithm == "sha3-256") {
    plan.algorithm = ChecksumAlgorithm::sha3_256;
    digest_size = 32;
  } else if (algorithm == "sha3-512") {
    plan.algorithm = ChecksumAlgorithm::sha3_512;
    digest_size = 64;
  } else {
    return make_error(ErrorCode::unsupported_feature,
                      "Unsupported block checksum algorithm");
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

Result<CompressionPlan> parse_compression_plan(
    std::string_view compression, std::string_view subblocks,
    std::uint64_t serialized_bytes, std::optional<std::uint64_t> expected_bytes,
    const ReaderOptions &options, std::uint64_t item_size,
    std::uint64_t max_serialized_bytes, std::uint64_t max_decoded_bytes) {
  if (serialized_bytes > max_serialized_bytes) {
    return make_error(ErrorCode::resource_limit,
                      "Serialized block exceeds the configured limit");
  }
  if (serialized_bytes > std::numeric_limits<std::size_t>::max()) {
    return make_error(ErrorCode::resource_limit,
                      "Serialized block cannot fit in addressable memory");
  }
  if (compression.empty()) {
    if (!subblocks.empty()) {
      return make_error(ErrorCode::invalid_block,
                        "Compression subblocks require a compression codec");
    }
    if (expected_bytes && serialized_bytes != *expected_bytes) {
      return make_error(ErrorCode::invalid_block,
                        "Block size does not match its declared typed extent");
    }
    if (serialized_bytes > max_decoded_bytes) {
      return make_error(ErrorCode::resource_limit,
                        "Decoded block exceeds the configured limit");
    }
    CompressionPlan plan;
    plan.uncompressed_size = serialized_bytes;
    return plan;
  }

  std::array<std::string_view, 3> tokens{};
  std::size_t token_count = 0;
  std::size_t start = 0;
  while (start <= compression.size()) {
    if (token_count == tokens.size()) {
      return make_error(ErrorCode::invalid_block,
                        "Compression descriptor has too many components");
    }
    const auto end = compression.find(':', start);
    tokens[token_count++] = compression.substr(
        start,
        end == std::string::npos ? compression.size() - start : end - start);
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  CompressionPlan plan;
  plan.max_zstd_window_bytes = options.max_zstd_window_bytes;
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
  } else if (tokens[0] == "zstd") {
    plan.codec = CompressionCodec::zstd;
  } else if (tokens[0] == "zstd+sh") {
    plan.codec = CompressionCodec::zstd;
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
  if (expected_bytes && plan.uncompressed_size != *expected_bytes) {
    return make_error(
        ErrorCode::invalid_block,
        "Declared uncompressed size does not match the typed extent");
  }
  if (plan.byte_shuffled &&
      (!parse_unsigned(tokens[2], plan.item_size) || plan.item_size == 0 ||
       plan.item_size != item_size)) {
    return make_error(
        ErrorCode::invalid_block,
        "Byte-shuffle item size does not match the typed element size");
  }
  if (serialized_bytes == 0) {
    return make_error(ErrorCode::invalid_block,
                      "Compressed block cannot be empty");
  }
  std::uint64_t maximum_output = 0;
  if (!checked_multiply(serialized_bytes, options.max_decompression_ratio,
                        maximum_output)) {
    maximum_output = std::numeric_limits<std::uint64_t>::max();
  }
  if (plan.uncompressed_size > maximum_output) {
    return make_error(ErrorCode::resource_limit,
                      "Block exceeds the configured decompression ratio");
  }
  if (plan.uncompressed_size > max_decoded_bytes ||
      plan.uncompressed_size > std::numeric_limits<std::size_t>::max()) {
    return make_error(ErrorCode::resource_limit,
                      "Decoded block exceeds the configured limit");
  }

  if (subblocks.empty()) {
    plan.subblocks.push_back(
        CompressionSubblock{serialized_bytes, plan.uncompressed_size});
    return plan;
  }

  std::uint64_t total_compressed = 0;
  std::uint64_t total_uncompressed = 0;
  start = 0;
  while (start <= subblocks.size()) {
    if (plan.subblocks.size() >= options.max_compressed_subblocks) {
      return make_error(
          ErrorCode::resource_limit,
          "Compressed subblock count exceeds the configured limit");
    }
    const auto end = subblocks.find(':', start);
    const auto pair = subblocks.substr(start, end == std::string::npos
                                                  ? subblocks.size() - start
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
      total_uncompressed != plan.uncompressed_size) {
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
  std::uint64_t endian_component_size{0};
  std::uint64_t expected_bytes{0};
  std::uint64_t serialized_bytes{0};
  CompressionPlan compression;
  ChecksumPlan checksum;
};

struct PropertyReadPlan {
  const MetadataEntry *property{nullptr};
  const std::vector<std::byte> *inline_block{nullptr};
  PropertyElementLayout layout;
  std::uint64_t expected_bytes{0};
  std::uint64_t serialized_bytes{0};
  CompressionPlan compression;
  ChecksumPlan checksum;
};

struct IccProfileReadPlan {
  const IccProfileInfo *profile{nullptr};
  const std::vector<std::byte> *inline_block{nullptr};
  std::uint64_t expected_bytes{0};
  std::uint64_t serialized_bytes{0};
  CompressionPlan compression;
  ChecksumPlan checksum;
};

Result<IccProfileReadPlan>
plan_icc_profile_read(const Document &document, const ReaderOptions &options,
                      const std::vector<std::vector<std::byte>> &inline_blocks,
                      std::size_t profile_index) {
  if (profile_index >= document.icc_profiles().size()) {
    return make_error(ErrorCode::invalid_argument,
                      "ICC profile index is outside the document");
  }
  const auto &profile = document.icc_profiles()[profile_index];
  if (profile.block.kind != BlockKind::attachment &&
      profile.block.kind != BlockKind::inline_data) {
    return make_error(
        ErrorCode::unsupported_feature,
        "Only attachment and inline ICC profile blocks are readable");
  }
  const std::vector<std::byte> *inline_block = nullptr;
  std::uint64_t serialized_bytes = profile.block.size;
  if (profile.block.kind == BlockKind::inline_data) {
    if (profile_index >= inline_blocks.size()) {
      return make_error(ErrorCode::internal_error,
                        "Inline ICC profile storage is inconsistent");
    }
    inline_block = &inline_blocks[profile_index];
    serialized_bytes = static_cast<std::uint64_t>(inline_block->size());
  } else if (profile.block.offset > document.file_size() ||
             profile.block.size > document.file_size() - profile.block.offset) {
    return make_error(ErrorCode::invalid_block,
                      "ICC profile attachment range extends beyond the source");
  }
  auto compression = parse_compression_plan(
      profile.compression, profile.subblocks, serialized_bytes, std::nullopt,
      options, 1, options.max_serialized_icc_profile_bytes,
      options.max_decoded_icc_profile_bytes);
  if (!compression) {
    return compression.error();
  }
  auto checksum = parse_checksum_plan(profile.checksum);
  if (!checksum) {
    return checksum.error();
  }
  auto compression_plan = std::move(compression).value();
  auto checksum_plan = std::move(checksum).value();
  const auto expected_bytes = compression_plan.uncompressed_size;
  return IccProfileReadPlan{&profile,
                            inline_block,
                            expected_bytes,
                            serialized_bytes,
                            std::move(compression_plan),
                            std::move(checksum_plan)};
}

Result<PropertyReadPlan>
plan_property_read(const Document &document, const ReaderOptions &options,
                   const std::vector<std::vector<std::byte>> &inline_blocks,
                   std::size_t metadata_index) {
  if (metadata_index >= document.metadata().size()) {
    return make_error(ErrorCode::invalid_argument,
                      "Metadata index is outside the document");
  }
  const auto &property = document.metadata()[metadata_index];
  if (property.kind != MetadataEntry::Kind::property ||
      property.value_form != MetadataEntry::ValueForm::data_block) {
    return make_error(ErrorCode::invalid_argument,
                      "Metadata entry is not a block-backed Property");
  }
  if (property.block.kind != BlockKind::attachment &&
      property.block.kind != BlockKind::inline_data) {
    return make_error(
        ErrorCode::unsupported_feature,
        "Only attachment and inline Property blocks are readable");
  }
  const auto layout = property_element_layout(property.type);
  if (!layout) {
    return make_error(ErrorCode::unsupported_feature,
                      "Property binary element type is not supported");
  }
  std::optional<std::uint64_t> typed_bytes;
  if (!layout->string_data) {
    std::uint64_t element_count = 0;
    const auto category = classify_property_type(property.type);
    if (category == PropertyCategory::vector && property.length) {
      element_count = *property.length;
    } else if (category == PropertyCategory::matrix && property.rows &&
               property.columns &&
               checked_multiply(*property.rows, *property.columns,
                                element_count)) {
      // element_count was computed above.
    } else {
      return make_error(ErrorCode::invalid_block,
                        "Property typed extent is incomplete or overflows");
    }
    std::uint64_t byte_count = 0;
    if (!checked_multiply(element_count, layout->element_size, byte_count)) {
      return make_error(ErrorCode::overflow,
                        "Property typed extent overflows its byte size");
    }
    typed_bytes = byte_count;
  }
  const std::vector<std::byte> *inline_block = nullptr;
  std::uint64_t serialized_bytes = property.block.size;
  if (property.block.kind == BlockKind::inline_data) {
    if (metadata_index >= inline_blocks.size()) {
      return make_error(ErrorCode::internal_error,
                        "Inline Property storage is inconsistent");
    }
    inline_block = &inline_blocks[metadata_index];
    serialized_bytes = static_cast<std::uint64_t>(inline_block->size());
  } else if (property.block.offset > document.file_size() ||
             property.block.size >
                 document.file_size() - property.block.offset) {
    return make_error(ErrorCode::invalid_block,
                      "Property attachment range extends beyond the source");
  }
  auto compression = parse_compression_plan(
      property.compression, property.subblocks, serialized_bytes, typed_bytes,
      options, layout->element_size, options.max_serialized_property_bytes,
      options.max_decoded_property_bytes);
  if (!compression) {
    return compression.error();
  }
  auto checksum = parse_checksum_plan(property.checksum);
  if (!checksum) {
    return checksum.error();
  }
  auto compression_plan = std::move(compression).value();
  auto checksum_plan = std::move(checksum).value();
  const auto expected_bytes = compression_plan.uncompressed_size;
  return PropertyReadPlan{&property,
                          inline_block,
                          *layout,
                          expected_bytes,
                          serialized_bytes,
                          std::move(compression_plan),
                          std::move(checksum_plan)};
}

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
                      "The reader only reads attachment and embedded image "
                      "blocks");
  }
  if (image.geometry.size() != 3) {
    return make_error(ErrorCode::unsupported_feature,
                      "The current reader profile only reads 2-D images");
  }
  if (image.color_space == "CIELab") {
    return make_error(ErrorCode::unsupported_feature,
                      "CIELab conversion is outside the M2 reader profile");
  }
  const auto sample_size = bytes_per_sample(image.sample_format);
  const auto component_size = endian_component_size(image.sample_format);
  if (!sample_size || !component_size) {
    return make_error(ErrorCode::unsupported_feature,
                      "The reader does not decode this image sample format");
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
      image.compression, image.subblocks, serialized_bytes, expected_bytes,
      options, *sample_size, options.max_serialized_image_bytes,
      options.max_decoded_image_bytes);
  if (!compression) {
    return compression.error();
  }
  auto checksum = parse_checksum_plan(image.checksum);
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
                       *component_size,
                       expected_bytes,
                       serialized_bytes,
                       std::move(compression).value(),
                       std::move(checksum).value()};
}

Result<ImageReadPlan>
plan_thumbnail_read(const Document &document, const ReaderOptions &options,
                    const std::vector<std::vector<std::byte>> &embedded_blocks,
                    std::size_t thumbnail_index) {
  if (thumbnail_index >= document.thumbnails().size()) {
    return make_error(ErrorCode::invalid_argument,
                      "Thumbnail index is outside the document");
  }
  const auto &image = document.thumbnails()[thumbnail_index].image;
  if (image.block.kind != BlockKind::attachment &&
      image.block.kind != BlockKind::embedded) {
    return make_error(
        ErrorCode::unsupported_feature,
        "Only attachment and embedded Thumbnail blocks are readable");
  }
  if (image.geometry.size() != 3) {
    return make_error(ErrorCode::invalid_block,
                      "Thumbnail geometry is not two-dimensional");
  }
  const auto sample_size = bytes_per_sample(image.sample_format);
  const auto component_size = endian_component_size(image.sample_format);
  if (!sample_size || !component_size) {
    return make_error(ErrorCode::unsupported_feature,
                      "Thumbnail sample format is not readable");
  }
  const std::uint64_t channels = image.geometry[2];
  std::uint64_t sample_count = 0;
  std::uint64_t expected_bytes = 0;
  if (!checked_multiply(image.geometry[0], image.geometry[1], sample_count) ||
      !checked_multiply(sample_count, channels, sample_count) ||
      !checked_multiply(sample_count, *sample_size, expected_bytes)) {
    return make_error(ErrorCode::overflow,
                      "Thumbnail geometry overflows its byte size");
  }
  if (expected_bytes > options.max_decoded_thumbnail_bytes ||
      expected_bytes > std::numeric_limits<std::size_t>::max()) {
    return make_error(ErrorCode::resource_limit,
                      "Thumbnail exceeds the decoded byte limit");
  }
  if (image.block.kind == BlockKind::embedded &&
      thumbnail_index >= embedded_blocks.size()) {
    return make_error(ErrorCode::internal_error,
                      "Embedded Thumbnail storage is inconsistent");
  }
  const std::vector<std::byte> *embedded_block = nullptr;
  const std::uint64_t serialized_bytes =
      image.block.kind == BlockKind::attachment
          ? image.block.size
          : static_cast<std::uint64_t>(embedded_blocks[thumbnail_index].size());
  auto compression = parse_compression_plan(
      image.compression, image.subblocks, serialized_bytes, expected_bytes,
      options, *sample_size, options.max_serialized_thumbnail_bytes,
      options.max_decoded_thumbnail_bytes);
  if (!compression) {
    return compression.error();
  }
  auto checksum = parse_checksum_plan(image.checksum);
  if (!checksum) {
    return checksum.error();
  }
  if (image.block.kind == BlockKind::attachment &&
      (image.block.offset > document.file_size() ||
       image.block.size > document.file_size() - image.block.offset)) {
    return make_error(ErrorCode::invalid_block,
                      "Thumbnail attachment extends beyond the source");
  }
  if (image.block.kind == BlockKind::embedded) {
    embedded_block = &embedded_blocks[thumbnail_index];
  }
  return ImageReadPlan{&image,
                       embedded_block,
                       channels,
                       sample_count,
                       *sample_size,
                       *component_size,
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

Result<ByteOrder> resolve_byte_order(ByteOrder source_order,
                                     ByteOrderOutput requested_order) {
  switch (requested_order) {
  case ByteOrderOutput::source:
    return source_order;
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

Result<std::size_t> copy_serialized_property(const ByteSource &source,
                                             const PropertyReadPlan &plan,
                                             std::span<std::byte> destination,
                                             std::stop_token stop_token) {
  constexpr std::size_t kReadChunkBytes = 8U * 1024U * 1024U;
  const auto expected = static_cast<std::size_t>(plan.serialized_bytes);
  std::size_t total = 0;
  while (total < expected) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled,
                        "Property block read was cancelled");
    }
    const auto chunk = std::min(kReadChunkBytes, expected - total);
    auto output = destination.subspan(total, chunk);
    if (plan.inline_block != nullptr) {
      std::copy_n(plan.inline_block->data() + total, chunk, output.data());
      total += chunk;
    } else {
      auto read = source.read_at(plan.property->block.offset + total, output);
      if (!read) {
        return read.error();
      }
      if (read.value() == 0 || read.value() > output.size()) {
        return make_error(ErrorCode::io_error,
                          "ByteSource returned an invalid short read");
      }
      total += read.value();
    }
  }
  return total;
}

Result<std::size_t> copy_serialized_icc_profile(
    const ByteSource &source, const IccProfileReadPlan &plan,
    std::span<std::byte> destination, std::stop_token stop_token) {
  constexpr std::size_t kReadChunkBytes = 8U * 1024U * 1024U;
  const auto expected = static_cast<std::size_t>(plan.serialized_bytes);
  std::size_t total = 0;
  while (total < expected) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "ICC profile read was cancelled");
    }
    const auto chunk = std::min(kReadChunkBytes, expected - total);
    auto output = destination.subspan(total, chunk);
    if (plan.inline_block != nullptr) {
      std::copy_n(plan.inline_block->data() + total, chunk, output.data());
      total += chunk;
    } else {
      auto read = source.read_at(plan.profile->block.offset + total, output);
      if (!read) {
        return read.error();
      }
      if (read.value() == 0 || read.value() > output.size()) {
        return make_error(ErrorCode::io_error,
                          "ByteSource returned an invalid short read");
      }
      total += read.value();
    }
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

Result<std::size_t> decompress_zstd(std::span<const std::byte> input,
                                    std::span<std::byte> output,
                                    std::uint64_t max_window_bytes) {
  if (max_window_bytes < 1024 || !std::has_single_bit(max_window_bytes)) {
    return make_error(
        ErrorCode::invalid_argument,
        "Zstandard window limit must be a power of two of at least 1024 bytes");
  }
  auto *context = ZSTD_createDCtx();
  if (context == nullptr) {
    return make_error(ErrorCode::internal_error,
                      "Unable to initialize the Zstandard decoder");
  }
  const auto window_log =
      static_cast<int>(std::bit_width(max_window_bytes) - 1U);
  const auto configured =
      ZSTD_DCtx_setParameter(context, ZSTD_d_windowLogMax, window_log);
  if (ZSTD_isError(configured) != 0) {
    ZSTD_freeDCtx(context);
    return make_error(ErrorCode::invalid_argument,
                      "Invalid Zstandard window limit");
  }
  const auto produced = ZSTD_decompressDCtx(
      context, output.data(), output.size(), input.data(), input.size());
  ZSTD_freeDCtx(context);
  if (ZSTD_isError(produced) != 0 || produced != output.size()) {
    return make_error(ErrorCode::invalid_block,
                      "Invalid Zstandard frame or decompressed size mismatch");
  }
  return produced;
}

Result<std::size_t> decompress_subblock(CompressionCodec codec,
                                        std::span<const std::byte> input,
                                        std::span<std::byte> output,
                                        std::uint64_t max_zstd_window_bytes) {
  switch (codec) {
  case CompressionCodec::zlib:
    return decompress_zlib(input, output);
  case CompressionCodec::lz4:
  case CompressionCodec::lz4hc:
    return decompress_lz4(input, output);
  case CompressionCodec::zstd:
    return decompress_zstd(input, output, max_zstd_window_bytes);
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
  case ChecksumAlgorithm::sha3_256:
    return EVP_sha3_256();
  case ChecksumAlgorithm::sha3_512:
    return EVP_sha3_512();
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
                      "Block checksum verification failed");
  }
  return true;
}

Result<bool> verify_image_checksum_streaming(const ByteSource &source,
                                             const ImageReadPlan &image,
                                             std::stop_token stop_token,
                                             std::size_t image_index) {
  if (image.checksum.algorithm == ChecksumAlgorithm::none) {
    return true;
  }
  const auto *digest = checksum_digest(image.checksum.algorithm);
  if (digest == nullptr) {
    return make_error(ErrorCode::internal_error,
                      "Unable to resolve checksum implementation");
  }
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (!context || EVP_DigestInit_ex(context.get(), digest, nullptr) != 1) {
    return make_error(ErrorCode::internal_error,
                      "Unable to initialize checksum computation");
  }
  constexpr std::size_t kHashChunkBytes = 8U * 1024U * 1024U;
  std::vector<std::byte> buffer(std::min<std::size_t>(
      kHashChunkBytes, static_cast<std::size_t>(image.serialized_bytes)));
  std::size_t offset = 0;
  while (offset < static_cast<std::size_t>(image.serialized_bytes)) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    const auto count =
        std::min(buffer.size(),
                 static_cast<std::size_t>(image.serialized_bytes) - offset);
    auto read = read_serialized_chunk(source, image, offset,
                                      std::span(buffer).first(count),
                                      stop_token, image_index);
    if (!read) {
      return read.error();
    }
    if (EVP_DigestUpdate(context.get(), buffer.data(), count) != 1) {
      return make_error(ErrorCode::internal_error,
                        "Checksum computation failed");
    }
    offset += count;
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> actual{};
  unsigned int actual_size = 0;
  if (EVP_DigestFinal_ex(context.get(), actual.data(), &actual_size) != 1) {
    return make_error(ErrorCode::internal_error, "Checksum computation failed");
  }
  if (actual_size != image.checksum.expected_digest.size() ||
      !std::equal(actual.begin(), actual.begin() + actual_size,
                  image.checksum.expected_digest.begin())) {
    return make_error(ErrorCode::checksum_mismatch,
                      "Block checksum verification failed");
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
      return make_error(ErrorCode::cancelled, "Block read was cancelled");
    }
    for (std::size_t byte = 0; byte < item_size; ++byte) {
      output[item * item_size + byte] = shuffled[byte * item_count + item];
    }
  }
  return output.size();
}

Result<std::size_t> decode_compressed_block(
    std::span<const std::byte> serialized, const CompressionPlan &compression,
    std::span<std::byte> destination, std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    return make_error(ErrorCode::cancelled, "Block read was cancelled");
  }
  std::size_t input_offset = 0;
  std::size_t output_offset = 0;
  std::vector<std::byte> shuffled;
  for (const auto &subblock : compression.subblocks) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Block read was cancelled");
    }
    const auto compressed_size =
        static_cast<std::size_t>(subblock.compressed_size);
    const auto uncompressed_size =
        static_cast<std::size_t>(subblock.uncompressed_size);
    const auto input = std::span<const std::byte>(serialized)
                           .subspan(input_offset, compressed_size);
    auto output = destination.subspan(output_offset, uncompressed_size);
    if (compression.byte_shuffled) {
      shuffled.resize(uncompressed_size);
      auto decoded = decompress_subblock(compression.codec, input, shuffled,
                                         compression.max_zstd_window_bytes);
      if (!decoded) {
        return decoded.error();
      }
      auto unshuffled = unshuffle_bytes(
          shuffled, output, static_cast<std::size_t>(compression.item_size),
          stop_token);
      if (!unshuffled) {
        return unshuffled.error();
      }
    } else {
      auto decoded = decompress_subblock(compression.codec, input, output,
                                         compression.max_zstd_window_bytes);
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
  const auto component_size =
      static_cast<std::size_t>(plan.endian_component_size);
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
      const auto component_offset = (byte / component_size) * component_size;
      const auto input_byte =
          plan.image->byte_order == output_byte_order
              ? byte
              : component_offset + component_size - (byte % component_size) -
                    1;
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
  const auto component_size =
      static_cast<std::size_t>(plan.endian_component_size);
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
        const auto component_offset = (byte / component_size) * component_size;
        const auto input_byte =
            plan.image->byte_order == output_byte_order
                ? byte
                : component_offset + component_size -
                      (byte % component_size) - 1;
        destination[output_offset + byte] = staging[input_offset + input_byte];
      }
    }
    source_sample += chunk_samples;
  }
  return static_cast<std::size_t>(plan.expected_bytes);
}

Result<std::size_t> swap_byte_order_in_place(std::span<std::byte> destination,
                                             std::size_t component_size,
                                             std::stop_token stop_token) {
  constexpr std::size_t kSamplesPerCancellationCheck = 1U << 20U;
  const auto component_count = destination.size() / component_size;
  for (std::size_t component = 0; component < component_count; ++component) {
    if (component % kSamplesPerCancellationCheck == 0 &&
        stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Block read was cancelled");
    }
    const auto begin = destination.begin() + component * component_size;
    std::reverse(begin, begin + component_size);
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

Result<bool> validate_icc_profile_bytes(std::span<const std::byte> bytes) {
  constexpr std::size_t kIccHeaderBytes = 128;
  if (bytes.size() < kIccHeaderBytes) {
    return make_error(ErrorCode::invalid_block,
                      "ICC profile is shorter than its mandatory header");
  }
  const auto octet = [&](std::size_t offset) {
    return static_cast<std::uint32_t>(
        std::to_integer<unsigned char>(bytes[offset]));
  };
  const auto declared_size =
      (octet(0) << 24U) | (octet(1) << 16U) | (octet(2) << 8U) | octet(3);
  if (declared_size != bytes.size()) {
    return make_error(ErrorCode::invalid_block,
                      "ICC profile size field does not match decoded bytes");
  }
  if (octet(36) != static_cast<unsigned char>('a') ||
      octet(37) != static_cast<unsigned char>('c') ||
      octet(38) != static_cast<unsigned char>('s') ||
      octet(39) != static_cast<unsigned char>('p')) {
    return make_error(ErrorCode::invalid_block,
                      "ICC profile is missing the acsp signature");
  }
  if ((octet(47) & 0x01U) == 0) {
    return make_error(ErrorCode::invalid_block,
                      "ICC profile embedded-profile flag is not set");
  }
  return true;
}

} // namespace

struct Reader::Impl {
  std::shared_ptr<const ByteSource> source;
  ReaderOptions options;
  Document document;
  std::vector<std::vector<std::byte>> embedded_blocks;
  std::vector<std::vector<std::byte>> inline_metadata_blocks;
  std::vector<std::vector<std::byte>> inline_icc_profile_blocks;
  std::vector<std::vector<std::byte>> embedded_thumbnail_blocks;
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
    impl->inline_metadata_blocks =
        std::move(parsed_header.inline_metadata_blocks);
    impl->inline_icc_profile_blocks =
        std::move(parsed_header.inline_icc_profile_blocks);
    impl->embedded_thumbnail_blocks =
        std::move(parsed_header.embedded_thumbnail_blocks);
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
    auto output_byte_order = resolve_byte_order(plan.value().image->byte_order,
                                                read_options.byte_order);
    if (!output_byte_order) {
      return output_byte_order.error();
    }
    RawImage result;
    result.width = plan.value().image->geometry[0];
    result.height = plan.value().image->geometry[1];
    result.channels = plan.value().channels;
    result.sample_format = plan.value().image->sample_format;
    result.orientation = plan.value().image->orientation;
    result.pixel_origin = plan.value().image->pixel_origin;
    result.pixel_traversal = plan.value().image->pixel_traversal;
    result.nominal_channel_order = plan.value().image->nominal_channel_order;
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
    if (!plan.value().image->checksum.empty()) {
      result.checksum_verification = ChecksumVerification::verified;
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
    auto output_byte_order = resolve_byte_order(plan.value().image->byte_order,
                                                read_options.byte_order);
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
          auto decoded = decode_compressed_block(
              serialized, plan.value().compression, source_pixels, stop_token);
          if (!decoded) {
            return decoded.error();
          }
          return transform_pixel_storage_from_buffer(
              plan.value(), source_pixels, output, output_storage.value(),
              output_byte_order.value(), stop_token);
        }
        auto decoded = decode_compressed_block(
            serialized, plan.value().compression, output, stop_token);
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
            output,
            static_cast<std::size_t>(plan.value().endian_component_size),
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
          output,
          static_cast<std::size_t>(plan.value().endian_component_size),
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

Result<ImageRowReadSummary>
Reader::read_image_rows(std::size_t image_index, ImageRowSink &destination,
                        ImageRowReadOptions read_options,
                        std::stop_token stop_token) const {
  try {
    auto plan = plan_image_read(impl_->document, impl_->options,
                                impl_->embedded_blocks, image_index);
    if (!plan) {
      return plan.error();
    }
    if (read_options.max_row_bytes == 0 ||
        read_options.max_subblock_bytes == 0) {
      return make_error(ErrorCode::invalid_argument,
                        "Image row staging limits must be nonzero");
    }
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    auto output_byte_order = resolve_byte_order(plan.value().image->byte_order,
                                                read_options.byte_order);
    if (!output_byte_order) {
      return output_byte_order.error();
    }

    const auto width = plan.value().image->geometry[0];
    const auto height = plan.value().image->geometry[1];
    if (width == 0 || height == 0 || plan.value().channels == 0) {
      return make_error(ErrorCode::invalid_block,
                        "Image row geometry must be nonzero");
    }
    std::uint64_t planar_row_count = 0;
    if (!checked_multiply(height, plan.value().channels, planar_row_count)) {
      return make_error(ErrorCode::overflow, "Image plane-row count overflows");
    }
    const auto source_row_count =
        plan.value().image->pixel_storage == PixelStorage::planar
            ? planar_row_count
            : height;
    std::uint64_t plane_row_bytes = 0;
    if (!checked_multiply(width, plan.value().sample_size, plane_row_bytes)) {
      return make_error(ErrorCode::overflow,
                        "Image plane-row byte size overflows");
    }
    std::uint64_t source_row_bytes = plane_row_bytes;
    if (plan.value().image->pixel_storage == PixelStorage::normal &&
        !checked_multiply(source_row_bytes, plan.value().channels,
                          source_row_bytes)) {
      return make_error(ErrorCode::overflow,
                        "Image source-row byte size overflows");
    }
    if (plane_row_bytes > read_options.max_row_bytes ||
        source_row_bytes > read_options.max_row_bytes ||
        source_row_bytes > std::numeric_limits<std::size_t>::max() ||
        plane_row_bytes > std::numeric_limits<std::size_t>::max()) {
      return make_error(ErrorCode::resource_limit,
                        "Image row exceeds the configured staging limit");
    }
    if (plan.value().compression.codec != CompressionCodec::none) {
      for (const auto &subblock : plan.value().compression.subblocks) {
        if (subblock.compressed_size > read_options.max_subblock_bytes ||
            subblock.uncompressed_size > read_options.max_subblock_bytes) {
          return make_error(
              ErrorCode::resource_limit,
              "Compressed image subblock exceeds the staging limit");
        }
      }
    }

    auto verified = verify_image_checksum_streaming(
        *impl_->source, plan.value(), stop_token, image_index);
    if (!verified) {
      return verified.error();
    }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>
        delivered_checksum_context(nullptr, &EVP_MD_CTX_free);
    if (plan.value().checksum.algorithm != ChecksumAlgorithm::none) {
      delivered_checksum_context.reset(EVP_MD_CTX_new());
      const auto *digest = checksum_digest(plan.value().checksum.algorithm);
      if (!delivered_checksum_context || digest == nullptr ||
          EVP_DigestInit_ex(delivered_checksum_context.get(), digest,
                            nullptr) != 1) {
        return make_error(ErrorCode::internal_error,
                          "Unable to initialize checksum computation");
      }
    }
    const auto hash_delivered_serialized =
        [&](std::span<const std::byte> bytes) -> Result<void> {
      if (delivered_checksum_context &&
          EVP_DigestUpdate(delivered_checksum_context.get(), bytes.data(),
                           bytes.size()) != 1) {
        return make_error(ErrorCode::internal_error,
                          "Checksum computation failed");
      }
      return {};
    };

    ImageRowReadSummary summary;
    if (plan.value().checksum.algorithm != ChecksumAlgorithm::none) {
      summary.checksum_verification = ChecksumVerification::verified;
    }
    std::vector<std::byte> source_row(
        static_cast<std::size_t>(source_row_bytes));
    std::vector<std::byte> plane_row;
    if (plan.value().image->pixel_storage == PixelStorage::normal) {
      plane_row.resize(static_cast<std::size_t>(plane_row_bytes));
    }
    std::uint64_t source_row_ordinal = 0;
    const auto sample_size = static_cast<std::size_t>(plan.value().sample_size);
    const auto component_size =
        static_cast<std::size_t>(plan.value().endian_component_size);
    const auto emit_plane_row =
        [&](std::uint64_t channel_index, std::uint64_t row_index,
            std::span<std::byte> bytes) -> Result<void> {
      if (stop_token.stop_requested()) {
        return make_error(ErrorCode::cancelled, "Image read was cancelled");
      }
      if (output_byte_order.value() != plan.value().image->byte_order) {
        auto swapped =
            swap_byte_order_in_place(bytes, component_size, stop_token);
        if (!swapped) {
          return swapped.error();
        }
      }
      const ImageRowView view{.channel_index = channel_index,
                              .row_index = row_index,
                              .byte_order = output_byte_order.value(),
                              .bytes = bytes};
      auto consumed = destination.consume(view);
      if (!consumed) {
        return consumed.error();
      }
      ++summary.rows_delivered;
      summary.bytes_delivered += bytes.size();
      return {};
    };
    const auto emit_source_row =
        [&](std::span<std::byte> bytes) -> Result<void> {
      if (plan.value().image->pixel_storage == PixelStorage::planar) {
        const auto channel_index = source_row_ordinal / height;
        const auto row_index = source_row_ordinal % height;
        auto emitted = emit_plane_row(channel_index, row_index, bytes);
        if (!emitted) {
          return emitted.error();
        }
      } else {
        for (std::uint64_t channel = 0; channel < plan.value().channels;
             ++channel) {
          for (std::uint64_t pixel = 0; pixel < width; ++pixel) {
            const auto source_offset = static_cast<std::size_t>(
                (pixel * plan.value().channels + channel) *
                plan.value().sample_size);
            const auto output_offset =
                static_cast<std::size_t>(pixel * plan.value().sample_size);
            std::copy_n(bytes.data() + source_offset, sample_size,
                        plane_row.data() + output_offset);
          }
          auto emitted = emit_plane_row(channel, source_row_ordinal, plane_row);
          if (!emitted) {
            return emitted.error();
          }
        }
      }
      ++source_row_ordinal;
      return {};
    };

    if (plan.value().compression.codec == CompressionCodec::none) {
      for (std::uint64_t row = 0; row < source_row_count; ++row) {
        auto read = read_serialized_chunk(
            *impl_->source, plan.value(),
            static_cast<std::size_t>(row * source_row_bytes), source_row,
            stop_token, image_index);
        if (!read) {
          return read.error();
        }
        auto hashed = hash_delivered_serialized(source_row);
        if (!hashed) {
          return hashed.error();
        }
        auto emitted = emit_source_row(source_row);
        if (!emitted) {
          return emitted.error();
        }
      }
    } else {
      std::size_t row_fill = 0;
      const auto feed_decoded =
          [&](std::span<const std::byte> decoded) -> Result<void> {
        std::size_t offset = 0;
        while (offset < decoded.size()) {
          const auto count =
              std::min(source_row.size() - row_fill, decoded.size() - offset);
          std::copy_n(decoded.data() + offset, count,
                      source_row.data() + row_fill);
          row_fill += count;
          offset += count;
          if (row_fill == source_row.size()) {
            auto emitted = emit_source_row(source_row);
            if (!emitted) {
              return emitted.error();
            }
            row_fill = 0;
          }
        }
        return {};
      };

      std::size_t serialized_offset = 0;
      for (const auto &subblock : plan.value().compression.subblocks) {
        std::vector<std::byte> serialized(
            static_cast<std::size_t>(subblock.compressed_size));
        auto read = read_serialized_chunk(*impl_->source, plan.value(),
                                          serialized_offset, serialized,
                                          stop_token, image_index);
        if (!read) {
          return read.error();
        }
        auto hashed = hash_delivered_serialized(serialized);
        if (!hashed) {
          return hashed.error();
        }
        std::vector<std::byte> decoded(
            static_cast<std::size_t>(subblock.uncompressed_size));
        CompressionPlan one_subblock;
        one_subblock.codec = plan.value().compression.codec;
        one_subblock.byte_shuffled = plan.value().compression.byte_shuffled;
        one_subblock.uncompressed_size = subblock.uncompressed_size;
        one_subblock.item_size = plan.value().compression.item_size;
        one_subblock.max_zstd_window_bytes =
            plan.value().compression.max_zstd_window_bytes;
        one_subblock.subblocks.push_back(subblock);
        auto decompressed = decode_compressed_block(serialized, one_subblock,
                                                    decoded, stop_token);
        if (!decompressed) {
          return decompressed.error();
        }
        auto fed = feed_decoded(decoded);
        if (!fed) {
          return fed.error();
        }
        serialized_offset += serialized.size();
      }
      if (row_fill != 0) {
        return make_error(ErrorCode::invalid_block,
                          "Decoded image ends with an incomplete source row");
      }
    }

    if (source_row_ordinal != source_row_count ||
        summary.rows_delivered != planar_row_count ||
        summary.bytes_delivered != plan.value().expected_bytes) {
      return make_error(ErrorCode::invalid_block,
                        "Decoded image row extent does not match geometry");
    }
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Image read was cancelled");
    }
    if (delivered_checksum_context) {
      std::array<unsigned char, EVP_MAX_MD_SIZE> actual{};
      unsigned int actual_size = 0;
      if (EVP_DigestFinal_ex(delivered_checksum_context.get(), actual.data(),
                             &actual_size) != 1) {
        return make_error(ErrorCode::internal_error,
                          "Checksum computation failed");
      }
      if (actual_size != plan.value().checksum.expected_digest.size() ||
          !std::equal(actual.begin(), actual.begin() + actual_size,
                      plan.value().checksum.expected_digest.begin())) {
        return make_error(ErrorCode::checksum_mismatch,
                          "Block checksum changed during row delivery");
      }
    }
    return summary;
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while streaming image rows");
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected image row read failure: ") +
                          exception.what());
  }
}

Result<RawPropertyBlock>
Reader::read_property_block(std::size_t metadata_index,
                            PropertyReadOptions read_options,
                            std::stop_token stop_token) const {
  try {
    auto plan =
        plan_property_read(impl_->document, impl_->options,
                           impl_->inline_metadata_blocks, metadata_index);
    if (!plan) {
      return plan.error();
    }
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled,
                        "Property block read was cancelled");
    }
    auto output_byte_order = resolve_byte_order(
        plan.value().property->byte_order, read_options.byte_order);
    if (!output_byte_order) {
      return output_byte_order.error();
    }

    RawPropertyBlock result;
    result.byte_order = output_byte_order.value();
    result.bytes.resize(static_cast<std::size_t>(plan.value().expected_bytes));
    const bool needs_serialized_staging =
        plan.value().compression.codec != CompressionCodec::none ||
        plan.value().checksum.algorithm != ChecksumAlgorithm::none;
    if (needs_serialized_staging) {
      std::vector<std::byte> serialized(
          static_cast<std::size_t>(plan.value().serialized_bytes));
      auto copied = copy_serialized_property(*impl_->source, plan.value(),
                                             serialized, stop_token);
      if (!copied) {
        return copied.error();
      }
      auto verified = verify_checksum(plan.value().checksum, serialized);
      if (!verified) {
        return verified.error();
      }
      if (plan.value().compression.codec != CompressionCodec::none) {
        auto decoded = decode_compressed_block(
            serialized, plan.value().compression, result.bytes, stop_token);
        if (!decoded) {
          return decoded.error();
        }
      } else {
        std::copy(serialized.begin(), serialized.end(), result.bytes.begin());
      }
    } else {
      auto copied = copy_serialized_property(*impl_->source, plan.value(),
                                             result.bytes, stop_token);
      if (!copied) {
        return copied.error();
      }
    }
    if (!plan.value().layout.string_data &&
        plan.value().layout.scalar_component_size > 1 &&
        result.byte_order != plan.value().property->byte_order) {
      auto swapped = swap_byte_order_in_place(
          result.bytes,
          static_cast<std::size_t>(plan.value().layout.scalar_component_size),
          stop_token);
      if (!swapped) {
        return swapped.error();
      }
    }
    if (plan.value().checksum.algorithm != ChecksumAlgorithm::none) {
      result.checksum_verification = ChecksumVerification::verified;
    }
    return result;
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while reading Property block");
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected Property block read failure: ") +
                          exception.what());
  }
}

Result<RawIccProfile>
Reader::read_icc_profile(std::size_t profile_index,
                         std::stop_token stop_token) const {
  try {
    auto plan =
        plan_icc_profile_read(impl_->document, impl_->options,
                              impl_->inline_icc_profile_blocks, profile_index);
    if (!plan) {
      return plan.error();
    }
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "ICC profile read was cancelled");
    }
    RawIccProfile result;
    result.bytes.resize(static_cast<std::size_t>(plan.value().expected_bytes));
    const bool needs_serialized_staging =
        plan.value().compression.codec != CompressionCodec::none ||
        plan.value().checksum.algorithm != ChecksumAlgorithm::none;
    if (needs_serialized_staging) {
      std::vector<std::byte> serialized(
          static_cast<std::size_t>(plan.value().serialized_bytes));
      auto copied = copy_serialized_icc_profile(*impl_->source, plan.value(),
                                                serialized, stop_token);
      if (!copied) {
        return copied.error();
      }
      auto verified = verify_checksum(plan.value().checksum, serialized);
      if (!verified) {
        return verified.error();
      }
      if (plan.value().compression.codec != CompressionCodec::none) {
        auto decoded = decode_compressed_block(
            serialized, plan.value().compression, result.bytes, stop_token);
        if (!decoded) {
          return decoded.error();
        }
      } else {
        std::copy(serialized.begin(), serialized.end(), result.bytes.begin());
      }
    } else {
      auto copied = copy_serialized_icc_profile(*impl_->source, plan.value(),
                                                result.bytes, stop_token);
      if (!copied) {
        return copied.error();
      }
    }
    auto valid = validate_icc_profile_bytes(result.bytes);
    if (!valid) {
      return valid.error();
    }
    if (plan.value().checksum.algorithm != ChecksumAlgorithm::none) {
      result.checksum_verification = ChecksumVerification::verified;
    }
    return result;
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while reading ICC profile");
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected ICC profile read failure: ") +
                          exception.what());
  }
}

Result<RawImage> Reader::read_thumbnail(std::size_t thumbnail_index,
                                        std::stop_token stop_token) const {
  try {
    auto plan =
        plan_thumbnail_read(impl_->document, impl_->options,
                            impl_->embedded_thumbnail_blocks, thumbnail_index);
    if (!plan) {
      return plan.error();
    }
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "Thumbnail read was cancelled");
    }
    RawImage result;
    result.width = plan.value().image->geometry[0];
    result.height = plan.value().image->geometry[1];
    result.channels = plan.value().channels;
    result.sample_format = plan.value().image->sample_format;
    result.orientation = plan.value().image->orientation;
    result.pixel_origin = plan.value().image->pixel_origin;
    result.pixel_traversal = plan.value().image->pixel_traversal;
    result.nominal_channel_order = plan.value().image->nominal_channel_order;
    result.pixel_storage = plan.value().image->pixel_storage;
    result.byte_order = plan.value().image->byte_order;
    result.pixels.resize(static_cast<std::size_t>(plan.value().expected_bytes));

    const bool needs_serialized_staging =
        plan.value().compression.codec != CompressionCodec::none ||
        plan.value().checksum.algorithm != ChecksumAlgorithm::none;
    if (needs_serialized_staging) {
      std::vector<std::byte> serialized(
          static_cast<std::size_t>(plan.value().serialized_bytes));
      auto copied =
          copy_serialized_image(*impl_->source, plan.value(), serialized,
                                stop_token, thumbnail_index);
      if (!copied) {
        return copied.error();
      }
      auto verified = verify_checksum(plan.value().checksum, serialized);
      if (!verified) {
        return verified.error();
      }
      if (plan.value().compression.codec != CompressionCodec::none) {
        auto decoded = decode_compressed_block(
            serialized, plan.value().compression, result.pixels, stop_token);
        if (!decoded) {
          return decoded.error();
        }
      } else {
        std::copy(serialized.begin(), serialized.end(), result.pixels.begin());
      }
    } else {
      auto copied =
          copy_serialized_image(*impl_->source, plan.value(), result.pixels,
                                stop_token, thumbnail_index);
      if (!copied) {
        return copied.error();
      }
    }
    if (plan.value().checksum.algorithm != ChecksumAlgorithm::none) {
      result.checksum_verification = ChecksumVerification::verified;
    }
    return result;
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Memory allocation failed while reading Thumbnail");
  } catch (const std::exception &exception) {
    return make_error(ErrorCode::internal_error,
                      std::string("Unexpected Thumbnail read failure: ") +
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

const char *to_string(ImageOrientation orientation) noexcept {
  switch (orientation) {
  case ImageOrientation::identity:
    return "0";
  case ImageOrientation::flip:
    return "flip";
  case ImageOrientation::rotate_90:
    return "90";
  case ImageOrientation::rotate_90_flip:
    return "90;flip";
  case ImageOrientation::rotate_minus_90:
    return "-90";
  case ImageOrientation::rotate_minus_90_flip:
    return "-90;flip";
  case ImageOrientation::rotate_180:
    return "180";
  case ImageOrientation::rotate_180_flip:
    return "180;flip";
  }
  return "0";
}

const char *to_string(PixelOrigin origin) noexcept {
  switch (origin) {
  case PixelOrigin::top_left:
    return "Top-left";
  }
  return "Top-left";
}

const char *to_string(PixelTraversal traversal) noexcept {
  switch (traversal) {
  case PixelTraversal::top_to_bottom_left_to_right:
    return "Top-to-bottom, left-to-right";
  }
  return "Top-to-bottom, left-to-right";
}

const char *to_string(NominalChannelOrder order) noexcept {
  switch (order) {
  case NominalChannelOrder::gray_then_alpha:
    return "Gray, then alpha";
  case NominalChannelOrder::red_green_blue_then_alpha:
    return "Red, green, blue, then alpha";
  case NominalChannelOrder::cie_l_a_b_then_alpha:
    return "CIE L*, a*, b*, then alpha";
  }
  return "Gray, then alpha";
}

const char *to_string(ChecksumVerification verification) noexcept {
  return verification == ChecksumVerification::verified ? "Verified"
                                                        : "Not declared";
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

const char *to_string(TableCellInfo::ValueForm value_form) noexcept {
  switch (value_form) {
  case TableCellInfo::ValueForm::attribute:
    return "Attribute";
  case TableCellInfo::ValueForm::character_data:
    return "Character data";
  case TableCellInfo::ValueForm::data_block:
    return "Data block";
  }
  return "Attribute";
}

const char *to_string(AncillaryKind kind) noexcept {
  switch (kind) {
  case AncillaryKind::rgb_working_space:
    return "RGBWorkingSpace";
  case AncillaryKind::display_function:
    return "DisplayFunction";
  case AncillaryKind::color_filter_array:
    return "ColorFilterArray";
  case AncillaryKind::resolution:
    return "Resolution";
  }
  return "Unknown";
}

} // namespace mmxisf
