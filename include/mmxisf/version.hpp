// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string_view>

#include "mmxisf/export.hpp"

namespace mmxisf {

[[nodiscard]] MMXISF_API std::string_view version() noexcept;

} // namespace mmxisf
