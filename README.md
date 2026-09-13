# Martian Medusa XISF (`mmxisf`)

`mmxisf` is the planned standalone, clean-room C++ library for reading and
writing Extensible Image Serialization Format (XISF) files. Its first product
consumer will be PSF Field Inspector (PFI), but the library will not depend on
PFI, PixInsight, PCL, or Qt.

> Status: M1 is complete; M2 implementation has an independent-producer scalar
> and color matrix, with storage/endian combinations still partial; M3 codec,
> resource-limit, and local performance prerequisites are complete; M4 is in
> progress and the first
> M5 integration prerequisites are implemented. Version
> `0.1.0` parses bounded
> monolithic XISF 1.0 headers and inspects image descriptors, properties, and
> FITS keywords. The reader handles the PFI scalar profile from uncompressed,
> zlib, LZ4, LZ4HC-compatible, and current PixInsight Zstandard attachment and
> embedded blocks, including byte shuffle, compression subblocks, and
> SHA-1/256/512 verification. It powers
> Gray/RGB preview in the macOS viewer. Block-backed String, vector, and matrix
> Properties can be read from attachment or inline blocks with the same codec,
> integrity, resource-limit, endian, and cancellation guarantees.
> This is still a pre-release profile, not a general XISF decoder.

## Why the public name is not `libXISF`

The requested local directory remains `software/libXISF`. An unrelated GPLv3+
C++ project is already distributed under the `libXISF` name and as a Debian
package. To avoid package, linker, search, and contributor confusion, this plan
uses the public project/package name `mmxisf`, CMake target
`mmxisf::mmxisf`, and C++ namespace `mmxisf`. The final public repository name
is a release gate, not an assumption.

## Intended scope

- Portable C++20 with CMake package installation.
- Safe parsing of untrusted XISF 1.0 inputs with explicit resource limits.
- Metadata inspection without decoding pixel blocks.
- Typed image decoding and streaming/caller-owned-buffer paths.
- Monolithic read support first; deterministic monolithic writing second.
- Compression, byte shuffling, checksums, metadata objects, and multiple images
  according to a versioned conformance matrix.
- Linux, macOS, and Windows CI before the first public beta.
- A narrow PFI adapter maintained in the PFI repository.

Distributed XISF units, network retrieval, XML digital-signature verification,
and obscure extension elements are deliberately outside the first PFI-ready
milestone. They remain candidates for later conformance work.

## Start here

- [Implementation plan](docs/PLAN.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Roadmap and estimates](docs/ROADMAP.md)
- [PFI integration](docs/PFI_INTEGRATION.md)
- [Conformance matrix](docs/conformance/xisf-1.0-matrix.json)
- [Threat model](docs/THREAT_MODEL.md)
- [Resource limits](docs/RESOURCE_LIMITS.md)
- [Fixture policy](docs/FIXTURE_POLICY.md)
- [Accelerated M0 status](docs/M0_STATUS.md)
- [M1 parser/viewer checkpoint](docs/M1_STATUS.md)
- [M2 progress](docs/M2_STATUS.md)
- [M3 progress](docs/M3_STATUS.md)
- [M4 progress](docs/M4_STATUS.md)
- [Sources and clean-room policy](docs/SOURCES.md)

## Build the library and inspector

```sh
cmake -S . -B build -DMMXISF_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
build/mmxisf-inspect path/to/image.xisf
build/mmxisf-inspect --decode path/to/image.xisf
build/mmxisf-inspect --decode-sha256 path/to/image.xisf
build/mmxisf-inspect --decode-properties-sha256 path/to/image.xisf
```

Normal configuration requires installed Expat, zlib, LZ4, Zstandard, and
OpenSSL Crypto development packages. No dependency is downloaded implicitly.

The public API requires C++20 library support for `std::span` and
`std::stop_token`. The macOS CI baseline therefore uses macOS 15 with Xcode
26.3 selected explicitly. The standard libraries supplied with Xcode 15.4 and
the runner's default Xcode 16.4 are insufficient for this API.

The CI package gate also configures and runs the independent
`tests/package_consumer` project against the installed CMake package rather
than the source tree.

The pre-release reader can also consume a caller-provided seekable
`mmxisf::ByteSource`. Decoded attachment bytes can be returned in an
owning `RawImage` or written into a caller-owned span with cooperative
`std::stop_token` cancellation. Embedded image blocks support whitespace-tolerant
Base64 and the specification's lowercase hexadecimal encoding. The current M3
slice supports zlib, LZ4, LZ4HC-compatible, and Zstandard blocks, their `+sh`
variants, validated compression subblocks, and SHA-1/256/512 checksums. The
Zstandard rows are a current PixInsight interoperability extension to the
pinned 2017 XISF 1.0 baseline. Failed checksums stop processing before any
compressed bytes reach a codec.

`Reader::read_property_block(metadata_index, options, stop_token)` returns the
exact bytes of a block-backed String, vector, or matrix Property. It supports
attachment and `inline:base64`/`inline:hex` locations, all declared standard
element widths through 128-bit and complex arrays, optional native-endian
output, the reader codecs and checksum algorithms above, and independent
serialized/decoded Property byte limits. Matrix bytes retain XISF row-major
order. The library does not reinterpret these bytes as astronomy semantics;
that remains a consumer responsibility.

By default pixel reads preserve the serialized byte order and Planar/Normal
layout exactly. Callers can pass `ImageReadOptions` to request native byte order
and either layout explicitly. No sample type or precision conversion is
performed, and attachment layout conversion uses bounded scratch memory rather
than a second full-frame buffer.

Image descriptors and owning reads explicitly report the serialized top-left,
top-to-bottom/left-to-right coordinate convention, nominal channel order, and
optional XISF display orientation. Scientific reads never apply that display
transform. A successful owning read also distinguishes a verified declared
checksum from an image with no checksum declaration.

The inspector's optional `--decode` mode validates every declared image block
and reports the exact decoded byte count. It does not convert endian, storage
layout, color, or sample precision.
`--decode-sha256` additionally reports a deterministic SHA-256 of those exact
source-representation pixel bytes for differential producer/PFI comparisons.
`--decode-properties-sha256` performs the corresponding bounded decode and hash
for every block-backed Property without decoding image pixels.

For a local sanitizer mutation smoke:

```sh
cmake -S . -B build-fuzz-smoke \
  -DMMXISF_BUILD_TESTS=OFF \
  -DMMXISF_BUILD_TOOLS=OFF \
  -DMMXISF_BUILD_FUZZ_SMOKE=ON
cmake --build build-fuzz-smoke
build-fuzz-smoke/mmxisf_fuzz_smoke
```

On a Clang installation that includes libFuzzer, enable
`MMXISF_BUILD_FUZZER=ON` as well and run `mmxisf_fuzz_header` with
`tests/fuzz_header.dict`. CI decodes the committed metadata, zlib, and LZ4 seed
corpus from `tests/fuzz_seed*.xisf.b64` and `tests/interop/*.xisf.b64`, performs
20,000 coverage-guided runs, and retains a crashing input when the job fails.

## Build and run the macOS PoC viewer

```sh
cmake -S . -B build \
  -DMMXISF_BUILD_TESTS=ON \
  -DMMXISF_BUILD_VIEWER=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
open "artifacts/mmXISF Viewer PoC.app"
```

The optional AppKit target is macOS-only and does not enter the standalone
library. The generated local bundle embeds Expat, LZ4, OpenSSL Crypto, zlib,
and Zstandard when they resolve to non-system dynamic libraries. Its
current preview scope is the first supported uncompressed, zlib-, LZ4-, or
Zstandard-compressed local/embedded Gray/RGB block in Planar or Normal layout,
with UInt8, UInt16, UInt32, Float32, or Float64 samples. The metadata inspector
can still open a broader set of headers, while unsupported image decoding fails
closed.

## License

This local foundation is licensed under Apache License 2.0. It is permissive,
includes an explicit patent grant, and permits use by both open-source
applications and PFI. The owner should reconfirm this choice at G0 before the
first external publication. Dependency and fixture licensing still require a
recorded audit.
