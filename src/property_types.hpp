// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mmxisf::detail {

enum class PropertyCategory {
  scalar_or_complex,
  string,
  time_point,
  vector,
  matrix,
  unknown
};

inline PropertyCategory classify_property_type(std::string_view type) {
  constexpr std::array<std::string_view, 26> scalar_and_complex{
      "Boolean", "Int8",      "UInt8",    "Byte",    "Int16",     "Short",
      "UInt16",  "UShort",    "Int32",    "Int",     "UInt32",    "UInt",
      "Int64",   "Int128",    "UInt64",   "UInt128", "Float32",   "Float",
      "Float64", "Double",    "Float128", "Quad",    "Complex32", "Complex64",
      "Complex", "Complex128"};
  constexpr std::array<std::string_view, 20> vectors{
      "I8Vector",   "UI8Vector",  "ByteArray",   "I16Vector", "UI16Vector",
      "I32Vector",  "IVector",    "UI32Vector",  "UIVector",  "I64Vector",
      "UI64Vector", "I128Vector", "UI128Vector", "F32Vector", "F64Vector",
      "Vector",     "F128Vector", "C32Vector",   "C64Vector", "C128Vector"};
  constexpr std::array<std::string_view, 20> matrices{
      "I8Matrix",   "UI8Matrix",  "ByteMatrix",  "I16Matrix", "UI16Matrix",
      "I32Matrix",  "IMatrix",    "UI32Matrix",  "UIMatrix",  "I64Matrix",
      "UI64Matrix", "I128Matrix", "UI128Matrix", "F32Matrix", "F64Matrix",
      "Matrix",     "F128Matrix", "C32Matrix",   "C64Matrix", "C128Matrix"};
  const auto contains = [type](const auto &types) {
    return std::find(types.begin(), types.end(), type) != types.end();
  };
  if (contains(scalar_and_complex)) {
    return PropertyCategory::scalar_or_complex;
  }
  if (type == "String") {
    return PropertyCategory::string;
  }
  if (type == "TimePoint") {
    return PropertyCategory::time_point;
  }
  if (contains(vectors)) {
    return PropertyCategory::vector;
  }
  if (contains(matrices)) {
    return PropertyCategory::matrix;
  }
  return PropertyCategory::unknown;
}

struct PropertyElementLayout {
  std::uint64_t element_size{0};
  std::uint64_t scalar_component_size{0};
  bool string_data{false};
};

inline std::optional<PropertyElementLayout>
property_element_layout(std::string_view type) {
  if (type == "String") {
    return PropertyElementLayout{1, 1, true};
  }
  constexpr std::array<std::string_view, 6> one_byte{"I8Vector",  "UI8Vector",
                                                     "ByteArray", "I8Matrix",
                                                     "UI8Matrix", "ByteMatrix"};
  constexpr std::array<std::string_view, 4> two_byte{"I16Vector", "UI16Vector",
                                                     "I16Matrix", "UI16Matrix"};
  constexpr std::array<std::string_view, 10> four_byte{
      "I32Vector", "IVector", "UI32Vector", "UIVector", "F32Vector",
      "I32Matrix", "IMatrix", "UI32Matrix", "UIMatrix", "F32Matrix"};
  constexpr std::array<std::string_view, 8> eight_byte{
      "I64Vector", "UI64Vector", "F64Vector", "Vector",
      "I64Matrix", "UI64Matrix", "F64Matrix", "Matrix"};
  constexpr std::array<std::string_view, 6> sixteen_byte{
      "I128Vector", "UI128Vector", "F128Vector",
      "I128Matrix", "UI128Matrix", "F128Matrix"};
  const auto contains = [type](const auto &types) {
    return std::find(types.begin(), types.end(), type) != types.end();
  };
  if (contains(one_byte)) {
    return PropertyElementLayout{1, 1, false};
  }
  if (contains(two_byte)) {
    return PropertyElementLayout{2, 2, false};
  }
  if (contains(four_byte)) {
    return PropertyElementLayout{4, 4, false};
  }
  if (contains(eight_byte)) {
    return PropertyElementLayout{8, 8, false};
  }
  if (contains(sixteen_byte)) {
    return PropertyElementLayout{16, 16, false};
  }
  if (type == "C32Vector" || type == "C32Matrix") {
    return PropertyElementLayout{8, 4, false};
  }
  if (type == "C64Vector" || type == "C64Matrix") {
    return PropertyElementLayout{16, 8, false};
  }
  if (type == "C128Vector" || type == "C128Matrix") {
    return PropertyElementLayout{32, 16, false};
  }
  return std::nullopt;
}

} // namespace mmxisf::detail
