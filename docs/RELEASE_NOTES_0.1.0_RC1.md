# mmxisf 0.1.0-rc.1 release notes

`v0.1.0-rc.1` is the first frozen release candidate of the standalone,
reusable C++20 `mmxisf` library. It is a qualification candidate, not a general
availability release. This prerelease is intentionally source-only; it does
not distribute platform binaries, the optional viewer, or PFI artifacts.

## Library scope

- Bounded XISF 1.0 parsing and image decoding for the profiles classified in
  `docs/support-profile-0.1.0.json`.
- Mono and RGB image data, supported integer/floating sample formats, declared
  compression/shuffle/checksum combinations, structured metadata, Properties,
  tables, ICC profiles, thumbnails, and sequential row delivery as classified
  by the frozen support profile.
- Deterministic XISF writing for the declared writer profiles, including
  caller-owned sequential byte sinks.
- Static and shared CMake package consumption, including embedding with
  `add_subdirectory` without default install/example pollution.
- Explicit resource budgets, cooperative cancellation, stable error categories,
  and no third-party implementation types in installed headers.

The complete row-by-row claim boundary, including `SUPPORTED`, `LIMITED`, and
`NOT_TESTED` entries, is the machine-readable
`docs/support-profile-0.1.0.json`. The optional macOS viewer and the PFI adapter
are not part of the standalone library readiness claim.

## Compatibility policy

The API is pre-1.0. Source compatibility follows Semantic Versioning from the
first public release, but **no ABI compatibility is promised before 1.0**.
Consumers must rebuild against the selected pre-1.0 library version. No C ABI,
exception-disabled toolchain profile, or stable binary plugin boundary is
claimed.

## Candidate identity

- Repository: `Martian-Medusa/mmxisf`
- Annotated tag: `v0.1.0-rc.1`
- Commit: `fd62b5b2a0239638d2ea252912212b3fa4f92178`
- Deterministic source archive:
  `mmxisf-0.1.0-source-fd62b5b2a023.tar.gz`
- Archive SHA-256:
  `cc1d5065726ab61cf151592a29a91edc1b293d2e24554b2f57601763d90ad375`

The release assets are the deterministic archive, its SHA-256 sidecar, the
source-candidate identity manifest, and the source-dependency SPDX 2.3 SBOM.
The manifest's `PREPARED_NOT_PUBLISHED` state records the fail-closed state in
which the archive was generated; publication authority was granted separately
by the repository owner and is not encoded by a package manifest.

## Publication verification

The GitHub prerelease was published at
<https://github.com/Martian-Medusa/mmxisf/releases/tag/v0.1.0-rc.1>. All four
assets were downloaded into a fresh directory and matched both their prepared
SHA-256 values and GitHub digests. The remote annotated tag still peels to the
candidate commit. The downloaded archive independently passed candidate and
Git-archive byte-identity validation, a warnings-as-errors macOS arm64 build,
19/19 tests, installation, and 2/2 installed-package consumer tests. The viewer
was excluded. Full evidence is retained in
`docs/quality-runs/2026-09-15-rc1-release-verification.json`.

## Qualification state

The exact candidate passes viewer-free pinned-dependency static/shared gates on
macOS arm64 and Linux amd64. Windows-target MinGW binaries and package consumers
also pass under Wine. Native Windows Server 2025 amd64/MSVC passes the
warnings-as-errors functional gate: static 18/18 and shared 19/19 tests,
direct/relocated installed consumers, embedded consumers, dependency floors,
SBOM generation, and the exact shared-export allowlist.

The exact-candidate public headers are byte-identical to the audited nine-header
baseline. The macOS, Linux, MinGW, and native MSVC shared builds expose the
expected 39 `mmxisf` symbols without unexpected dependency exports. The
exact-candidate long fuzz campaign executed 2,387,632 inputs without a crash,
sanitizer finding, timeout, or slow input. The dated source-release security
review passes for the published asset scope, and private vulnerability
reporting is enabled on the public repository.

## Known limitations

- The native MSVC static library was not byte-identical across distinct build
  directories. Functional qualification passed, shared import/runtime
  libraries were byte-identical, no Windows binary is distributed, and no
  portable binary-reproducibility claim is made.
- The support profile is bounded and includes explicit `LIMITED` and
  `NOT_TESTED` XISF rows; this is not a universal XISF implementation claim.
- The API is pre-1.0 and does not carry an ABI compatibility promise.
- The optional macOS viewer remains a local PoC and is not a release asset.
- PFI operator acceptance and rollback exercise are separate product gates and
  do not contribute to the standalone-library release claim.
