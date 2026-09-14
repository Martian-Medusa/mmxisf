# Contributing to mmxisf

Thank you for helping build a safe standalone XISF implementation.

## Clean-room boundary

Work only from the public XISF specification, independently generated files,
documented public APIs, and behavior observed through black-box tests. Do not
inspect, copy, translate, or adapt implementation code from GPL `libXISF` or
PixInsight/PCL. Record the provenance and license/redistribution basis of every
external fixture.

The core must remain independent of PFI, PixInsight, PCL, Qt, and consumer
astronomy semantics. Integrations belong in their consumer repositories behind
narrow adapters.

## Build and test

The baseline is C++20 with CMake 3.20 or newer and installed Expat, zlib, LZ4,
Zstandard, and OpenSSL Crypto development packages.

```sh
cmake -S . -B build -DMMXISF_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Also test `-DBUILD_SHARED_LIBS=ON` for public API/package changes. Parser,
codec, block, arithmetic, ownership, and cancellation changes require the
ASan/UBSan mutation smoke described in `README.md`. During the current hosted
runner budget pause, ordinary pushes do not start GitHub Actions: contributors
must run the applicable local gates, while the manually dispatched CI workflow
remains authoritative for Linux/macOS/Windows and coverage-guided fuzz
checkpoints. A local macOS pass is never recorded as Windows or Linux evidence.
The candidate-campaign evidence and regression-promotion rules are in
`docs/FUZZING.md`.

The maintained one-command local gate is:

```sh
tools/run_local_quality_gates.sh
```

On a Docker-capable Linux host, the pinned Ubuntu 24.04 amd64 environment and
the same maintained gate can be built and run with:

```sh
tools/run_linux_docker_gate.sh
```

The container mounts only the current checkout at `/work`, writes its build
tree to `build-linux-docker-gates/`, and does not require or modify any running
application service. The image digest and package installation recipe are
versioned under `containers/`. Release/package builds use GCC, while sanitizer
and fuzz targets use Clang. Both compilers retain warnings as errors; the Linux
gate disables only `-Wmissing-field-initializers` because the public test
records intentionally exercise C++20 aggregate defaults.

Do not treat an incremental successful compile alone as equivalent to this
static/shared/install/sanitizer/documentation/viewer gate.

## Change requirements

- Treat all sizes, offsets, dimensions, XML, compressed streams, and external
  references as untrusted.
- Preserve exact pixel layout, byte order, type, metadata text, and provenance;
  never replace missing information with a fabricated value.
- Fail closed with a structured error for malformed, unsupported, overflowing,
  overlapping, checksum-invalid, or resource-exhausting inputs.
- Add positive, malformed, and boundary tests. Round-trip tests cannot be the
  sole interoperability oracle.
- Public API changes require an ADR under `docs/decisions/` and an explicit
  pre-1.0 SemVer/ABI impact statement.
- Update the conformance matrix and milestone/status documentation when a
  support claim changes.
- Use a concise Conventional Commit subject.

## Fixtures

Follow `docs/FIXTURE_POLICY.md`. Compact deterministic Base64 fixtures may be
normal Git text. Astronomy binaries use Git LFS. Never commit a private corpus;
retain only approved hashes, producer/version, and non-sensitive results.

## Security

Follow `SECURITY.md`. Do not file a public issue for a suspected vulnerability
or include proprietary image data without permission.
