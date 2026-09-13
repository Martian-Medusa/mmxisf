// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "mmxisf/result.hpp"

namespace mmxisf {

class ByteSource {
public:
  virtual ~ByteSource() = default;

  [[nodiscard]] virtual Result<std::uint64_t> size() const = 0;
  [[nodiscard]] virtual Result<std::size_t>
  read_at(std::uint64_t offset, std::span<std::byte> destination) const = 0;
};

} // namespace mmxisf
