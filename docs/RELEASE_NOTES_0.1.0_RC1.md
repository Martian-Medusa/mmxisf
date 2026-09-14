# mmxisf 0.1.0-rc.1 release notes

`v0.1.0-rc.1` is the first frozen release candidate of the standalone,
reusable C++20 `mmxisf` library. It is a qualification candidate, not a general
availability release. Publication of a GitHub Release or release assets
requires separate owner approval.

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

The archive, checksum, and source-candidate manifest are prepared locally and
are not published release assets.

## Qualification state

The exact candidate passes viewer-free pinned-dependency static/shared gates on
macOS arm64 and Linux amd64. Windows-target MinGW binaries and package consumers
also pass under Wine. Native Windows amd64/MSVC qualification is still missing,
so the cross-platform production gate remains limited rather than passing.

The exact-candidate public headers are byte-identical to the audited nine-header
baseline. The macOS, Linux, and MinGW shared builds expose the expected 39
`mmxisf` symbols without unexpected dependency exports. The exact-candidate
long fuzz campaign and time-sensitive security disposition are recorded
separately because their evidence and limitations have different lifetimes.

## Known release blockers

- Native Windows amd64/MSVC build, test, install, relocation, export, and
  reproducibility evidence.
- A complete candidate build-toolchain vulnerability disposition; the direct
  binary dependency review alone is not sufficient.
- A private vulnerability-reporting channel suitable for a public project.
- Published-release asset identity and post-publication download verification.
- PFI native operator acceptance and rollback exercise for the product claim.
