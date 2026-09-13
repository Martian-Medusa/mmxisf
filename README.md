# Martian Medusa XISF (`mmxisf`)

`mmxisf` is the planned standalone, clean-room C++ library for reading and
writing Extensible Image Serialization Format (XISF) files. Its first product
consumer will be PSF Field Inspector (PFI), but the library will not depend on
PFI, PixInsight, PCL, or Qt.

> Status: planning and build scaffold only. No XISF file can be read or written
> yet. The repository version is intentionally `0.0.0`.

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
- [Sources and clean-room policy](docs/SOURCES.md)

## Build the scaffold

```sh
cmake -S . -B build -DMMXISF_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The scaffold proves only that the standalone package shape compiles. It is not
format-support evidence.

## License

This local foundation is licensed under Apache License 2.0. It is permissive,
includes an explicit patent grant, and permits use by both open-source
applications and PFI. The owner should reconfirm this choice at G0 before the
first external publication. Dependency and fixture licensing still require a
recorded audit.
