// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/error.hpp"
#include "mmxisf/reader.hpp"
#include "mmxisf/version.hpp"
#include "mmxisf/writer.hpp"

#include <string_view>

int main() {
  const mmxisf::ReaderOptions reader_options;
  const mmxisf::WriterOptions writer_options;
  return std::string_view(mmxisf::version()) == "0.1.0" &&
                 std::string_view(mmxisf::to_string(
                     mmxisf::ErrorCode::invalid_signature)) ==
                     "invalid_signature" &&
                 reader_options.max_header_bytes > 0 &&
                 writer_options.max_images > 0
             ? 0
             : 1;
}
