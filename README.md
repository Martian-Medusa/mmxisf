# Martian Medusa XISF (`mmxisf`)

`mmxisf` is the planned standalone, clean-room C++ library for reading and
writing Extensible Image Serialization Format (XISF) files. Its first product
consumer will be PSF Field Inspector (PFI), but the library will not depend on
PFI, PixInsight, PCL, or Qt.

> Status: M1-M3 implementation is complete for the declared local monolithic
> profile; M4/M5 library and PFI-adapter paths await native PixInsight evidence.
> M6 includes a deterministic compressed multi-image Gray/RGB writer with
> declarative scalar and block-backed metadata. M7 now inventories ancillary
> objects, ICC profiles, thumbnails, extensions, and bounded heterogeneous
> tables. M8 hardening and distribution work is in progress. Version
> `0.1.0` parses bounded
> monolithic XISF 1.0 headers and inspects image descriptors, properties, and
> FITS keywords. The reader handles the PFI scalar profile from uncompressed,
> zlib, LZ4, LZ4HC-compatible, and current PixInsight Zstandard attachment and
> embedded blocks, including byte shuffle, compression subblocks, and
> SHA-1/256/512 and SHA3-256/512 verification. It powers
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

Contributions must follow [`CONTRIBUTING.md`](CONTRIBUTING.md); security reports
follow [`SECURITY.md`](SECURITY.md). The fail-closed candidate, publication,
and rollback checklist is in
[`docs/RELEASE_PROCESS.md`](docs/RELEASE_PROCESS.md).

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
- [Standalone support profile](docs/support-profile-0.1.0.json)
- [Conformance matrix](docs/conformance/xisf-1.0-matrix.json)
- [Threat model](docs/THREAT_MODEL.md)
- [Resource limits](docs/RESOURCE_LIMITS.md)
- [Fixture policy](docs/FIXTURE_POLICY.md)
- [Accelerated M0 status](docs/M0_STATUS.md)
- [M1 parser/viewer checkpoint](docs/M1_STATUS.md)
- [M2 progress](docs/M2_STATUS.md)
- [M3 progress](docs/M3_STATUS.md)
- [M4 progress](docs/M4_STATUS.md)
- [M6 writer progress](docs/M6_STATUS.md)
- [M7 broader image coverage](docs/M7_STATUS.md)
- [M7 row-delivery memory checkpoint](docs/M7_ROW_PERFORMANCE.md)
- [M8 hardening and distribution](docs/M8_STATUS.md)
- [Writer API](docs/WRITER.md)
- [Manual PixInsight validation](tests/pixinsight/README.md)
- [Bounded image-row reader](docs/ROW_READER.md)
- [Generated API reference overview](docs/API.md)
- [Fuzzing policy](docs/FUZZING.md)
- [Supply-chain and license audit](docs/SUPPLY_CHAIN_AUDIT.md)
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
build/mmxisf-inspect --decode-icc-sha256 path/to/image.xisf
build/mmxisf-inspect --decode-thumbnails-sha256 path/to/image.xisf
build/mmxisf-inspect --decode-rows path/to/image.xisf
```

Normal configuration requires installed Expat, zlib, LZ4, Zstandard, and
OpenSSL Crypto development packages. No dependency is downloaded implicitly.

For an isolated, versioned dependency graph, use the checked `vcpkg.json` with
a vcpkg checkout at its pinned built-in registry commit, then configure through
the vcpkg toolchain and enable the production floor:

```sh
cmake -S . -B build-vcpkg \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DMMXISF_ENFORCE_PRODUCTION_DEPENDENCY_BASELINE=ON \
  -DMMXISF_BUILD_TESTS=ON
cmake --build build-vcpkg
ctest --test-dir build-vcpkg --output-on-failure
```

Manifest mode keeps its installed dependency graph isolated from system and
MacPorts packages. The pinned baseline is reproducible input, not a permanent
security approval; refresh and audit it before freezing every candidate.

On macOS arm64, a bootstrapped vcpkg checkout can run the complete isolated
baseline, installed-consumer, static-linkage, viewer-signature, and architecture
gate with:

```sh
MMXISF_VCPKG_ROOT=/path/to/vcpkg \
  tools/run_vcpkg_macos_arm64_gate.sh
```

On a Linux amd64 Docker host, use a dedicated official vcpkg checkout at the
manifest's exact `builtin-baseline`, bootstrap it for Linux, and run the static
and shared production-baseline builds plus both installed-package consumers:

```sh
MMXISF_VCPKG_ROOT=/path/to/linux-vcpkg \
  tools/run_vcpkg_linux_docker_gate.sh
```

The wrapper builds the pinned Ubuntu image, refuses to reuse a gate directory,
and verifies that the vcpkg checkout commit equals the manifest baseline. The
checkout, downloads, and binary package cache stay outside the container for
deliberate reuse. By default the binary cache is
`.mmxisf-binary-cache/` inside the dedicated checkout; override it with
`MMXISF_VCPKG_BINARY_CACHE`. Use a checkout dedicated to Linux because the
bootstrapped vcpkg binary is platform-specific.

The default package is static. Set `-DBUILD_SHARED_LIBS=ON` for a shared
library. Public functions/classes use explicit import/export annotations, and
the installed CMake target propagates `MMXISF_STATIC_DEFINE` only for static
consumers. Before 1.0, package compatibility is limited to the same minor
version; from 1.0 onward it follows the same-major SemVer contract. With the
default `MMXISF_BUILD_VIEWER=OFF`, no viewer source or
preview-support target is compiled; viewer code and tests belong only to its
explicit macOS build. Both library forms install the Apache-2.0 license,
notices, security policy,
dependency notices, and the validated SPDX 2.3 source-dependency SBOM under
`share/mmxisf`. The same install tree carries the standalone support profile,
the exact conformance matrix it hashes, and the pinned specification
provenance. Their relocatable paths are exported as
`mmxisf_SUPPORT_PROFILE`, `mmxisf_CONFORMANCE_MATRIX`, and
`mmxisf_SPECIFICATION_BASELINE` by `find_package(mmxisf CONFIG)`. Each
configured build also generates and installs a binary
dependency SBOM containing the exact versions resolved from the dependency
headers, the target platform, compiler, build configuration, and static/shared
linkage. Missing or malformed version macros fail configuration rather than
producing guessed package data.

The installed config exports `mmxisf_LINKAGE` as `static` or `shared`. Static
consumers resolve the five private implementation libraries because their
linker must complete the archive's dependency closure. Shared consumers do not
need those development packages at CMake configure time: no dependency target
or header crosses the public API. Runtime libraries, when dynamically selected
by a platform build, remain an explicit deployment responsibility recorded in
the binary SBOM.

Consumers may also embed the source tree with CMake `add_subdirectory` or
`FetchContent` and link the same `mmxisf::mmxisf` target. Embedded builds create
only the library by default; developer tests, tools, fuzzers, documentation,
and the optional viewer remain off unless the consumer explicitly enables
their `MMXISF_BUILD_*` options. Embedded configuration also leaves the parent
project's global `BUILD_TESTING` option untouched. Maintained external-consumer
gates compile and
run both static and shared source-subdirectory variants independently from the
installed-package tests and reject accidental developer targets. Both consumer
modes also compile every public header in its own translation unit; the gate
fails if a header depends on another public header being included first or if
the checked public-header inventory becomes stale.

Installed-package gates additionally copy each static and shared installation
to a new prefix and build a fresh consumer there. The consumer verifies that
the imported library, SBOM, support profile, conformance matrix, and
specification baseline all resolve inside that copied prefix, preventing hidden
build-tree or original-install dependencies.

Each manually dispatched full CI revision uses
`cmake/PrepareSourceCandidate.cmake` to create the source archive twice, require
byte identity, and retain one archive, its SHA-256, and a machine-readable
identity manifest as a short-lived workflow artifact. The same script can
prepare a local rehearsal from a clean checkout; it refuses to overwrite an
output directory or package a ref other than the checked-out `HEAD`. Routine
development currently uses local static, shared, sanitizer, and package-
consumer gates to conserve hosted runner minutes. The viewer has a separate
opt-in gate and never contributes to the standalone-library result. This is
release rehearsal only; the manifest records `publicationAuthorized: false`,
and no tag or public release is created.

Generate the versioned HTML API reference locally with:

```sh
cmake -S . -B build-docs \
  -DMMXISF_BUILD_DOCS=ON \
  -DMMXISF_BUILD_TESTS=OFF \
  -DMMXISF_BUILD_TOOLS=OFF
cmake --build build-docs --target mmxisf_docs
```

The entry point is `build-docs/api/html/index.html`. Documentation warnings
fail the build, and CI retains the generated reference as a short-lived
artifact without publishing a website or release.

The public API requires C++20 library support for `std::span` and
`std::stop_token`. The macOS CI baseline therefore uses macOS 15 with Xcode
26.3 selected explicitly. The standard libraries supplied with Xcode 15.4 and
the runner's default Xcode 16.4 are insufficient for this API.

The CI package gate also configures and runs the independent
`tests/package_consumer` project against the installed CMake package rather
than the source tree.

During the hosted-runner budget pause, run the complete local gate from the
repository root:

```sh
tools/run_local_quality_gates.sh
```

It performs warning-as-error static and shared builds, both installed-package
consumer tests, the ASan/UBSan suite and deterministic 20,000-case mutation
smoke, the ThreadSanitizer suite including concurrent same-reader and
same-destination-writer contracts, and generated API documentation. Outputs
stay under the ignored `build-local-gates` directory. On macOS,
`MMXISF_LOCAL_VIEWER=ON tools/run_local_quality_gates.sh` additionally runs the
isolated viewer PoC build and strict code-signature check after the complete
viewer-free library gate. `MMXISF_LOCAL_GATE_ROOT`, `MMXISF_LOCAL_JOBS`,
`MMXISF_LOCAL_CXX_FLAGS`, `MMXISF_LOCAL_TSAN=OFF`, and
`MMXISF_LOCAL_VIEWER=ON` control those explicit choices. The command records
only evidence for the host where it runs; it does not replace the manually
dispatched Linux/macOS/Windows matrix. The manual CI workflow follows the same
boundary: its library matrix is always viewer-free, while the viewer job runs
only when the `include_viewer` input is selected.

The pre-release `Writer::write_file` API emits one or more attached little-
endian Planar Gray or RGB images using UInt8, UInt16, UInt32, Float32, or
Float64 samples. Floating-point images require explicit finite bounds; all
images borrow immutable pixel spans for the duration of the call. Equivalent
inputs produce byte-identical files. Existing destinations are not overwritten,
per-image and cumulative resource limits and arithmetic are checked before file
creation, and cancellation removes incomplete temporary output. A declarative
metadata overload writes image-scoped direct scalar/String/TimePoint Properties
and FITS keywords plus the corresponding XISF-unit Properties without accepting
raw XML. The same ordered metadata records can attach exact typed vector and
matrix bytes with checked extents, explicit byte order, and independent limits.
Optional zlib, LZ4, LZ4HC, and Zstandard compression can be combined with byte
shuffle and SHA-1/256/512 plus SHA3-256/512 checksums. Image and Property
compression share the same bounded pipeline. Large compressed blocks use
bounded, item-aligned
subblocks so codec/shuffle scratch does not scale to the complete block.
References remain a later profile.

`Writer::write_to` exposes the same deterministic serializer through a
caller-owned sequential `ByteSink`. Generic sinks have explicit partial-output
and flush semantics; callers requiring an atomic no-overwrite file continue to
use `write_file`.

The optional build-tree `mmxisf-writer-benchmark` exercises a deterministic
73.5 MB RGB compression/checksum profile without committing a large fixture;
its current local measurement method and limits are documented in
[`docs/M6_WRITER_PERFORMANCE.md`](docs/M6_WRITER_PERFORMANCE.md).

The pre-release reader can also consume a caller-provided seekable
`mmxisf::ByteSource`. Decoded attachment bytes can be returned in an
owning `RawImage` or written into a caller-owned span with cooperative
`std::stop_token` cancellation. Embedded image blocks support whitespace-tolerant
Base64 and the specification's lowercase hexadecimal encoding. The current M3
slice supports zlib, LZ4, LZ4HC-compatible, and Zstandard blocks, their `+sh`
variants, validated compression subblocks, and SHA-1/256/512 plus
SHA3-256/512 checksums. The Zstandard rows are a current PixInsight
interoperability extension to the
pinned 2017 XISF 1.0 baseline. Failed checksums stop processing before any
compressed bytes reach a codec.

`Reader::read_image_rows` provides a low-copy alternative for analysis:
caller-owned callbacks receive ephemeral planar channel rows with exact channel
and row indices. Uncompressed input uses row-sized staging, compressed input is
decoded one bounded declared subblock at a time, and a declared checksum is
verified in a bounded first pass before any row is delivered. The complete
contract and example are in [`docs/ROW_READER.md`](docs/ROW_READER.md).

`Reader::read_property_block(metadata_index, options, stop_token)` returns the
exact bytes of a block-backed String, vector, or matrix Property. It supports
attachment and `inline:base64`/`inline:hex` locations, all declared standard
element widths through 128-bit and complex arrays, optional native-endian
output, the reader codecs and checksum algorithms above, and independent
serialized/decoded Property byte limits. Matrix bytes retain XISF row-major
order. The library does not reinterpret these bytes as astronomy semantics;
that remains a consumer responsibility.

`Document::icc_profiles()` and `Document::icc_profile_bindings()` expose
ordered ICC block descriptors and direct or referenced image associations.
`Reader::read_icc_profile(index, stop_token)` reads local attachment and inline
profiles through the bounded integrity/decompression pipeline and returns their
exact big-endian bytes. It performs limited ICC header screening but does not
execute color management or claim complete ICC semantic validation.

`Document::thumbnails()` and `Document::thumbnail_bindings()` expose validated
UInt8/UInt16 Gray/RGB preview descriptors and their main-image associations.
`Reader::read_thumbnail(index, stop_token)` returns exact source-representation
pixels from attachment or embedded blocks; it never substitutes thumbnail
pixels for scientific image data or applies display transforms.

`Document::table_structures()`, `Document::tables()`, and
`Document::table_bindings()` expose bounded ordered `Structure`/`Table`
inspection. Schemas, rows, cells, declared shapes, serialization forms, and
image associations are validated and retained without coercing heterogeneous
values. Table Cell data blocks remain descriptors only and external locations
are never resolved.

By default pixel reads preserve the serialized byte order and Planar/Normal
layout exactly. Callers can pass `ImageReadOptions` to request native byte order
and either layout explicitly. UInt64, Complex32, and Complex64 use the same raw
bounded block path as the PFI scalar formats; complex endian conversion reverses
the real and imaginary components independently. No sample type, precision, or
complex-value conversion is performed, and attachment layout conversion uses
bounded scratch memory rather than a second full-frame buffer.

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
`--decode-icc-sha256` performs the bounded profile decode, structural screening,
and hash for every locally readable ICC profile.
`--decode-thumbnails-sha256` performs the bounded thumbnail decode and exact
source-representation pixel hash.
`--decode-rows` exercises the low-copy callback path and reports exact planar
row and decoded-byte counts plus the checksum state without retaining a full
decoded frame. Its incremental SHA-256 covers callback order; it equals the
source-representation pixel hash for Planar source/native-order output, while
Normal sources intentionally have a different row/channel delivery order.
The normal inspector output also lists the bounded semantic inventory of
non-XISF XML extension elements and their namespace-aware attributes; this is
inspection data, not a byte-identical XML round-trip representation.
Validated RGB working-space, display-function, CFA, and resolution objects are
listed separately with direct or referenced image associations. Their source
parameters are descriptive; the reader does not apply display/color transforms
or synthesize absent defaults.

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
library or its production-readiness claim. The generated local bundle embeds
Expat, LZ4, OpenSSL Crypto, zlib, and Zstandard when they resolve to non-system
dynamic libraries. Its sealed Resources directory includes the project license,
notice, security and contribution policies, third-party notices, and the exact
generated binary SPDX SBOM. A viewer-enabled test validates those resources,
the SBOM, and the complete deep code signature. Its current preview scope is the
first supported uncompressed, zlib-, LZ4-, or Zstandard-compressed
local/embedded Gray/RGB block in Planar or Normal layout,
with UInt8, UInt16, UInt32, Float32, or Float64 samples. The metadata inspector
can still open a broader set of headers and displays inventoried extension
elements/attributes, validated ancillary core objects, and ICC profile
descriptors/associations plus Thumbnail and Structure/Table records alongside
metadata, while
unsupported image decoding fails closed.

## License

This local foundation is licensed under Apache License 2.0. It is permissive,
includes an explicit patent grant, and permits use by both open-source
applications and PFI. The owner should reconfirm this choice at G0 before the
first external publication. Dependency and fixture licensing still require a
recorded audit.
