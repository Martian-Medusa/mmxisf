// SPDX-License-Identifier: Apache-2.0

#include "mmxisf/reader.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

class FuzzByteSource final : public mmxisf::ByteSource {
public:
  FuzzByteSource(const std::uint8_t *data, std::size_t size) : bytes_(size) {
    if (size != 0) {
      std::transform(data, data + size, bytes_.begin(),
                     [](std::uint8_t value) { return std::byte{value}; });
    }
  }

  mmxisf::Result<std::uint64_t> size() const override {
    return static_cast<std::uint64_t>(bytes_.size());
  }

  mmxisf::Result<std::size_t>
  read_at(std::uint64_t offset,
          std::span<std::byte> destination) const override {
    if (offset > bytes_.size()) {
      mmxisf::Error error;
      error.code = mmxisf::ErrorCode::io_error;
      error.message = "fuzz source range error";
      return error;
    }
    const auto count = std::min(
        destination.size(), bytes_.size() - static_cast<std::size_t>(offset));
    std::copy_n(bytes_.data() + static_cast<std::size_t>(offset), count,
                destination.data());
    return count;
  }

private:
  std::vector<std::byte> bytes_;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  mmxisf::ReaderOptions options;
  options.max_header_bytes = 1024U * 1024U;
  options.max_xml_nodes = 10'000;
  options.max_metadata_entries = 2'000;
  options.max_metadata_value_bytes = 256U * 1024U;
  options.max_decoded_image_bytes = 1024U * 1024U;
  options.max_samples_per_image = 256U * 1024U;

  auto source = std::make_shared<FuzzByteSource>(data, size);
  auto reader = mmxisf::Reader::open_source(std::move(source), options);
  if (reader && !reader.value().document().images().empty()) {
    (void)reader.value().read_image(0);
  }
  return 0;
}

#ifdef MMXISF_FUZZ_SMOKE_MAIN
#include <iostream>

namespace {

std::vector<std::uint8_t> seed_unit() {
  const std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" version=\"1.0\">"
      "<Property id=\"test\" type=\"String\">value</Property>"
      "<Metadata><Property id=\"XISF:CreationTime\" type=\"TimePoint\" "
      "value=\"2026-09-13T00:00:00Z\"/>"
      "<Property id=\"XISF:CreatorApplication\" "
      "type=\"String\">fuzz smoke</Property></Metadata></xisf>";
  std::vector<std::uint8_t> bytes(16 + xml.size(), 0);
  const std::string signature = "XISF0100";
  std::copy(signature.begin(), signature.end(), bytes.begin());
  const auto length = static_cast<std::uint32_t>(xml.size());
  bytes[8] = static_cast<std::uint8_t>(length & 0xffU);
  bytes[9] = static_cast<std::uint8_t>((length >> 8U) & 0xffU);
  bytes[10] = static_cast<std::uint8_t>((length >> 16U) & 0xffU);
  bytes[11] = static_cast<std::uint8_t>((length >> 24U) & 0xffU);
  std::copy(xml.begin(), xml.end(), bytes.begin() + 16);
  return bytes;
}

std::uint64_t next_random(std::uint64_t &state) {
  state ^= state << 13U;
  state ^= state >> 7U;
  state ^= state << 17U;
  return state;
}

} // namespace

int main() {
  const auto seed = seed_unit();
  std::uint64_t random_state = 0x6d6d786973663031ULL;
  constexpr std::size_t kCases = 20'000;
  for (std::size_t iteration = 0; iteration < kCases; ++iteration) {
    auto candidate = seed;
    const auto mutations = 1U + (next_random(random_state) % 12U);
    for (std::size_t mutation = 0; mutation < mutations; ++mutation) {
      const auto index = next_random(random_state) % candidate.size();
      candidate[index] ^=
          static_cast<std::uint8_t>(1U << (next_random(random_state) % 8U));
    }
    if (iteration % 5 == 0) {
      candidate.resize(next_random(random_state) % (candidate.size() + 1U));
    }
    LLVMFuzzerTestOneInput(candidate.data(), candidate.size());
  }
  std::cout << "sanitizer fuzz smoke: " << kCases << " cases\n";
  return 0;
}
#endif
