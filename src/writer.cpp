// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/writer.hpp"
#include "property_types.hpp"

#include <lz4.h>
#include <lz4hc.h>
#include <openssl/evp.h>
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace mmxisf {
namespace {

using detail::classify_property_type;
using detail::property_element_layout;
using detail::PropertyCategory;

Error make_error(ErrorCode code, std::string message) {
  Error error;
  error.code = code;
  error.message = std::move(message);
  return error;
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

bool is_power_of_two(std::uint64_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

Result<std::uint64_t> align_up(std::uint64_t value, std::uint64_t alignment) {
  const auto remainder = value & (alignment - 1);
  if (remainder == 0) {
    return value;
  }
  std::uint64_t aligned = 0;
  if (!checked_add(value, alignment - remainder, aligned)) {
    return make_error(ErrorCode::overflow,
                      "Writer attachment alignment overflows");
  }
  return aligned;
}

bool is_leap_year(unsigned year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool parse_digits(std::string_view text, std::size_t position,
                  std::size_t count, unsigned &value) {
  if (position > text.size() || count > text.size() - position) {
    return false;
  }
  value = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const auto character = text[position + index];
    if (character < '0' || character > '9') {
      return false;
    }
    value = value * 10U + static_cast<unsigned>(character - '0');
  }
  return true;
}

bool is_canonical_utc_time(std::string_view text) {
  if (text.size() != 20 || text[4] != '-' || text[7] != '-' ||
      text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
      text[19] != 'Z') {
    return false;
  }
  unsigned year = 0;
  unsigned month = 0;
  unsigned day = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  if (!parse_digits(text, 0, 4, year) || !parse_digits(text, 5, 2, month) ||
      !parse_digits(text, 8, 2, day) || !parse_digits(text, 11, 2, hour) ||
      !parse_digits(text, 14, 2, minute) ||
      !parse_digits(text, 17, 2, second)) {
    return false;
  }
  constexpr std::array<unsigned, 12> month_lengths{31, 28, 31, 30, 31, 30,
                                                   31, 31, 30, 31, 30, 31};
  if (month == 0 || month > month_lengths.size()) {
    return false;
  }
  auto maximum_day = month_lengths[month - 1];
  if (month == 2 && is_leap_year(year)) {
    maximum_day = 29;
  }
  return day != 0 && day <= maximum_day && hour <= 23 && minute <= 59 &&
         second <= 60;
}

bool is_valid_xml_utf8(std::string_view text) {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::uint32_t code_point = 0;
    std::size_t continuation_count = 0;
    if (lead < 0x80U) {
      code_point = lead;
    } else if (lead >= 0xc2U && lead <= 0xdfU) {
      code_point = lead & 0x1fU;
      continuation_count = 1;
    } else if (lead >= 0xe0U && lead <= 0xefU) {
      code_point = lead & 0x0fU;
      continuation_count = 2;
    } else if (lead >= 0xf0U && lead <= 0xf4U) {
      code_point = lead & 0x07U;
      continuation_count = 3;
    } else {
      return false;
    }
    if (continuation_count > text.size() - index - 1) {
      return false;
    }
    for (std::size_t continuation = 0; continuation < continuation_count;
         ++continuation) {
      const auto byte =
          static_cast<unsigned char>(text[index + continuation + 1]);
      if ((byte & 0xc0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (byte & 0x3fU);
    }
    if ((continuation_count == 2 && code_point < 0x800U) ||
        (continuation_count == 3 && code_point < 0x10000U) ||
        code_point > 0x10ffffU ||
        (code_point >= 0xd800U && code_point <= 0xdfffU)) {
      return false;
    }
    const bool xml_character =
        code_point == 0x09U || code_point == 0x0aU || code_point == 0x0dU ||
        (code_point >= 0x20U && code_point <= 0xd7ffU) ||
        (code_point >= 0xe000U && code_point <= 0xfffdU) ||
        (code_point >= 0x10000U && code_point <= 0x10ffffU);
    if (!xml_character) {
      return false;
    }
    index += continuation_count + 1;
  }
  return true;
}

Result<std::string> escape_xml(std::string_view input, bool attribute) {
  if (!is_valid_xml_utf8(input)) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer text is not valid XML 1.0 UTF-8");
  }
  std::string output;
  output.reserve(input.size());
  for (const auto character : input) {
    switch (character) {
    case '&':
      output += "&amp;";
      break;
    case '<':
      output += "&lt;";
      break;
    case '>':
      output += "&gt;";
      break;
    case '"':
      output += attribute ? "&quot;" : "\"";
      break;
    default:
      output.push_back(character);
      break;
    }
  }
  return output;
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
  for (const auto character : value) {
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
  return static_cast<unsigned>(digits.front() - '0') <=
         (1U << leading_bits) - 1U;
}

bool is_valid_integer_value(std::string_view text, bool is_signed,
                            unsigned bit_width) {
  text = trim_xml_whitespace(text);
  if (text.empty()) {
    return false;
  }
  if (text.size() >= 2 && text.front() == '0') {
    const char prefix = text[1];
    const unsigned base = prefix == 'b' || prefix == 'B'   ? 2U
                          : prefix == 'o' || prefix == 'O' ? 8U
                          : prefix == 'x' || prefix == 'X' ? 16U
                                                           : 0U;
    if (base != 0) {
      text.remove_prefix(2);
      const bool valid_digits =
          !text.empty() &&
          std::all_of(text.begin(), text.end(), [base](char character) {
            if (character >= '0' && character <= '9') {
              return static_cast<unsigned>(character - '0') < base;
            }
            return base == 16 && ((character >= 'a' && character <= 'f') ||
                                  (character >= 'A' && character <= 'F'));
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
  if (text.empty() || (text.size() > 1 && text.front() == '0') ||
      !std::all_of(text.begin(), text.end(), is_ascii_digit)) {
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
  std::size_t position = 0;
  while (position < text.size() && is_ascii_digit(text[position])) {
    ++position;
  }
  const bool integer_digits = position != 0;
  bool fraction_digits = false;
  if (position < text.size() && text[position] == '.') {
    const auto start = ++position;
    while (position < text.size() && is_ascii_digit(text[position])) {
      ++position;
    }
    fraction_digits = position != start;
    if (!fraction_digits) {
      return false;
    }
  }
  if (!integer_digits && !fraction_digits) {
    return false;
  }
  if (position < text.size() &&
      (text[position] == 'e' || text[position] == 'E')) {
    ++position;
    if (position < text.size() &&
        (text[position] == '+' || text[position] == '-')) {
      ++position;
    }
    const auto start = position;
    while (position < text.size() && is_ascii_digit(text[position])) {
      ++position;
    }
    if (position == start) {
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

unsigned integer_bit_width(std::string_view type) {
  if (type == "Int8" || type == "UInt8" || type == "Byte")
    return 8;
  if (type == "Int16" || type == "Short" || type == "UInt16" ||
      type == "UShort")
    return 16;
  if (type == "Int32" || type == "Int" || type == "UInt32" || type == "UInt")
    return 32;
  if (type == "Int64" || type == "UInt64")
    return 64;
  if (type == "Int128" || type == "UInt128")
    return 128;
  return 0;
}

bool is_valid_scalar_property(std::string_view type, std::string_view value) {
  if (type == "Boolean") {
    const auto trimmed = trim_xml_whitespace(value);
    return trimmed == "true" || trimmed == "false" || trimmed == "0" ||
           trimmed == "1";
  }
  constexpr std::array<std::string_view, 7> signed_types{
      "Int8", "Int16", "Short", "Int32", "Int", "Int64", "Int128"};
  if (std::find(signed_types.begin(), signed_types.end(), type) !=
      signed_types.end()) {
    return is_valid_integer_value(value, true, integer_bit_width(type));
  }
  constexpr std::array<std::string_view, 8> unsigned_types{
      "UInt8",  "Byte", "UInt16", "UShort",
      "UInt32", "UInt", "UInt64", "UInt128"};
  if (std::find(unsigned_types.begin(), unsigned_types.end(), type) !=
      unsigned_types.end()) {
    return is_valid_integer_value(value, false, integer_bit_width(type));
  }
  constexpr std::array<std::string_view, 6> floating_types{
      "Float32", "Float", "Float64", "Double", "Float128", "Quad"};
  if (std::find(floating_types.begin(), floating_types.end(), type) !=
      floating_types.end()) {
    return is_valid_floating_point_value(value);
  }
  constexpr std::array<std::string_view, 4> complex_types{
      "Complex32", "Complex64", "Complex", "Complex128"};
  return std::find(complex_types.begin(), complex_types.end(), type) !=
             complex_types.end() &&
         is_valid_complex_value(value);
}

bool is_supported_scalar_property_type(std::string_view type) {
  return is_valid_scalar_property(type, "0") ||
         is_valid_scalar_property(type, "(0,0)");
}

struct PreparedBlock {
  std::vector<std::byte> storage;
  std::filesystem::path spool_path;
  std::uint64_t serialized_size{0};
  std::string compression;
  std::string subblocks;
  std::string checksum;
};

Result<std::string>
make_metadata_xml(const MetadataWriteEntry &entry,
                  const BlockLocation *property_block = nullptr,
                  const PreparedBlock *prepared_block = nullptr) {
  auto escaped_name = escape_xml(entry.name, true);
  auto escaped_value = escape_xml(
      entry.value,
      entry.type != "String" || entry.kind == MetadataWriteKind::fits_keyword);
  if (!escaped_name) {
    return escaped_name.error();
  }
  if (!escaped_value) {
    return escaped_value.error();
  }
  if (entry.kind == MetadataWriteKind::fits_keyword) {
    auto escaped_comment = escape_xml(entry.comment, true);
    if (!escaped_comment) {
      return escaped_comment.error();
    }
    return "<FITSKeyword name=\"" + escaped_name.value() + "\" value=\"" +
           escaped_value.value() + "\" comment=\"" + escaped_comment.value() +
           "\"/>";
  }
  if (entry.value_form == MetadataWriteValueForm::data_block) {
    if (property_block == nullptr || prepared_block == nullptr) {
      return make_error(ErrorCode::internal_error,
                        "Writer Property block layout is missing");
    }
    auto escaped_format = escape_xml(entry.format, true);
    if (!escaped_format) {
      return escaped_format.error();
    }
    std::string result = "<Property id=\"" + escaped_name.value() +
                         "\" type=\"" + entry.type + "\"";
    if (entry.length) {
      result += " length=\"" + std::to_string(*entry.length) + "\"";
    } else {
      result += " rows=\"" + std::to_string(*entry.rows) + "\" columns=\"" +
                std::to_string(*entry.columns) + "\"";
    }
    if (entry.byte_order == ByteOrder::big) {
      result += " byteOrder=\"big\"";
    }
    if (!entry.format.empty()) {
      result += " format=\"" + escaped_format.value() + "\"";
    }
    if (!prepared_block->compression.empty()) {
      result += " compression=\"" + prepared_block->compression + "\"";
    }
    if (!prepared_block->subblocks.empty()) {
      result += " subblocks=\"" + prepared_block->subblocks + "\"";
    }
    if (!prepared_block->checksum.empty()) {
      result += " checksum=\"" + prepared_block->checksum + "\"";
    }
    result +=
        " location=\"attachment:" + std::to_string(property_block->offset) +
        ':' + std::to_string(property_block->size) + "\"/>";
    return result;
  }
  if (entry.type == "String") {
    return "<Property id=\"" + escaped_name.value() + "\" type=\"String\">" +
           escaped_value.value() + "</Property>";
  }
  return "<Property id=\"" + escaped_name.value() + "\" type=\"" + entry.type +
         "\" value=\"" + escaped_value.value() + "\"/>";
}

std::pair<std::string_view, std::uint64_t>
sample_format_description(SampleFormat format) {
  switch (format) {
  case SampleFormat::uint8:
    return {"UInt8", 1};
  case SampleFormat::uint16:
    return {"UInt16", 2};
  case SampleFormat::uint32:
    return {"UInt32", 4};
  case SampleFormat::float32:
    return {"Float32", 4};
  case SampleFormat::float64:
    return {"Float64", 8};
  default:
    return {{}, 0};
  }
}

Result<std::string> format_bound(double value) {
  if (!std::isfinite(value)) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer image bounds must be finite");
  }
  std::array<char, 64> buffer{};
  const auto converted =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                    std::chars_format::scientific,
                    std::numeric_limits<double>::max_digits10 - 1);
  if (converted.ec != std::errc{}) {
    return make_error(ErrorCode::internal_error,
                      "Writer could not serialize image bounds");
  }
  return std::string(buffer.data(), converted.ptr);
}

std::string_view compression_name(CompressionCodec codec) {
  switch (codec) {
  case CompressionCodec::none:
    return {};
  case CompressionCodec::zlib:
    return "zlib";
  case CompressionCodec::lz4:
    return "lz4";
  case CompressionCodec::lz4hc:
    return "lz4hc";
  case CompressionCodec::zstd:
    return "zstd";
  }
  return {};
}

bool is_valid_compression_codec(CompressionCodec codec) {
  switch (codec) {
  case CompressionCodec::none:
  case CompressionCodec::zlib:
  case CompressionCodec::lz4:
  case CompressionCodec::lz4hc:
  case CompressionCodec::zstd:
    return true;
  }
  return false;
}

const EVP_MD *checksum_digest(ChecksumAlgorithm algorithm) {
  switch (algorithm) {
  case ChecksumAlgorithm::none:
    return nullptr;
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
  }
  return nullptr;
}

std::string_view checksum_name(ChecksumAlgorithm algorithm) {
  switch (algorithm) {
  case ChecksumAlgorithm::none:
    return {};
  case ChecksumAlgorithm::sha1:
    return "sha-1";
  case ChecksumAlgorithm::sha256:
    return "sha-256";
  case ChecksumAlgorithm::sha512:
    return "sha-512";
  case ChecksumAlgorithm::sha3_256:
    return "sha3-256";
  case ChecksumAlgorithm::sha3_512:
    return "sha3-512";
  }
  return {};
}

bool is_valid_checksum_algorithm(ChecksumAlgorithm algorithm) {
  switch (algorithm) {
  case ChecksumAlgorithm::none:
  case ChecksumAlgorithm::sha1:
  case ChecksumAlgorithm::sha256:
  case ChecksumAlgorithm::sha512:
  case ChecksumAlgorithm::sha3_256:
  case ChecksumAlgorithm::sha3_512:
    return true;
  }
  return false;
}

Result<std::vector<std::byte>> shuffle_bytes(std::span<const std::byte> input,
                                             std::size_t item_size) {
  if (item_size == 0 || input.size() % item_size != 0) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer byte shuffle has invalid item geometry");
  }
  const auto item_count = input.size() / item_size;
  std::vector<std::byte> output(input.size());
  for (std::size_t byte = 0; byte < item_size; ++byte) {
    for (std::size_t item = 0; item < item_count; ++item) {
      output[byte * item_count + item] = input[item * item_size + byte];
    }
  }
  return output;
}

Result<std::vector<std::byte>> compress_bytes(std::span<const std::byte> input,
                                              CompressionCodec codec,
                                              std::uint64_t max_output_bytes,
                                              std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    return make_error(ErrorCode::cancelled, "XISF write was cancelled");
  }
  if (max_output_bytes == 0) {
    return make_error(ErrorCode::resource_limit,
                      "Writer compressed block has no remaining byte budget");
  }
  const auto bounded_capacity = [&](std::uint64_t codec_bound) {
    return static_cast<std::size_t>(std::min<std::uint64_t>(
        std::min<std::uint64_t>(codec_bound, max_output_bytes),
        std::numeric_limits<std::size_t>::max()));
  };
  std::vector<std::byte> output;
  switch (codec) {
  case CompressionCodec::zlib: {
    if (input.size() > std::numeric_limits<uLong>::max()) {
      return make_error(ErrorCode::resource_limit,
                        "Writer zlib input exceeds codec limit");
    }
    const auto bound = compressBound(static_cast<uLong>(input.size()));
    output.resize(bounded_capacity(bound));
    auto output_size = static_cast<uLongf>(output.size());
    const auto status =
        compress2(reinterpret_cast<Bytef *>(output.data()), &output_size,
                  reinterpret_cast<const Bytef *>(input.data()),
                  static_cast<uLong>(input.size()), Z_BEST_COMPRESSION);
    if (status == Z_BUF_ERROR) {
      return make_error(ErrorCode::resource_limit,
                        "Writer zlib output exceeds its byte budget");
    }
    if (status != Z_OK) {
      return make_error(ErrorCode::internal_error,
                        "Writer zlib compression failed");
    }
    output.resize(static_cast<std::size_t>(output_size));
    break;
  }
  case CompressionCodec::lz4:
  case CompressionCodec::lz4hc: {
    if (input.size() > static_cast<std::size_t>(LZ4_MAX_INPUT_SIZE)) {
      return make_error(ErrorCode::resource_limit,
                        "Writer LZ4 input exceeds codec limit");
    }
    const auto input_size = static_cast<int>(input.size());
    const auto bound = LZ4_compressBound(input_size);
    if (bound <= 0) {
      return make_error(ErrorCode::internal_error,
                        "Writer LZ4 compression bound failed");
    }
    const auto capacity = bounded_capacity(static_cast<std::uint64_t>(bound));
    output.resize(capacity);
    const auto produced =
        codec == CompressionCodec::lz4
            ? LZ4_compress_default(reinterpret_cast<const char *>(input.data()),
                                   reinterpret_cast<char *>(output.data()),
                                   input_size, static_cast<int>(capacity))
            : LZ4_compress_HC(reinterpret_cast<const char *>(input.data()),
                              reinterpret_cast<char *>(output.data()),
                              input_size, static_cast<int>(capacity),
                              LZ4HC_CLEVEL_DEFAULT);
    if (produced <= 0) {
      return make_error(capacity < static_cast<std::size_t>(bound)
                            ? ErrorCode::resource_limit
                            : ErrorCode::internal_error,
                        capacity < static_cast<std::size_t>(bound)
                            ? "Writer LZ4 output exceeds its byte budget"
                            : "Writer LZ4 compression failed");
    }
    output.resize(static_cast<std::size_t>(produced));
    break;
  }
  case CompressionCodec::zstd: {
    const auto bound = ZSTD_compressBound(input.size());
    if (ZSTD_isError(bound) != 0) {
      return make_error(ErrorCode::resource_limit,
                        "Writer Zstandard output exceeds codec limit");
    }
    const auto capacity = bounded_capacity(bound);
    output.resize(capacity);
    const auto produced = ZSTD_compress(output.data(), output.size(),
                                        input.data(), input.size(), 3);
    if (ZSTD_isError(produced) != 0) {
      return make_error(capacity < bound ? ErrorCode::resource_limit
                                         : ErrorCode::internal_error,
                        capacity < bound
                            ? "Writer Zstandard output exceeds its byte budget"
                            : "Writer Zstandard compression failed");
    }
    output.resize(produced);
    break;
  }
  case CompressionCodec::none:
    return make_error(ErrorCode::internal_error,
                      "Writer compression codec is missing");
  }
  if (stop_token.stop_requested()) {
    return make_error(ErrorCode::cancelled, "XISF write was cancelled");
  }
  return output;
}

Result<std::string> compute_checksum(std::span<const std::byte> bytes,
                                     ChecksumAlgorithm algorithm) {
  const auto *digest = checksum_digest(algorithm);
  const auto name = checksum_name(algorithm);
  if (digest == nullptr || name.empty()) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer checksum algorithm is invalid");
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
  unsigned int hash_size = 0;
  if (EVP_Digest(bytes.data(), bytes.size(), hash.data(), &hash_size, digest,
                 nullptr) != 1) {
    return make_error(ErrorCode::internal_error,
                      "Writer checksum computation failed");
  }
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(name);
  result += ':';
  result.reserve(result.size() + hash_size * 2U);
  for (unsigned int index = 0; index < hash_size; ++index) {
    result.push_back(digits[hash[index] >> 4U]);
    result.push_back(digits[hash[index] & 0x0fU]);
  }
  return result;
}

Result<std::string> compute_checksum_file(const std::filesystem::path &path,
                                          std::uint64_t expected_size,
                                          ChecksumAlgorithm algorithm,
                                          std::stop_token stop_token) {
  const auto *digest = checksum_digest(algorithm);
  const auto name = checksum_name(algorithm);
  if (digest == nullptr || name.empty()) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer checksum algorithm is invalid");
  }
  using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  DigestContext context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!context || EVP_DigestInit_ex(context.get(), digest, nullptr) != 1) {
    return make_error(ErrorCode::internal_error,
                      "Writer checksum initialization failed");
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return make_error(ErrorCode::io_error,
                      "Unable to read writer compression spool");
  }
  constexpr std::size_t kChunkBytes = 1024U * 1024U;
  std::vector<std::byte> buffer(static_cast<std::size_t>(
      std::min<std::uint64_t>(expected_size, kChunkBytes)));
  auto remaining = expected_size;
  while (remaining != 0) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "XISF write was cancelled");
    }
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(remaining, buffer.size()));
    input.read(reinterpret_cast<char *>(buffer.data()),
               static_cast<std::streamsize>(count));
    if (input.gcount() != static_cast<std::streamsize>(count) ||
        EVP_DigestUpdate(context.get(), buffer.data(), count) != 1) {
      return make_error(input ? ErrorCode::internal_error : ErrorCode::io_error,
                        input ? "Writer checksum update failed"
                              : "Writer compression spool is truncated");
    }
    remaining -= count;
  }
  if (input.peek() != std::char_traits<char>::eof()) {
    return make_error(ErrorCode::io_error,
                      "Writer compression spool has unexpected trailing data");
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
  unsigned int hash_size = 0;
  if (EVP_DigestFinal_ex(context.get(), hash.data(), &hash_size) != 1) {
    return make_error(ErrorCode::internal_error,
                      "Writer checksum finalization failed");
  }
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(name);
  result += ':';
  result.reserve(result.size() + hash_size * 2U);
  for (unsigned int index = 0; index < hash_size; ++index) {
    result.push_back(digits[hash[index] >> 4U]);
    result.push_back(digits[hash[index] & 0x0fU]);
  }
  return result;
}

Result<std::string>
make_header(std::span<const ImageWriteView> images,
            std::span<const MetadataWriteEntry> metadata,
            const WriterOptions &options,
            std::span<const PreparedBlock> prepared,
            std::span<const BlockLocation> image_blocks,
            std::span<const BlockLocation> metadata_blocks,
            std::span<const PreparedBlock> prepared_metadata) {
  auto escaped_creator = escape_xml(options.creator_application, false);
  if (!escaped_creator) {
    return escaped_creator.error();
  }
  std::string header = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
  header += "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">";
  for (std::size_t index = 0; index < images.size(); ++index) {
    const auto &image = images[index];
    auto escaped_id = escape_xml(image.id, true);
    if (!escaped_id) {
      return escaped_id.error();
    }
    const auto [sample_format, unused_sample_size] =
        sample_format_description(image.sample_format);
    static_cast<void>(unused_sample_size);
    header += "<Image";
    if (!image.id.empty()) {
      header += " id=\"" + escaped_id.value() + "\"";
    }
    header += " geometry=\"" + std::to_string(image.width) + ':' +
              std::to_string(image.height) + ':' +
              std::to_string(image.channels) + "\"";
    header += " sampleFormat=\"" + std::string(sample_format) +
              "\" colorSpace=\"" + image.color_space +
              "\" pixelStorage=\"Planar\" byteOrder=\"little\"";
    if (image.lower_bound && image.upper_bound) {
      auto lower = format_bound(*image.lower_bound);
      auto upper = format_bound(*image.upper_bound);
      if (!lower) {
        return lower.error();
      }
      if (!upper) {
        return upper.error();
      }
      header += " bounds=\"" + lower.value() + ':' + upper.value() + "\"";
    }
    if (!prepared[index].compression.empty()) {
      header += " compression=\"" + prepared[index].compression + "\"";
    }
    if (!prepared[index].subblocks.empty()) {
      header += " subblocks=\"" + prepared[index].subblocks + "\"";
    }
    if (!prepared[index].checksum.empty()) {
      header += " checksum=\"" + prepared[index].checksum + "\"";
    }
    header +=
        " location=\"attachment:" + std::to_string(image_blocks[index].offset) +
        ':' + std::to_string(image_blocks[index].size) + "\"";
    bool has_metadata = false;
    for (std::size_t metadata_index = 0; metadata_index < metadata.size();
         ++metadata_index) {
      const auto &entry = metadata[metadata_index];
      if (entry.image_index != index) {
        continue;
      }
      if (!has_metadata) {
        header += '>';
        has_metadata = true;
      }
      const auto *block = entry.value_form == MetadataWriteValueForm::data_block
                              ? &metadata_blocks[metadata_index]
                              : nullptr;
      const auto *prepared_property =
          entry.value_form == MetadataWriteValueForm::data_block
              ? &prepared_metadata[metadata_index]
              : nullptr;
      auto serialized = make_metadata_xml(entry, block, prepared_property);
      if (!serialized) {
        return serialized.error();
      }
      header += serialized.value();
    }
    header += has_metadata ? "</Image>" : "/>";
  }
  header += "<Metadata><Property id=\"XISF:CreationTime\" type=\"TimePoint\" "
            "value=\"" +
            options.creation_time + "\"/>";
  header += "<Property id=\"XISF:CreatorApplication\" type=\"String\">" +
            escaped_creator.value() + "</Property>";
  for (std::size_t metadata_index = 0; metadata_index < metadata.size();
       ++metadata_index) {
    const auto &entry = metadata[metadata_index];
    if (entry.image_index) {
      continue;
    }
    const auto *block = entry.value_form == MetadataWriteValueForm::data_block
                            ? &metadata_blocks[metadata_index]
                            : nullptr;
    const auto *prepared_property =
        entry.value_form == MetadataWriteValueForm::data_block
            ? &prepared_metadata[metadata_index]
            : nullptr;
    auto serialized = make_metadata_xml(entry, block, prepared_property);
    if (!serialized) {
      return serialized.error();
    }
    header += serialized.value();
  }
  header += "</Metadata></xisf>";
  return header;
}

class OstreamByteSink final : public ByteSink {
public:
  explicit OstreamByteSink(std::ofstream &output) : output_(output) {}

  Result<std::size_t> write(std::span<const std::byte> source) override {
    output_.write(reinterpret_cast<const char *>(source.data()),
                  static_cast<std::streamsize>(source.size()));
    if (!output_) {
      return make_error(ErrorCode::io_error, "Unable to write XISF bytes");
    }
    return source.size();
  }

  Result<void> flush() override {
    output_.flush();
    if (!output_) {
      return make_error(ErrorCode::io_error, "Unable to flush XISF bytes");
    }
    return {};
  }

private:
  std::ofstream &output_;
};

Result<std::uint64_t> write_all(ByteSink &output,
                                std::span<const std::byte> bytes,
                                std::stop_token stop_token) {
  constexpr std::size_t kChunkBytes = 8U * 1024U * 1024U;
  std::size_t written = 0;
  while (written < bytes.size()) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "XISF write was cancelled");
    }
    const auto count = std::min(kChunkBytes, bytes.size() - written);
    auto result = output.write(bytes.subspan(written, count));
    if (!result) {
      return result.error();
    }
    if (result.value() == 0 || result.value() > count) {
      return make_error(ErrorCode::io_error,
                        "ByteSink returned an invalid write count");
    }
    written += result.value();
  }
  return static_cast<std::uint64_t>(written);
}

Result<std::uint64_t> write_file_contents(ByteSink &output,
                                          const std::filesystem::path &path,
                                          std::uint64_t expected_size,
                                          std::stop_token stop_token) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return make_error(ErrorCode::io_error,
                      "Unable to read writer compression spool");
  }
  constexpr std::size_t kChunkBytes = 1024U * 1024U;
  std::vector<std::byte> buffer(static_cast<std::size_t>(
      std::min<std::uint64_t>(expected_size, kChunkBytes)));
  std::uint64_t written = 0;
  while (written < expected_size) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "XISF write was cancelled");
    }
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(expected_size - written, buffer.size()));
    input.read(reinterpret_cast<char *>(buffer.data()),
               static_cast<std::streamsize>(count));
    if (input.gcount() != static_cast<std::streamsize>(count)) {
      return make_error(ErrorCode::io_error,
                        "Writer compression spool is truncated");
    }
    auto copied = write_all(output, std::span(buffer).first(count), stop_token);
    if (!copied) {
      return copied.error();
    }
    written += count;
  }
  if (input.peek() != std::char_traits<char>::eof()) {
    return make_error(ErrorCode::io_error,
                      "Writer compression spool has unexpected trailing data");
  }
  return written;
}

class TemporaryFilesCleanup {
public:
  void track(std::filesystem::path path) { paths_.push_back(std::move(path)); }

  ~TemporaryFilesCleanup() {
    for (const auto &path : paths_) {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
  }

private:
  std::vector<std::filesystem::path> paths_;
};

bool path_is_available(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  return (!error && status.type() == std::filesystem::file_type::not_found) ||
         error == std::errc::no_such_file_or_directory;
}

} // namespace

Result<WriteSummary>
Writer::write_file(const std::filesystem::path &destination,
                   const ImageWriteView &image, const WriterOptions &options,
                   std::stop_token stop_token) {
  return write_file(destination, std::span(&image, 1), {}, options, stop_token);
}

Result<WriteSummary>
Writer::write_file(const std::filesystem::path &destination,
                   std::span<const ImageWriteView> images,
                   const WriterOptions &options, std::stop_token stop_token) {
  return write_file(destination, images, {}, options, stop_token);
}

namespace {

struct BlockPrepareRequest {
  std::span<const std::byte> bytes;
  std::size_t item_size{0};
  CompressionCodec compression{CompressionCodec::none};
  bool byte_shuffle{false};
  ChecksumAlgorithm checksum{ChecksumAlgorithm::none};
  std::uint64_t max_serialized_bytes{0};
  std::uint64_t max_cumulative_remaining{0};
  std::string spool_suffix;
};

Result<PreparedBlock> prepare_block(const std::filesystem::path &destination,
                                    const BlockPrepareRequest &request,
                                    const WriterOptions &options,
                                    TemporaryFilesCleanup &cleanup,
                                    std::stop_token stop_token) {
  PreparedBlock block;
  const auto decoded_size = static_cast<std::uint64_t>(request.bytes.size());
  if (request.compression != CompressionCodec::none) {
    auto chunk_limit = options.compression_subblock_bytes;
    if (request.compression == CompressionCodec::lz4 ||
        request.compression == CompressionCodec::lz4hc) {
      chunk_limit = std::min<std::uint64_t>(chunk_limit, LZ4_MAX_INPUT_SIZE);
    } else if (request.compression == CompressionCodec::zlib) {
      chunk_limit = std::min<std::uint64_t>(chunk_limit,
                                            std::numeric_limits<uLong>::max());
    }
    chunk_limit -= chunk_limit % request.item_size;
    if (chunk_limit < request.item_size) {
      return make_error(
          ErrorCode::invalid_argument,
          "Writer compression subblock size is smaller than one item");
    }

    std::vector<std::pair<std::uint64_t, std::uint64_t>> subblocks;
    const bool use_spool = decoded_size > chunk_limit;
    std::ofstream spool_output;
    if (use_spool) {
      if (destination.empty()) {
        return make_error(
            ErrorCode::invalid_argument,
            "ByteSink multi-subblock compression requires a scratch file "
            "stem");
      }
      block.spool_path = destination;
      block.spool_path += request.spool_suffix;
      if (!path_is_available(block.spool_path)) {
        return make_error(
            ErrorCode::io_error,
            "Writer compression spool already exists or cannot be checked");
      }
      cleanup.track(block.spool_path);
      spool_output.open(block.spool_path, std::ios::binary | std::ios::trunc);
      if (!spool_output) {
        return make_error(ErrorCode::io_error,
                          "Unable to create writer compression spool");
      }
    }
    OstreamByteSink spool_sink(spool_output);

    std::uint64_t input_offset = 0;
    std::uint64_t serialized_so_far = 0;
    while (input_offset < decoded_size) {
      if (stop_token.stop_requested()) {
        return make_error(ErrorCode::cancelled, "XISF write was cancelled");
      }
      if (subblocks.size() >= options.max_compression_subblocks) {
        return make_error(
            ErrorCode::resource_limit,
            "Writer compression subblock count exceeds its budget");
      }
      const auto uncompressed_size =
          std::min(chunk_limit, decoded_size - input_offset);
      const auto input =
          request.bytes.subspan(static_cast<std::size_t>(input_offset),
                                static_cast<std::size_t>(uncompressed_size));
      std::vector<std::byte> shuffled;
      std::span<const std::byte> compression_input = input;
      if (request.byte_shuffle) {
        auto result = shuffle_bytes(input, request.item_size);
        if (!result) {
          return result.error();
        }
        shuffled = std::move(result).value();
        compression_input = shuffled;
      }
      if (serialized_so_far >= request.max_serialized_bytes ||
          serialized_so_far >= request.max_cumulative_remaining) {
        return make_error(
            ErrorCode::resource_limit,
            "Writer compressed block has no remaining byte budget");
      }
      const auto compression_budget =
          std::min(request.max_serialized_bytes - serialized_so_far,
                   request.max_cumulative_remaining - serialized_so_far);
      auto compressed = compress_bytes(compression_input, request.compression,
                                       compression_budget, stop_token);
      if (!compressed) {
        return compressed.error();
      }
      const auto compressed_size =
          static_cast<std::uint64_t>(compressed.value().size());
      if (use_spool) {
        auto spooled = write_all(spool_sink, compressed.value(), stop_token);
        if (!spooled) {
          return spooled.error();
        }
      } else {
        block.storage = std::move(compressed).value();
      }
      subblocks.emplace_back(compressed_size, uncompressed_size);
      serialized_so_far += compressed_size;
      input_offset += uncompressed_size;
    }
    if (use_spool) {
      auto flushed = spool_sink.flush();
      if (!flushed) {
        return flushed.error();
      }
      spool_output.close();
      if (!spool_output) {
        return make_error(ErrorCode::io_error,
                          "Unable to close writer compression spool");
      }
    }
    block.serialized_size = serialized_so_far;
    block.compression = std::string(compression_name(request.compression));
    if (request.byte_shuffle) {
      block.compression += "+sh";
    }
    block.compression += ':' + std::to_string(decoded_size);
    if (request.byte_shuffle) {
      block.compression += ':' + std::to_string(request.item_size);
    }
    if (subblocks.size() > 1) {
      for (const auto &[compressed_size, uncompressed_size] : subblocks) {
        if (!block.subblocks.empty()) {
          block.subblocks += ':';
        }
        block.subblocks += std::to_string(compressed_size) + ',' +
                           std::to_string(uncompressed_size);
      }
    }
  } else {
    block.serialized_size = decoded_size;
  }
  if (block.serialized_size > request.max_serialized_bytes ||
      block.serialized_size > request.max_cumulative_remaining) {
    return make_error(ErrorCode::resource_limit,
                      "Writer serialized block exceeds its byte budget");
  }
  if (request.checksum != ChecksumAlgorithm::none) {
    auto checksum =
        block.spool_path.empty()
            ? compute_checksum(block.storage.empty()
                                   ? request.bytes
                                   : std::span<const std::byte>(block.storage),
                               request.checksum)
            : compute_checksum_file(block.spool_path, block.serialized_size,
                                    request.checksum, stop_token);
    if (!checksum) {
      return checksum.error();
    }
    block.checksum = std::move(checksum).value();
  }
  return block;
}

Result<WriteSummary>
write_impl(const std::filesystem::path &destination_or_scratch,
           ByteSink *external_sink, std::span<const ImageWriteView> images,
           std::span<const MetadataWriteEntry> metadata,
           const WriterOptions &options, std::stop_token stop_token) {
  const bool writes_file = external_sink == nullptr;
  if (stop_token.stop_requested()) {
    return make_error(ErrorCode::cancelled, "XISF write was cancelled");
  }
  if (writes_file && destination_or_scratch.empty()) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer destination cannot be empty");
  }
  if (images.empty()) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer requires at least one image");
  }
  if (images.size() > options.max_images) {
    return make_error(ErrorCode::resource_limit,
                      "Writer image count exceeds its budget");
  }
  if (options.max_metadata_entries < 2 ||
      metadata.size() > options.max_metadata_entries - 2) {
    return make_error(ErrorCode::resource_limit,
                      "Writer metadata count exceeds its budget");
  }
  if (!is_power_of_two(options.attachment_alignment) ||
      options.attachment_alignment < 16 ||
      options.attachment_alignment > 1024U * 1024U) {
    return make_error(
        ErrorCode::invalid_argument,
        "Writer alignment must be a power of two from 16 to 1 MiB");
  }
  if (options.compression_subblock_bytes == 0 ||
      options.compression_subblock_bytes >
          std::numeric_limits<std::size_t>::max()) {
    return make_error(
        ErrorCode::invalid_argument,
        "Writer compression subblock size must fit a nonzero size_t");
  }
  if (options.max_compression_subblocks == 0) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer compression subblock count must be nonzero");
  }
  if (!is_canonical_utc_time(options.creation_time)) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer creation time must be YYYY-MM-DDTHH:MM:SSZ");
  }
  if (options.creator_application.empty()) {
    return make_error(ErrorCode::invalid_argument,
                      "Writer creator application cannot be empty");
  }
  if (options.creator_application.size() > options.max_metadata_value_bytes) {
    return make_error(ErrorCode::resource_limit,
                      "Writer creator application exceeds metadata budget");
  }

  std::unordered_set<std::string> unit_property_ids{"XISF:CreationTime",
                                                    "XISF:CreatorApplication"};
  std::vector<std::unordered_set<std::string>> image_property_ids(
      images.size());
  std::uint64_t cumulative_property_bytes = 0;
  for (std::size_t metadata_index = 0; metadata_index < metadata.size();
       ++metadata_index) {
    const auto &entry = metadata[metadata_index];
    if (entry.kind != MetadataWriteKind::property &&
        entry.kind != MetadataWriteKind::fits_keyword) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer metadata kind is invalid");
    }
    if (entry.image_index && *entry.image_index >= images.size()) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer metadata image index is out of range");
    }
    if (entry.value.size() > options.max_metadata_value_bytes ||
        entry.comment.size() > options.max_metadata_value_bytes ||
        entry.format.size() > options.max_metadata_value_bytes) {
      return make_error(ErrorCode::resource_limit,
                        "Writer metadata value exceeds its byte budget");
    }
    if (entry.kind == MetadataWriteKind::fits_keyword) {
      if (entry.value_form != MetadataWriteValueForm::direct || entry.length ||
          entry.rows || entry.columns || !entry.format.empty() ||
          !entry.block_bytes.empty() ||
          entry.compression != CompressionCodec::none || entry.byte_shuffle ||
          entry.checksum != ChecksumAlgorithm::none) {
        return make_error(ErrorCode::invalid_argument,
                          "Writer FITS keywords cannot use Property blocks");
      }
      if (!entry.image_index) {
        return make_error(ErrorCode::invalid_argument,
                          "Writer FITS keywords require an image index");
      }
      if (!entry.type.empty()) {
        return make_error(ErrorCode::invalid_argument,
                          "Writer FITS keywords cannot declare a type");
      }
      if (!is_valid_fits_keyword_name(entry.name)) {
        return make_error(ErrorCode::invalid_argument,
                          "Writer FITS keyword name has invalid syntax");
      }
    } else {
      if (!is_valid_property_identifier(entry.name)) {
        return make_error(ErrorCode::invalid_argument,
                          "Writer Property identifier has invalid syntax");
      }
      if (!entry.comment.empty()) {
        return make_error(ErrorCode::invalid_argument,
                          "Writer Properties cannot declare a FITS comment");
      }
      if (entry.value_form == MetadataWriteValueForm::direct) {
        if (entry.length || entry.rows || entry.columns ||
            !entry.format.empty() || !entry.block_bytes.empty() ||
            entry.byte_order != ByteOrder::little ||
            entry.compression != CompressionCodec::none || entry.byte_shuffle ||
            entry.checksum != ChecksumAlgorithm::none) {
          return make_error(
              ErrorCode::invalid_argument,
              "Writer direct Properties cannot declare block fields");
        }
        if (entry.type != "String" && entry.type != "TimePoint" &&
            !is_supported_scalar_property_type(entry.type)) {
          return make_error(ErrorCode::unsupported_feature,
                            "Writer metadata Property type is not supported");
        }
        if (entry.type == "TimePoint" && !is_canonical_utc_time(entry.value)) {
          return make_error(
              ErrorCode::invalid_argument,
              "Writer TimePoint values must be YYYY-MM-DDTHH:MM:SSZ");
        }
        if (is_supported_scalar_property_type(entry.type) &&
            !is_valid_scalar_property(entry.type, entry.value)) {
          return make_error(
              ErrorCode::invalid_argument,
              "Writer scalar Property value is invalid or out of range");
        }
      } else if (entry.value_form == MetadataWriteValueForm::data_block) {
        if (!entry.value.empty()) {
          return make_error(ErrorCode::invalid_argument,
                            "Writer block Properties cannot declare a value");
        }
        const auto layout = property_element_layout(entry.type);
        if (!layout) {
          return make_error(
              ErrorCode::unsupported_feature,
              "Writer block Property element type is not supported");
        }
        std::uint64_t element_count = 0;
        const auto category = classify_property_type(entry.type);
        if (category == PropertyCategory::vector) {
          if (!entry.length || *entry.length == 0 || entry.rows ||
              entry.columns) {
            return make_error(
                ErrorCode::invalid_argument,
                "Writer vector Properties require a nonzero length only");
          }
          element_count = *entry.length;
        } else if (category == PropertyCategory::matrix) {
          if (entry.length || !entry.rows || !entry.columns ||
              *entry.rows == 0 || *entry.columns == 0) {
            return make_error(
                ErrorCode::invalid_argument,
                "Writer matrix Properties require nonzero rows and columns");
          }
          if (!checked_multiply(*entry.rows, *entry.columns, element_count)) {
            return make_error(ErrorCode::overflow,
                              "Writer Property matrix extent overflows");
          }
        } else {
          return make_error(ErrorCode::internal_error,
                            "Writer Property category is inconsistent");
        }
        std::uint64_t expected_bytes = 0;
        if (!checked_multiply(element_count, layout->element_size,
                              expected_bytes)) {
          return make_error(ErrorCode::overflow,
                            "Writer Property byte count overflows");
        }
        if (expected_bytes != entry.block_bytes.size()) {
          return make_error(
              ErrorCode::invalid_argument,
              "Writer Property byte span does not match its typed extent");
        }
        if (expected_bytes > options.max_property_bytes) {
          return make_error(ErrorCode::resource_limit,
                            "Writer Property exceeds its byte budget");
        }
        if (!checked_add(cumulative_property_bytes, expected_bytes,
                         cumulative_property_bytes) ||
            cumulative_property_bytes > options.max_cumulative_property_bytes) {
          return make_error(
              ErrorCode::resource_limit,
              "Writer Properties exceed their cumulative byte budget");
        }
        if (entry.byte_order != ByteOrder::little &&
            entry.byte_order != ByteOrder::big) {
          return make_error(ErrorCode::invalid_argument,
                            "Writer Property byte order is invalid");
        }
        if (!is_valid_compression_codec(entry.compression)) {
          return make_error(ErrorCode::invalid_argument,
                            "Writer Property compression codec is invalid");
        }
        if (!is_valid_checksum_algorithm(entry.checksum)) {
          return make_error(ErrorCode::invalid_argument,
                            "Writer Property checksum algorithm is invalid");
        }
        if (entry.byte_shuffle && entry.compression == CompressionCodec::none) {
          return make_error(
              ErrorCode::invalid_argument,
              "Writer Property byte shuffle requires compression");
        }
        auto escaped_format = escape_xml(entry.format, true);
        if (!escaped_format) {
          return escaped_format.error();
        }
      } else {
        return make_error(ErrorCode::invalid_argument,
                          "Writer Property value form is invalid");
      }
      auto &property_ids = entry.image_index
                               ? image_property_ids[*entry.image_index]
                               : unit_property_ids;
      if (!entry.image_index && !entry.name.starts_with("XISF:")) {
        return make_error(
            ErrorCode::invalid_argument,
            "Writer XISF-unit Property identifiers require XISF namespace");
      }
      if (!property_ids.emplace(entry.name).second) {
        return make_error(
            ErrorCode::invalid_argument,
            "Writer Property identifiers must be unique per association");
      }
    }
    if (entry.value_form == MetadataWriteValueForm::direct) {
      auto serialized = make_metadata_xml(entry);
      if (!serialized) {
        return serialized.error();
      }
    }
  }

  std::vector<std::uint64_t> pixel_sizes;
  pixel_sizes.reserve(images.size());
  std::uint64_t cumulative_pixel_bytes = 0;
  for (const auto &image : images) {
    const auto [sample_name, sample_size] =
        sample_format_description(image.sample_format);
    if (sample_name.empty() || image.pixel_storage != PixelStorage::planar ||
        image.byte_order != ByteOrder::little ||
        (image.color_space != "Gray" && image.color_space != "RGB")) {
      return make_error(
          ErrorCode::unsupported_feature,
          "Writer supports little-endian Planar UInt8, UInt16, UInt32, "
          "Float32, or Float64 Gray and RGB images");
    }
    if (!is_valid_compression_codec(image.compression)) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer image compression codec is invalid");
    }
    if (!is_valid_checksum_algorithm(image.checksum)) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer image checksum algorithm is invalid");
    }
    if (image.byte_shuffle && image.compression == CompressionCodec::none) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer byte shuffle requires compression");
    }
    if (image.width == 0 || image.height == 0 ||
        (image.color_space == "Gray" ? image.channels != 1
                                     : image.channels != 3)) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer image geometry does not match its color space");
    }
    if (image.lower_bound.has_value() != image.upper_bound.has_value()) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer image bounds must be provided as a pair");
    }
    const bool floating_point = image.sample_format == SampleFormat::float32 ||
                                image.sample_format == SampleFormat::float64;
    if (floating_point && !image.lower_bound) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer floating-point images require bounds");
    }
    if (image.lower_bound && (!std::isfinite(*image.lower_bound) ||
                              !std::isfinite(*image.upper_bound) ||
                              *image.lower_bound >= *image.upper_bound)) {
      return make_error(
          ErrorCode::invalid_argument,
          "Writer image bounds must be finite and strictly increasing");
    }

    std::uint64_t sample_count = 0;
    std::uint64_t pixel_bytes = 0;
    if (!checked_multiply(image.width, image.height, sample_count) ||
        !checked_multiply(sample_count, image.channels, sample_count) ||
        !checked_multiply(sample_count, sample_size, pixel_bytes)) {
      return make_error(ErrorCode::overflow,
                        "Writer image byte count overflows");
    }
    if (pixel_bytes > options.max_image_bytes) {
      return make_error(ErrorCode::resource_limit,
                        "Writer image exceeds its byte budget");
    }
    if (!checked_add(cumulative_pixel_bytes, pixel_bytes,
                     cumulative_pixel_bytes) ||
        cumulative_pixel_bytes > options.max_cumulative_image_bytes) {
      return make_error(ErrorCode::resource_limit,
                        "Writer images exceed their cumulative byte budget");
    }
    if (pixel_bytes != image.pixels.size()) {
      return make_error(ErrorCode::invalid_argument,
                        "Writer pixel span size does not match image geometry");
    }
    pixel_sizes.push_back(pixel_bytes);
  }

  std::filesystem::path temporary;
  TemporaryFilesCleanup cleanup;
  if (writes_file) {
    if (!path_is_available(destination_or_scratch)) {
      return make_error(
          ErrorCode::io_error,
          "Writer destination already exists or cannot be checked");
    }
    temporary = destination_or_scratch;
    temporary += ".mmxisf-tmp";
    if (!path_is_available(temporary)) {
      return make_error(
          ErrorCode::io_error,
          "Writer temporary path already exists or cannot be checked");
    }
    cleanup.track(temporary);
  }

  std::vector<PreparedBlock> prepared(images.size());
  std::uint64_t cumulative_serialized_bytes = 0;
  for (std::size_t index = 0; index < images.size(); ++index) {
    const auto &image = images[index];
    if (cumulative_serialized_bytes > options.max_cumulative_serialized_bytes) {
      return make_error(
          ErrorCode::resource_limit,
          "Writer images exceed their cumulative serialized-byte budget");
    }
    const auto sample_size = static_cast<std::size_t>(
        sample_format_description(image.sample_format).second);
    BlockPrepareRequest request{
        .bytes = image.pixels,
        .item_size = sample_size,
        .compression = image.compression,
        .byte_shuffle = image.byte_shuffle,
        .checksum = image.checksum,
        .max_serialized_bytes = options.max_serialized_image_bytes,
        .max_cumulative_remaining = options.max_cumulative_serialized_bytes -
                                    cumulative_serialized_bytes,
        .spool_suffix = ".mmxisf-block-" + std::to_string(index) + "-tmp"};
    auto block = prepare_block(destination_or_scratch, request, options,
                               cleanup, stop_token);
    if (!block) {
      return block.error();
    }
    prepared[index] = std::move(block).value();
    if (!checked_add(cumulative_serialized_bytes,
                     prepared[index].serialized_size,
                     cumulative_serialized_bytes) ||
        cumulative_serialized_bytes > options.max_cumulative_serialized_bytes) {
      return make_error(
          ErrorCode::resource_limit,
          "Writer images exceed their cumulative serialized-byte budget");
    }
  }

  std::vector<PreparedBlock> prepared_metadata(metadata.size());
  std::uint64_t cumulative_serialized_property_bytes = 0;
  for (std::size_t index = 0; index < metadata.size(); ++index) {
    const auto &entry = metadata[index];
    if (entry.value_form != MetadataWriteValueForm::data_block) {
      continue;
    }
    if (cumulative_serialized_property_bytes >
        options.max_cumulative_serialized_property_bytes) {
      return make_error(
          ErrorCode::resource_limit,
          "Writer Properties exceed their cumulative serialized-byte budget");
    }
    const auto layout = property_element_layout(entry.type);
    if (!layout) {
      return make_error(ErrorCode::internal_error,
                        "Writer Property layout was not retained");
    }
    BlockPrepareRequest request{
        .bytes = entry.block_bytes,
        .item_size = static_cast<std::size_t>(layout->element_size),
        .compression = entry.compression,
        .byte_shuffle = entry.byte_shuffle,
        .checksum = entry.checksum,
        .max_serialized_bytes = options.max_serialized_property_bytes,
        .max_cumulative_remaining =
            options.max_cumulative_serialized_property_bytes -
            cumulative_serialized_property_bytes,
        .spool_suffix =
            ".mmxisf-property-block-" + std::to_string(index) + "-tmp"};
    auto block = prepare_block(destination_or_scratch, request, options,
                               cleanup, stop_token);
    if (!block) {
      return block.error();
    }
    prepared_metadata[index] = std::move(block).value();
    if (!checked_add(cumulative_serialized_property_bytes,
                     prepared_metadata[index].serialized_size,
                     cumulative_serialized_property_bytes) ||
        cumulative_serialized_property_bytes >
            options.max_cumulative_serialized_property_bytes) {
      return make_error(
          ErrorCode::resource_limit,
          "Writer Properties exceed their cumulative serialized-byte budget");
    }
  }

  std::vector<BlockLocation> image_blocks(images.size());
  std::vector<BlockLocation> metadata_blocks(metadata.size());
  const auto property_block_count = static_cast<std::size_t>(std::count_if(
      metadata.begin(), metadata.end(), [](const MetadataWriteEntry &entry) {
        return entry.value_form == MetadataWriteValueForm::data_block;
      }));
  const auto plan_blocks =
      [&](std::uint64_t first_offset) -> Result<std::uint64_t> {
    auto offset = first_offset;
    std::size_t planned = 0;
    const auto total_blocks = images.size() + property_block_count;
    const auto plan_one = [&](BlockLocation &block,
                              std::uint64_t size) -> Result<std::uint64_t> {
      block.kind = BlockKind::attachment;
      block.offset = offset;
      block.size = size;
      block.raw =
          "attachment:" + std::to_string(offset) + ':' + std::to_string(size);
      std::uint64_t end = 0;
      if (!checked_add(offset, size, end)) {
        return make_error(ErrorCode::overflow, "Writer file layout overflows");
      }
      ++planned;
      if (planned == total_blocks) {
        return end;
      }
      auto aligned = align_up(end, options.attachment_alignment);
      if (!aligned) {
        return aligned.error();
      }
      offset = aligned.value();
      return offset;
    };
    for (std::size_t index = 0; index < images.size(); ++index) {
      auto result =
          plan_one(image_blocks[index], prepared[index].serialized_size);
      if (!result) {
        return result.error();
      }
      if (planned == total_blocks) {
        return result.value();
      }
    }
    for (std::size_t index = 0; index < metadata.size(); ++index) {
      if (metadata[index].value_form != MetadataWriteValueForm::data_block) {
        continue;
      }
      auto result = plan_one(metadata_blocks[index],
                             prepared_metadata[index].serialized_size);
      if (!result) {
        return result.error();
      }
      if (planned == total_blocks) {
        return result.value();
      }
    }
    return make_error(ErrorCode::internal_error,
                      "Writer block planner did not finish");
  };

  std::uint64_t attachment_offset = options.attachment_alignment;
  auto file_size_result = plan_blocks(attachment_offset);
  if (!file_size_result) {
    return file_size_result.error();
  }
  std::string header;
  bool layout_stable = false;
  for (unsigned iteration = 0; iteration < 4; ++iteration) {
    auto candidate =
        make_header(images, metadata, options, prepared, image_blocks,
                    metadata_blocks, prepared_metadata);
    if (!candidate) {
      return candidate.error();
    }
    header = std::move(candidate).value();
    std::uint64_t header_end = 0;
    if (!checked_add(16, header.size(), header_end)) {
      return make_error(ErrorCode::overflow, "Writer header size overflows");
    }
    auto aligned = align_up(header_end, options.attachment_alignment);
    if (!aligned) {
      return aligned.error();
    }
    if (aligned.value() == attachment_offset) {
      layout_stable = true;
      break;
    }
    attachment_offset = aligned.value();
    file_size_result = plan_blocks(attachment_offset);
    if (!file_size_result) {
      return file_size_result.error();
    }
  }
  if (!layout_stable) {
    return make_error(ErrorCode::internal_error,
                      "Writer could not stabilize its attachment layout");
  }
  if (header.size() > options.max_header_bytes ||
      header.size() > std::numeric_limits<std::uint32_t>::max()) {
    return make_error(ErrorCode::resource_limit,
                      "Writer header exceeds its byte budget");
  }
  std::uint64_t header_end = 0;
  if (!checked_add(16, header.size(), header_end) ||
      attachment_offset < header_end) {
    return make_error(ErrorCode::overflow, "Writer file layout overflows");
  }
  const auto file_size = file_size_result.value();

  std::ofstream output_file;
  std::unique_ptr<OstreamByteSink> file_sink;
  ByteSink *output = external_sink;
  if (writes_file) {
    output_file.open(temporary, std::ios::binary | std::ios::trunc);
    if (!output_file) {
      return make_error(ErrorCode::io_error,
                        "Unable to create temporary XISF file");
    }
    file_sink = std::make_unique<OstreamByteSink>(output_file);
    output = file_sink.get();
  }
  if (output == nullptr) {
    return make_error(ErrorCode::internal_error,
                      "Writer output sink was not initialized");
  }

  std::array<std::byte, 16> preamble{
      std::byte{'X'}, std::byte{'I'}, std::byte{'S'}, std::byte{'F'},
      std::byte{'0'}, std::byte{'1'}, std::byte{'0'}, std::byte{'0'}};
  const auto header_length = static_cast<std::uint32_t>(header.size());
  preamble[8] = static_cast<std::byte>(header_length & 0xffU);
  preamble[9] = static_cast<std::byte>((header_length >> 8U) & 0xffU);
  preamble[10] = static_cast<std::byte>((header_length >> 16U) & 0xffU);
  preamble[11] = static_cast<std::byte>((header_length >> 24U) & 0xffU);
  auto preamble_written = write_all(*output, preamble, stop_token);
  if (!preamble_written) {
    return preamble_written.error();
  }
  const auto header_bytes = std::as_bytes(std::span(header));
  auto header_written = write_all(*output, header_bytes, stop_token);
  if (!header_written) {
    return header_written.error();
  }
  std::array<std::byte, 4096> zeros{};
  auto output_position = header_end;
  for (std::size_t index = 0; index < images.size(); ++index) {
    auto padding = image_blocks[index].offset - output_position;
    while (padding != 0) {
      const auto count = static_cast<std::size_t>(
          std::min<std::uint64_t>(padding, zeros.size()));
      auto padding_written =
          write_all(*output, std::span(zeros).first(count), stop_token);
      if (!padding_written) {
        return padding_written.error();
      }
      padding -= count;
    }
    const auto &prepared_block = prepared[index];
    auto pixels_written =
        prepared_block.spool_path.empty()
            ? write_all(
                  *output,
                  prepared_block.storage.empty()
                      ? images[index].pixels
                      : std::span<const std::byte>(prepared_block.storage),
                  stop_token)
            : write_file_contents(*output, prepared_block.spool_path,
                                  prepared_block.serialized_size, stop_token);
    if (!pixels_written) {
      return pixels_written.error();
    }
    output_position = image_blocks[index].offset + image_blocks[index].size;
  }
  for (std::size_t index = 0; index < metadata.size(); ++index) {
    if (metadata[index].value_form != MetadataWriteValueForm::data_block) {
      continue;
    }
    auto padding = metadata_blocks[index].offset - output_position;
    while (padding != 0) {
      const auto count = static_cast<std::size_t>(
          std::min<std::uint64_t>(padding, zeros.size()));
      auto padding_written =
          write_all(*output, std::span(zeros).first(count), stop_token);
      if (!padding_written) {
        return padding_written.error();
      }
      padding -= count;
    }
    const auto &prepared_block = prepared_metadata[index];
    auto property_written =
        prepared_block.spool_path.empty()
            ? write_all(
                  *output,
                  prepared_block.storage.empty()
                      ? metadata[index].block_bytes
                      : std::span<const std::byte>(prepared_block.storage),
                  stop_token)
            : write_file_contents(*output, prepared_block.spool_path,
                                  prepared_block.serialized_size, stop_token);
    if (!property_written) {
      return property_written.error();
    }
    output_position =
        metadata_blocks[index].offset + metadata_blocks[index].size;
  }
  auto flushed = output->flush();
  if (!flushed) {
    return flushed.error();
  }
  if (writes_file) {
    output_file.close();
    if (!output_file) {
      return make_error(ErrorCode::io_error, "Unable to close XISF file");
    }
  }
  if (stop_token.stop_requested()) {
    return make_error(ErrorCode::cancelled, "XISF write was cancelled");
  }
  if (writes_file) {
    std::error_code filesystem_error;
    std::filesystem::create_hard_link(temporary, destination_or_scratch,
                                      filesystem_error);
    if (filesystem_error) {
      return make_error(ErrorCode::io_error,
                        "Unable to commit temporary XISF file");
    }
  }
  WriteSummary summary;
  summary.file_size = file_size;
  summary.header_length = header_length;
  summary.image_block = image_blocks.front();
  summary.image_blocks = std::move(image_blocks);
  summary.property_blocks.reserve(property_block_count);
  for (std::size_t index = 0; index < metadata.size(); ++index) {
    if (metadata[index].value_form == MetadataWriteValueForm::data_block) {
      summary.property_blocks.push_back(std::move(metadata_blocks[index]));
    }
  }
  return summary;
}

} // namespace

Result<WriteSummary>
Writer::write_file(const std::filesystem::path &destination,
                   std::span<const ImageWriteView> images,
                   std::span<const MetadataWriteEntry> metadata,
                   const WriterOptions &options, std::stop_token stop_token) {
  try {
    return write_impl(destination, nullptr, images, metadata, options,
                      stop_token);
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Writer could not allocate within configured budgets");
  } catch (const std::exception &) {
    return make_error(ErrorCode::internal_error,
                      "Writer dependency raised an unexpected exception");
  } catch (...) {
    return make_error(ErrorCode::internal_error,
                      "Writer failed with an unexpected exception");
  }
}

Result<WriteSummary> Writer::write_to(ByteSink &destination,
                                      const ImageWriteView &image,
                                      const WriterOptions &options,
                                      const SinkWriteOptions &sink_options,
                                      std::stop_token stop_token) {
  return write_to(destination, std::span(&image, 1), {}, options, sink_options,
                  stop_token);
}

Result<WriteSummary> Writer::write_to(ByteSink &destination,
                                      std::span<const ImageWriteView> images,
                                      const WriterOptions &options,
                                      const SinkWriteOptions &sink_options,
                                      std::stop_token stop_token) {
  return write_to(destination, images, {}, options, sink_options, stop_token);
}

Result<WriteSummary> Writer::write_to(
    ByteSink &destination, std::span<const ImageWriteView> images,
    std::span<const MetadataWriteEntry> metadata, const WriterOptions &options,
    const SinkWriteOptions &sink_options, std::stop_token stop_token) {
  try {
    return write_impl(sink_options.scratch_file_stem, &destination, images,
                      metadata, options, stop_token);
  } catch (const std::bad_alloc &) {
    return make_error(ErrorCode::resource_limit,
                      "Writer could not allocate within configured budgets");
  } catch (const std::exception &) {
    return make_error(ErrorCode::internal_error,
                      "Writer dependency raised an unexpected exception");
  } catch (...) {
    return make_error(ErrorCode::internal_error,
                      "Writer failed with an unexpected exception");
  }
}

} // namespace mmxisf
