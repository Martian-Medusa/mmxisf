// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <utility>
#include <variant>

#include "mmxisf/error.hpp"

namespace mmxisf {

template <typename T> class Result {
public:
  Result(T value) : storage_(std::move(value)) {}
  Result(Error error) : storage_(std::move(error)) {}

  [[nodiscard]] bool has_value() const noexcept {
    return std::holds_alternative<T>(storage_);
  }
  explicit operator bool() const noexcept { return has_value(); }

  T &value() & { return std::get<T>(storage_); }
  const T &value() const & { return std::get<T>(storage_); }
  T &&value() && { return std::get<T>(std::move(storage_)); }

  Error &error() & { return std::get<Error>(storage_); }
  const Error &error() const & { return std::get<Error>(storage_); }

private:
  std::variant<T, Error> storage_;
};

} // namespace mmxisf
