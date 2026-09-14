// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/byte_sink.hpp"
#include "mmxisf/byte_source.hpp"
#include "mmxisf/document.hpp"
#include "mmxisf/error.hpp"
#include "mmxisf/export.hpp"
#include "mmxisf/reader.hpp"
#include "mmxisf/result.hpp"
#include "mmxisf/version.hpp"
#include "mmxisf/writer.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace {

void expect(bool condition) {
  if (!condition) {
    std::abort();
  }
}

using mmxisf::Reader;
using mmxisf::Result;

static_assert(std::is_abstract_v<mmxisf::ByteSource>);
static_assert(std::has_virtual_destructor_v<mmxisf::ByteSource>);
static_assert(std::is_abstract_v<mmxisf::ByteSink>);
static_assert(std::has_virtual_destructor_v<mmxisf::ByteSink>);
static_assert(std::is_abstract_v<mmxisf::ImageRowSink>);
static_assert(std::has_virtual_destructor_v<mmxisf::ImageRowSink>);

static_assert(std::is_move_constructible_v<Reader>);
static_assert(std::is_nothrow_move_constructible_v<Reader>);
static_assert(std::is_move_assignable_v<Reader>);
static_assert(std::is_nothrow_move_assignable_v<Reader>);
static_assert(!std::is_copy_constructible_v<Reader>);
static_assert(!std::is_copy_assignable_v<Reader>);
static_assert(noexcept(std::declval<const Reader &>().document()));

static_assert(
    std::is_same_v<decltype(Reader::open_file(
                       std::declval<const std::filesystem::path &>(), {})),
                   Result<Reader>>);
static_assert(
    std::is_same_v<
        decltype(Reader::open_source(
            std::declval<std::shared_ptr<const mmxisf::ByteSource>>(), {})),
        Result<Reader>>);
static_assert(std::is_same_v<decltype(std::declval<const Reader &>().read_image(
                                 std::size_t{}, std::stop_token{})),
                             Result<mmxisf::RawImage>>);
static_assert(
    std::is_same_v<decltype(std::declval<const Reader &>().read_image_into(
                       std::size_t{}, std::declval<std::span<std::byte>>(),
                       std::stop_token{})),
                   Result<std::size_t>>);
static_assert(std::is_same_v<decltype(mmxisf::Writer::write_file(
                                 std::declval<const std::filesystem::path &>(),
                                 std::declval<const mmxisf::ImageWriteView &>(),
                                 std::declval<const mmxisf::WriterOptions &>(),
                                 std::stop_token{})),
                             Result<mmxisf::WriteSummary>>);
static_assert(noexcept(mmxisf::version()));

void verify_result_contract() {
  Result<int> value{42};
  expect(value.has_value());
  expect(value.value() == 42);
  bool wrong_error_threw = false;
  try {
    static_cast<void>(value.error());
  } catch (const std::bad_variant_access &) {
    wrong_error_threw = true;
  }
  expect(wrong_error_threw);

  mmxisf::Error detail;
  detail.code = mmxisf::ErrorCode::invalid_argument;
  Result<int> error{detail};
  expect(!error);
  expect(error.error().code == mmxisf::ErrorCode::invalid_argument);
  bool wrong_value_threw = false;
  try {
    static_cast<void>(error.value());
  } catch (const std::bad_variant_access &) {
    wrong_value_threw = true;
  }
  expect(wrong_value_threw);

  Result<void> success;
  expect(success.has_value());
  success.value();
  Result<void> void_error{detail};
  expect(!void_error);
  expect(void_error.error().code == mmxisf::ErrorCode::invalid_argument);
}

void verify_default_resource_contract() {
  const mmxisf::ReaderOptions reader;
  expect(reader.max_header_bytes > 0);
  expect(reader.max_xml_depth > 0);
  expect(reader.max_xml_nodes > 0);
  expect(reader.max_images > 0);
  expect(reader.max_encoded_block_bytes > 0);
  expect(reader.max_serialized_image_bytes > 0);
  expect(reader.max_decoded_image_bytes > 0);
  expect(reader.max_decompression_ratio > 0);
  expect(reader.max_zstd_window_bytes > 0);

  const mmxisf::WriterOptions writer;
  expect(writer.attachment_alignment > 0);
  expect(writer.max_header_bytes > 0);
  expect(writer.max_images > 0);
  expect(writer.compression_subblock_bytes > 0);
  expect(writer.max_cumulative_image_bytes >= writer.max_image_bytes);
  expect(writer.max_cumulative_property_bytes >= writer.max_property_bytes);
  expect(writer.max_cumulative_serialized_property_bytes >=
         writer.max_serialized_property_bytes);
  expect(writer.max_cumulative_serialized_bytes >=
         writer.max_serialized_image_bytes);

  const mmxisf::ImageRowReadOptions rows;
  expect(rows.max_row_bytes > 0);
  expect(rows.max_subblock_bytes > 0);
}

void verify_error_code_strings() {
  constexpr std::array codes{
      mmxisf::ErrorCode::io_error,
      mmxisf::ErrorCode::invalid_signature,
      mmxisf::ErrorCode::invalid_preamble,
      mmxisf::ErrorCode::header_too_large,
      mmxisf::ErrorCode::malformed_xml,
      mmxisf::ErrorCode::resource_limit,
      mmxisf::ErrorCode::invalid_xisf,
      mmxisf::ErrorCode::unsupported_feature,
      mmxisf::ErrorCode::invalid_block,
      mmxisf::ErrorCode::checksum_mismatch,
      mmxisf::ErrorCode::invalid_argument,
      mmxisf::ErrorCode::overflow,
      mmxisf::ErrorCode::cancelled,
      mmxisf::ErrorCode::internal_error,
  };
  for (const auto code : codes) {
    const char *const text = mmxisf::to_string(code);
    expect(text != nullptr);
    expect(text[0] != '\0');
  }
}

} // namespace

int main() {
  verify_result_contract();
  verify_default_resource_contract();
  verify_error_code_strings();
  return 0;
}
