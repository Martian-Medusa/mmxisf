# Supply-chain and license audit

- Audit date: 2026-09-15 (refreshed for `v0.1.0-rc.1`)
- Scope: source-only public-beta candidate preparation
- Result: source/license review **PASS**; legacy local macOS
  development binaries **BLOCKED / UPGRADE REQUIRED**; isolated pinned macOS
  arm64, Linux amd64, and native Windows amd64/MSVC dependency graphs **PASS**;
  public security intake, exact frozen-candidate dependency review, and dated
  source-only release security disposition **PASS**

## Reviewed controls

- The project is Apache-2.0 and the install package includes `LICENSE`,
  `NOTICE`, `SECURITY.md`, `CONTRIBUTING.md`, and
  `THIRD_PARTY_NOTICES.md`.
- Direct dependencies are limited to Expat, zlib, LZ4, Zstandard, and OpenSSL
  Crypto. Their selected permissive license paths are recorded in the source
  SPDX 2.3 SBOM and reproduced in the third-party notices.
- Configured builds generate an SPDX 2.3 binary SBOM from the dependency
  headers actually selected by CMake. CI now retains that exact record for
  every Linux/macOS static/shared build and both Windows linkage variants.
- GitHub Actions are pinned to immutable full commit identifiers. Checkout does
  not persist a workflow token because no job writes to the repository.
- Dependabot version updates are enabled for the pinned GitHub Actions. Proposed
  updates still have to pass the complete matrix before merging.
- Workflow permissions are read-only, artifact retention is finite, source
  archive generation is deterministic, and release publication is not part of
  ordinary CI.

## Dependency license disposition

| Dependency | Selected license | Distribution disposition |
| --- | --- | --- |
| Expat | MIT | compatible; full notice retained |
| zlib | Zlib | compatible; full notice retained |
| LZ4 library | BSD-2-Clause | compatible; library notice retained |
| Zstandard | BSD-3-Clause | compatible; BSD path and notice retained |
| OpenSSL 3 | Apache-2.0 | compatible with project license; notice retained |

This audit covers the library dependencies actually used by `mmxisf`; it does
not claim that unrelated command-line programs shipped by upstream projects
have the same license.

## Open release gates

The dated review of the current macOS arm64 provider is retained in
[`security-audits/2026-09-14-macos-arm64.md`](security-audits/2026-09-14-macos-arm64.md).
It fails the production dependency baseline: Expat 2.5.0 and the end-of-life
OpenSSL 3.2.0 are hard blockers, while zlib, LZ4, and Zstandard must also be
refreshed to the documented release floor. Release-candidate configuration now
has an opt-in fail-closed dependency-baseline check; beta and production builds
must enable it. A checked vcpkg manifest pins an immutable official registry
commit whose five selected ports meet the dated floor without modifying the
host package installation; Dependabot monitors that baseline independently
from the GitHub Actions pins. The fresh exact-commit local result is retained in
[`security-audits/2026-09-14-vcpkg-macos-arm64.json`](security-audits/2026-09-14-vcpkg-macos-arm64.json):
12/12 tests, install, installed consumer 1/1, binary SBOM, system-only viewer
linkage, and strict deep ad-hoc signature verification passed.

The same exact registry baseline then passed a separate extracted-source Linux
amd64 Docker gate in both static and shared forms. Both library suites passed
12/12, both installed-package consumers passed 1/1, the generated API reference
passed, and the shared library exposed no non-system runtime dependency because
the five vcpkg libraries were linked statically. Exact library and SBOM hashes
from the original gate are retained in
[`security-audits/2026-09-14-vcpkg-linux-amd64.json`](security-audits/2026-09-14-vcpkg-linux-amd64.json).
The newer exact-development-head replay also proved persistent binary-cache
population and isolated restoration with byte-identical static and shared
libraries; its exact evidence is retained in
[`security-audits/2026-09-14-vcpkg-cache-linux-amd64-8c33a65.json`](security-audits/2026-09-14-vcpkg-cache-linux-amd64-8c33a65.json).
The exact dependencies of the later macOS arm64 PFI provider snapshot at
`3fd55e6a0ad99a645388ef842fe040b996756a91` also passed a dated official-
upstream advisory review. The result and API-scoped LZ4 disposition are retained
in
[`security-audits/2026-09-14-development-binary-advisory-review-3fd55e6.md`](security-audits/2026-09-14-development-binary-advisory-review-3fd55e6.md).
These add development evidence; they do not freeze a candidate or replace the
final time-sensitive vulnerability and complete-toolchain review.

The frozen `v0.1.0-rc.1` candidate now has six exact binary SBOMs from the
viewer-free macOS arm64, Linux amd64, and Windows-target MinGW static/shared
production-graph gates. All bind the same five versions from vcpkg commit
`a1cae005c39be7b18ba319fced856b68d7276271`. A refreshed official-upstream
review passes those direct dependencies and retains the LZ4 unsafe-API scope
check in
[`security-audits/2026-09-14-rc1-binary-advisory-review.md`](security-audits/2026-09-14-rc1-binary-advisory-review.md).
The exact candidate subsequently passed the native Windows Server 2025
amd64/MSVC functional and package gate. Its complete runner/toolchain inventory,
the public-repository controls, the distributed source-only asset set, and the
dated advisory disposition are recorded in
[`security-audits/2026-09-15-rc1-source-release-security-review.json`](security-audits/2026-09-15-rc1-source-release-security-review.json).
That review closes the security gate only for the source-only prerelease: no
Windows or other platform binary, PDB, viewer, compiler, SDK, linker, or PFI
artifact is distributed, and no universal toolchain-vulnerability claim is
made. Any later binary distribution requires a new artifact-specific review.

The later viewer-only distribution rehearsal at exact clean source commit
`53c4845857eaeed7bfd610c7f41a170862a633e1` embeds the exact binary SBOM,
project license and notice, security/contribution policies, and third-party
notices inside the sealed application. Its production dependency floor,
warnings-as-errors build, 13/13 tests, resource contract, system-only runtime
linkage, strict deep ad-hoc signature, ZIP extraction, and extracted signature
all passed. Exact identities are retained in
[`security-audits/2026-09-14-viewer-macos-arm64-53c4845.json`](security-audits/2026-09-14-viewer-macos-arm64-53c4845.json).
The app remains a local arm64 development artifact: it is neither Developer ID
signed nor notarized, and no publication is authorized. These are viewer-only
distribution gates and do not block the reusable C++ library's readiness.

1. Dependency versions are resolved by the target system or CI package manager.
   The generated binary SBOM provides exact provenance, but a cross-toolchain
   binary is not claimed to be reproducible from the source archive alone.
2. The local macOS viewer is ad-hoc signed and currently embeds the versions
   available on the development host: Expat 2.5.0, zlib 1.3, LZ4 1.9.4,
   Zstandard 1.5.5, and OpenSSL 3.2.0. The host's MacPorts installation reports
   an operating-system platform mismatch, so this bundle is explicitly a
   development artifact, not an approved public binary. The dated audit confirms
   that this bundle cannot be promoted. A release candidate needs dependencies
   rebuilt from a controlled supported environment at or above the documented
   floor, baseline enforcement enabled, a fresh exact-version vulnerability
   review, signing, notarization, hashes, and a retained binary SBOM.
3. The repository is public. Private vulnerability reporting, Dependabot
   security updates, secret scanning with push protection, read-only default
   workflow permissions, selected Actions, and full-SHA action pinning are
   enabled. The dated control snapshot is retained in
   [`security-audits/2026-09-15-public-repository-controls.json`](security-audits/2026-09-15-public-repository-controls.json).
4. The exact source-only asset set has a dated vulnerability and build-host
   disposition. Refresh it for any later publication date, changed dependency,
   toolchain, binary distribution, or newly relevant advisory.
5. Native PixInsight reader/writer interoperability and the release-candidate
   long fuzz campaign passed as independent gates; their bounded evidence does
   not imply universal XISF compatibility or parser safety.

This audit authorizes no binary publication, security-support service level, or
portable reproducible-binary claim. The repository owner separately authorized
the source-only `v0.1.0-rc.1` prerelease. It is published, and independent
post-publication download, SHA-256, immutable-tag, archive-identity, build,
install, and installed-package-consumer verification passed as recorded in
[`quality-runs/2026-09-15-rc1-release-verification.json`](quality-runs/2026-09-15-rc1-release-verification.json).
