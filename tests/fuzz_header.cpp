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
  options.max_extension_elements = 512;
  options.max_extension_attributes = 4'096;
  options.max_extension_bytes = 256U * 1024U;
  options.max_ancillary_objects = 512;
  options.max_ancillary_attributes = 4'096;
  options.max_ancillary_bindings = 2'000;
  options.max_ancillary_bytes = 256U * 1024U;
  options.max_icc_profiles = 512;
  options.max_icc_profile_bindings = 2'000;
  options.max_serialized_icc_profile_bytes = 1024U * 1024U;
  options.max_decoded_icc_profile_bytes = 1024U * 1024U;
  options.max_thumbnails = 128;
  options.max_thumbnail_bindings = 2'000;
  options.max_thumbnail_dimension = 1'024;
  options.max_serialized_thumbnail_bytes = 1024U * 1024U;
  options.max_decoded_thumbnail_bytes = 1024U * 1024U;
  options.max_table_structures = 128;
  options.max_tables = 128;
  options.max_table_fields = 2'000;
  options.max_table_rows = 2'000;
  options.max_table_cells = 10'000;
  options.max_table_bindings = 2'000;
  options.max_table_text_bytes = 256U * 1024U;
  options.max_decoded_image_bytes = 1024U * 1024U;
  options.max_serialized_property_bytes = 1024U * 1024U;
  options.max_decoded_property_bytes = 1024U * 1024U;
  options.max_samples_per_image = 256U * 1024U;

  auto source = std::make_shared<FuzzByteSource>(data, size);
  auto reader = mmxisf::Reader::open_source(std::move(source), options);
  if (reader) {
    if (!reader.value().document().images().empty()) {
      (void)reader.value().read_image(0);
    }
    std::size_t decoded_properties = 0;
    const auto &metadata = reader.value().document().metadata();
    for (std::size_t index = 0; index < metadata.size(); ++index) {
      if (metadata[index].kind == mmxisf::MetadataEntry::Kind::property &&
          metadata[index].value_form ==
              mmxisf::MetadataEntry::ValueForm::data_block) {
        (void)reader.value().read_property_block(index);
        if (++decoded_properties == 16) {
          break;
        }
      }
    }
    const auto profiles = std::min<std::size_t>(
        reader.value().document().icc_profiles().size(), 16);
    for (std::size_t index = 0; index < profiles; ++index) {
      (void)reader.value().read_icc_profile(index);
    }
    const auto thumbnails = std::min<std::size_t>(
        reader.value().document().thumbnails().size(), 16);
    for (std::size_t index = 0; index < thumbnails; ++index) {
      (void)reader.value().read_thumbnail(index);
    }
  }
  return 0;
}

#ifdef MMXISF_FUZZ_SMOKE_MAIN
#include <iostream>

namespace {

std::vector<std::uint8_t> seed_unit() {
  const std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<xisf xmlns=\"http://www.pixinsight.com/xisf\" "
      "xmlns:ext=\"urn:mmxisf:fuzz\" version=\"1.0\">"
      "<ext:Probe ext:mode=\"fuzz\">before<ext:Nested/>after</ext:Probe>"
      "<Resolution horizontal=\"72\" vertical=\"72\" unit=\"inch\"/>"
      "<ICCProfile location=\"inline:base64\">"
      "AAAAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAYWNzcAAAAAAAAAAB"
      "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
      "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=</ICCProfile>"
      "<Thumbnail geometry=\"1:1:1\" sampleFormat=\"UInt8\" "
      "location=\"embedded\"><Data encoding=\"hex\">00</Data></Thumbnail>"
      "<Structure uid=\"FuzzStructure\"><Field id=\"value\" "
      "type=\"UInt8\"/></Structure><Table id=\"FuzzTable\" rows=\"1\" "
      "columns=\"1\"><Reference ref=\"FuzzStructure\"/>"
      "<Row><Cell value=\"1\"/></Row></Table>"
      "<Property id=\"test\" type=\"String\">value</Property>"
      "<Property id=\"Test:Boolean\" type=\"Boolean\" value=\"true\"/>"
      "<Property id=\"Test:Integer\" type=\"Int32\" value=\"-42\"/>"
      "<Property id=\"Test:Float\" type=\"Float64\" value=\"1.25e-3\"/>"
      "<Property id=\"Test:Complex\" type=\"Complex64\" "
      "value=\"(1.5,-2)\"/>"
      "<Property id=\"Test:Time\" type=\"TimePoint\" "
      "value=\"2026-09-13T00:00:00Z\"/>"
      "<Property id=\"Test:Vector\" type=\"UI8Vector\" length=\"4\" "
      "location=\"inline:base64\">AQIDBA==</Property>"
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
