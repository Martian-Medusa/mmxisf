// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mmxisf {

enum class ErrorCode {
  io_error,
  invalid_signature,
  invalid_preamble,
  header_too_large,
  malformed_xml,
  resource_limit,
  invalid_xisf,
  unsupported_feature,
  invalid_block,
  checksum_mismatch,
  invalid_argument,
  overflow,
  cancelled,
  internal_error,
};

struct Error {
  ErrorCode code{ErrorCode::internal_error};
  std::string message;
  std::optional<std::uint64_t> byte_offset;
  std::optional<std::size_t> image_index;
  std::string element;
  std::string attribute;
};

const char *to_string(ErrorCode code) noexcept;

} // namespace mmxisf
