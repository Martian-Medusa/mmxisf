// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/byte_source.hpp"
#include "mmxisf/reader.hpp"
#include "mmxisf/version.hpp"

#include <string_view>

int main() {
  mmxisf::ReaderOptions options;
  return options.max_header_bytes > 0 &&
                 std::string_view(mmxisf::version()) == "0.1.0"
             ? 0
             : 1;
}
