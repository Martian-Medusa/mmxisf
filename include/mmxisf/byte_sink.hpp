// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <span>

#include "mmxisf/result.hpp"

namespace mmxisf {

// Sequential caller-owned output. Implementations may accept fewer bytes than
// requested, but must make progress or return an error.
class ByteSink {
public:
  virtual ~ByteSink() = default;

  [[nodiscard]] virtual Result<std::size_t>
  write(std::span<const std::byte> source) = 0;
  [[nodiscard]] virtual Result<void> flush() = 0;
};

} // namespace mmxisf
