// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/writer.hpp"

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
#include <new>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace mmxisf {
namespace {

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

Result<std::string> make_metadata_xml(const MetadataWriteEntry &entry) {
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
  if (entry.type == "String") {
    return "<Property id=\"" + escaped_name.value() + "\" type=\"String\">" +
           escaped_value.value() + "</Property>";
  }
  return "<Property id=\"" + escaped_name.value() +
         "\" type=\"TimePoint\" value=\"" + escaped_value.value() + "\"/>";
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

struct PreparedImageBlock {
  std::vector<std::byte> storage;
  std::uint64_t serialized_size{0};
  std::string compression;
  std::string checksum;
};

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
  }
  return {};
}

bool is_valid_checksum_algorithm(ChecksumAlgorithm algorithm) {
  switch (algorithm) {
  case ChecksumAlgorithm::none:
  case ChecksumAlgorithm::sha1:
  case ChecksumAlgorithm::sha256:
  case ChecksumAlgorithm::sha512:
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

Result<std::string> make_header(std::span<const ImageWriteView> images,
                                std::span<const MetadataWriteEntry> metadata,
                                const WriterOptions &options,
                                std::span<const PreparedImageBlock> prepared,
                                std::span<const BlockLocation> image_blocks) {
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
    if (!prepared[index].checksum.empty()) {
      header += " checksum=\"" + prepared[index].checksum + "\"";
    }
    header +=
        " location=\"attachment:" + std::to_string(image_blocks[index].offset) +
        ':' + std::to_string(image_blocks[index].size) + "\"";
    bool has_metadata = false;
    for (const auto &entry : metadata) {
      if (entry.image_index != index) {
        continue;
      }
      if (!has_metadata) {
        header += '>';
        has_metadata = true;
      }
      auto serialized = make_metadata_xml(entry);
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
  for (const auto &entry : metadata) {
    if (entry.image_index) {
      continue;
    }
    auto serialized = make_metadata_xml(entry);
    if (!serialized) {
      return serialized.error();
    }
    header += serialized.value();
  }
  header += "</Metadata></xisf>";
  return header;
}

Result<std::size_t> write_all(std::ofstream &output,
                              std::span<const std::byte> bytes,
                              std::stop_token stop_token) {
  constexpr std::size_t kChunkBytes = 8U * 1024U * 1024U;
  std::size_t written = 0;
  while (written < bytes.size()) {
    if (stop_token.stop_requested()) {
      return make_error(ErrorCode::cancelled, "XISF write was cancelled");
    }
    const auto count = std::min(kChunkBytes, bytes.size() - written);
    output.write(reinterpret_cast<const char *>(bytes.data() + written),
                 static_cast<std::streamsize>(count));
    if (!output) {
      return make_error(ErrorCode::io_error, "Unable to write XISF bytes");
    }
    written += count;
  }
  return written;
}

class TemporaryFileCleanup {
public:
  explicit TemporaryFileCleanup(std::filesystem::path path)
      : path_(std::move(path)) {}
  ~TemporaryFileCleanup() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

private:
  std::filesystem::path path_;
};

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

Result<WriteSummary>
write_file_impl(const std::filesystem::path &destination,
                std::span<const ImageWriteView> images,
                std::span<const MetadataWriteEntry> metadata,
                const WriterOptions &options, std::stop_token stop_token) {
  if (stop_token.stop_requested()) {
    return make_error(ErrorCode::cancelled, "XISF write was cancelled");
  }
  if (destination.empty()) {
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
  for (const auto &entry : metadata) {
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
        entry.comment.size() > options.max_metadata_value_bytes) {
      return make_error(ErrorCode::resource_limit,
                        "Writer metadata value exceeds its byte budget");
    }
    if (entry.kind == MetadataWriteKind::fits_keyword) {
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
      if (entry.type != "String" && entry.type != "TimePoint") {
        return make_error(
            ErrorCode::unsupported_feature,
            "Writer metadata supports String and TimePoint Properties");
      }
      if (!entry.comment.empty()) {
        return make_error(ErrorCode::invalid_argument,
                          "Writer Properties cannot declare a FITS comment");
      }
      if (entry.type == "TimePoint" && !is_canonical_utc_time(entry.value)) {
        return make_error(
            ErrorCode::invalid_argument,
            "Writer TimePoint values must be YYYY-MM-DDTHH:MM:SSZ");
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
    auto serialized = make_metadata_xml(entry);
    if (!serialized) {
      return serialized.error();
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

  std::vector<PreparedImageBlock> prepared(images.size());
  std::uint64_t cumulative_serialized_bytes = 0;
  for (std::size_t index = 0; index < images.size(); ++index) {
    const auto &image = images[index];
    auto &block = prepared[index];
    std::vector<std::byte> shuffled;
    std::span<const std::byte> compression_input = image.pixels;
    if (image.byte_shuffle) {
      const auto sample_size =
          sample_format_description(image.sample_format).second;
      auto result =
          shuffle_bytes(image.pixels, static_cast<std::size_t>(sample_size));
      if (!result) {
        return result.error();
      }
      shuffled = std::move(result).value();
      compression_input = shuffled;
    }
    if (image.compression != CompressionCodec::none) {
      if (cumulative_serialized_bytes >
          options.max_cumulative_serialized_bytes) {
        return make_error(
            ErrorCode::resource_limit,
            "Writer images exceed their cumulative serialized-byte budget");
      }
      const auto remaining_cumulative =
          options.max_cumulative_serialized_bytes - cumulative_serialized_bytes;
      const auto compression_budget =
          std::min(options.max_serialized_image_bytes, remaining_cumulative);
      auto compressed = compress_bytes(compression_input, image.compression,
                                       compression_budget, stop_token);
      if (!compressed) {
        return compressed.error();
      }
      block.storage = std::move(compressed).value();
      block.serialized_size = block.storage.size();
      block.compression = std::string(compression_name(image.compression));
      if (image.byte_shuffle) {
        block.compression += "+sh";
      }
      block.compression += ':' + std::to_string(pixel_sizes[index]);
      if (image.byte_shuffle) {
        block.compression +=
            ':' + std::to_string(
                      sample_format_description(image.sample_format).second);
      }
    } else {
      block.serialized_size = pixel_sizes[index];
    }
    if (block.serialized_size > options.max_serialized_image_bytes) {
      return make_error(ErrorCode::resource_limit,
                        "Writer serialized image exceeds its byte budget");
    }
    if (!checked_add(cumulative_serialized_bytes, block.serialized_size,
                     cumulative_serialized_bytes) ||
        cumulative_serialized_bytes > options.max_cumulative_serialized_bytes) {
      return make_error(
          ErrorCode::resource_limit,
          "Writer images exceed their cumulative serialized-byte budget");
    }
    if (image.checksum != ChecksumAlgorithm::none) {
      const auto serialized = block.storage.empty()
                                  ? image.pixels
                                  : std::span<const std::byte>(block.storage);
      auto checksum = compute_checksum(serialized, image.checksum);
      if (!checksum) {
        return checksum.error();
      }
      block.checksum = std::move(checksum).value();
    }
  }

  std::vector<BlockLocation> image_blocks(images.size());
  const auto plan_blocks =
      [&](std::uint64_t first_offset) -> Result<std::uint64_t> {
    auto offset = first_offset;
    for (std::size_t index = 0; index < images.size(); ++index) {
      auto &block = image_blocks[index];
      block.kind = BlockKind::attachment;
      block.offset = offset;
      block.size = prepared[index].serialized_size;
      block.raw = "attachment:" + std::to_string(offset) + ':' +
                  std::to_string(prepared[index].serialized_size);
      std::uint64_t end = 0;
      if (!checked_add(offset, prepared[index].serialized_size, end)) {
        return make_error(ErrorCode::overflow, "Writer file layout overflows");
      }
      if (index + 1 == images.size()) {
        return end;
      }
      auto aligned = align_up(end, options.attachment_alignment);
      if (!aligned) {
        return aligned.error();
      }
      offset = aligned.value();
    }
    return make_error(ErrorCode::internal_error,
                      "Writer block planner received no images");
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
        make_header(images, metadata, options, prepared, image_blocks);
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

  const auto path_is_available = [](const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return (!error && status.type() == std::filesystem::file_type::not_found) ||
           error == std::errc::no_such_file_or_directory;
  };
  if (!path_is_available(destination)) {
    return make_error(ErrorCode::io_error,
                      "Writer destination already exists or cannot be checked");
  }
  auto temporary = destination;
  temporary += ".mmxisf-tmp";
  if (!path_is_available(temporary)) {
    return make_error(
        ErrorCode::io_error,
        "Writer temporary path already exists or cannot be checked");
  }
  TemporaryFileCleanup cleanup(temporary);
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) {
    return make_error(ErrorCode::io_error,
                      "Unable to create temporary XISF file");
  }

  std::array<std::byte, 16> preamble{
      std::byte{'X'}, std::byte{'I'}, std::byte{'S'}, std::byte{'F'},
      std::byte{'0'}, std::byte{'1'}, std::byte{'0'}, std::byte{'0'}};
  const auto header_length = static_cast<std::uint32_t>(header.size());
  preamble[8] = static_cast<std::byte>(header_length & 0xffU);
  preamble[9] = static_cast<std::byte>((header_length >> 8U) & 0xffU);
  preamble[10] = static_cast<std::byte>((header_length >> 16U) & 0xffU);
  preamble[11] = static_cast<std::byte>((header_length >> 24U) & 0xffU);
  auto preamble_written = write_all(output, preamble, stop_token);
  if (!preamble_written) {
    return preamble_written.error();
  }
  const auto header_bytes = std::as_bytes(std::span(header));
  auto header_written = write_all(output, header_bytes, stop_token);
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
          write_all(output, std::span(zeros).first(count), stop_token);
      if (!padding_written) {
        return padding_written.error();
      }
      padding -= count;
    }
    const auto serialized =
        prepared[index].storage.empty()
            ? images[index].pixels
            : std::span<const std::byte>(prepared[index].storage);
    auto pixels_written = write_all(output, serialized, stop_token);
    if (!pixels_written) {
      return pixels_written.error();
    }
    output_position = image_blocks[index].offset + image_blocks[index].size;
  }
  output.flush();
  if (!output) {
    return make_error(ErrorCode::io_error, "Unable to flush XISF file");
  }
  output.close();
  if (!output) {
    return make_error(ErrorCode::io_error, "Unable to close XISF file");
  }
  if (stop_token.stop_requested()) {
    return make_error(ErrorCode::cancelled, "XISF write was cancelled");
  }
  std::error_code filesystem_error;
  std::filesystem::create_hard_link(temporary, destination, filesystem_error);
  if (filesystem_error) {
    return make_error(ErrorCode::io_error,
                      "Unable to commit temporary XISF file");
  }
  WriteSummary summary;
  summary.file_size = file_size;
  summary.header_length = header_length;
  summary.image_block = image_blocks.front();
  summary.image_blocks = std::move(image_blocks);
  return summary;
}

} // namespace

Result<WriteSummary>
Writer::write_file(const std::filesystem::path &destination,
                   std::span<const ImageWriteView> images,
                   std::span<const MetadataWriteEntry> metadata,
                   const WriterOptions &options, std::stop_token stop_token) {
  try {
    return write_file_impl(destination, images, metadata, options, stop_token);
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
