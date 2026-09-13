// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "mmxisf/reader.hpp"

#include <cstdint>

namespace mmxisf::viewer {

struct StretchRange {
  double linear_low{0.0};
  double linear_high{1.0};
  double auto_low{0.0};
  double auto_high{1.0};
};

[[nodiscard]] double sample_value(const RawImage &image,
                                  std::uint64_t pixel_index,
                                  std::uint64_t channel);

[[nodiscard]] StretchRange calculate_stretch_range(const RawImage &image);

} // namespace mmxisf::viewer
