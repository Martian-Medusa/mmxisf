# Supply-chain and license audit

- Audit date: 2026-09-14
- Scope: source-only public-beta candidate preparation
- Result: source/license review **CONDITIONAL PASS**; current local macOS
  development binaries **BLOCKED / UPGRADE REQUIRED**; public security intake
  remains gated

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
from the GitHub Actions pins.

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
3. GitHub vulnerability alerts are disabled for the current private repository.
   GitHub private vulnerability reporting is unavailable while the repository
   is private. Enable dependency alerts now if desired, and enable the private
   reporting form when the repository becomes public.
4. Run an exact-version vulnerability audit for all candidate binaries and the
   build toolchain immediately before release. The 2026-09-14 development audit
   is evidence of a known failed baseline, not a substitute for that
   time-sensitive frozen-candidate check.
5. Native PixInsight interoperability and the release-candidate long fuzz run
   remain independent mandatory gates.

No tag, release, binary publication, security-support promise, or reproducible
binary claim is authorized by this audit.
