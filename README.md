# Martian Medusa XISF (`mmxisf`)

`mmxisf` is the planned standalone, clean-room C++ library for reading and
writing Extensible Image Serialization Format (XISF) files. Its first product
consumer will be PSF Field Inspector (PFI), but the library will not depend on
PFI, PixInsight, PCL, or Qt.

> Status: M1 is in progress. Version `0.1.0` parses bounded monolithic XISF 1.0
> headers and inspects image descriptors, properties, and FITS keywords. A
> narrow, fail-closed raw attachment path powers the macOS viewer PoC. This is
> not yet a general XISF decoder.

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
- [Sources and clean-room policy](docs/SOURCES.md)

## Build the library and inspector

```sh
cmake -S . -B build -DMMXISF_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
build/mmxisf-inspect path/to/image.xisf
```

Normal configuration requires an installed Expat development package. No
dependency is downloaded implicitly.

The CI package gate also configures and runs the independent
`tests/package_consumer` project against the installed CMake package rather
than the source tree.

The pre-release reader can also consume a caller-provided seekable
`mmxisf::ByteSource`. Exact uncompressed attachment bytes can be returned in an
owning `RawImage` or written into a caller-owned span with cooperative
`std::stop_token` cancellation.

For a local sanitizer mutation smoke:

```sh
cmake -S . -B build-fuzz-smoke \
  -DMMXISF_BUILD_TESTS=OFF \
  -DMMXISF_BUILD_TOOLS=OFF \
  -DMMXISF_BUILD_FUZZ_SMOKE=ON
cmake --build build-fuzz-smoke
build-fuzz-smoke/mmxisf_fuzz_smoke
```

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
library. The generated local bundle embeds Expat. Its current preview scope is
the first uncompressed local Gray attachment with UInt8, UInt16, or Float32
samples. The metadata inspector can still open a broader set of headers, while
unsupported image decoding fails closed.

## License

This local foundation is licensed under Apache License 2.0. It is permissive,
includes an explicit patent grant, and permits use by both open-source
applications and PFI. The owner should reconfirm this choice at G0 before the
first external publication. Dependency and fixture licensing still require a
recorded audit.
