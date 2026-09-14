# M8 progress: hardening and distribution

- Status: IMPLEMENTATION_IN_PROGRESS; PUBLICATION_NOT_AUTHORIZED
- Started: 2026-09-14
- Publication status: private repository; no tag or release

## Implemented distribution gates

- Linux, macOS, and Windows Release builds in CI.
- Installed-package consumer configured only against the installed CMake
  package, not source-tree headers.
- Static library is the default; shared-library builds use explicit public
  symbol import/export annotations, hidden non-public symbols on supported
  compilers, and the same installed-package consumer.
- CI covers static and shared installs on Linux, macOS, and Windows. Windows
  shared-library tests add only the installed DLL directory to the test process
  path.
- Installed packages include Apache-2.0 `LICENSE`, `NOTICE`, `SECURITY.md`, and
  `THIRD_PARTY_NOTICES.md`.
- A checked SPDX 2.3 source-dependency SBOM names all five direct libraries,
  their declared licenses, and dependency relationships. A CTest gate ties its
  project version to CMake and rejects missing dependency licenses.
- Every configured static/shared build generates a second SPDX 2.3 SBOM with
  the exact Expat, zlib, LZ4, Zstandard, and OpenSSL versions parsed from the
  headers that are actually compiled. The installed artifact records platform,
  compiler, configuration, and linkage; missing versions fail closed.
- A separate CI job produces the Git source archive twice, requires byte
  identity, computes SHA-256, and retains the archive/checksum for 14 days
  without creating a tag or release.
- Contributor clean-room/testing requirements, the current untrusted-input
  security boundary, and an exact candidate/publication/rollback checklist are
  documented without claiming an unreleased support policy.
- A versioned Doxygen target generates the installed public-header API
  reference with warnings treated as errors. CI verifies its entry point and
  retains the HTML output as a short-lived, non-published artifact.
- ASan/UBSan deterministic 20,000-case mutation smoke and a Linux Clang
  coverage-guided 20,000-run job are wired into CI.
- CI keeps one active run per workflow/ref. A newer push cancels only the
  superseded run for the same branch, so the final commit receives the complete
  matrix without duplicating long Windows dependency builds.

## Evidence available now

- Fresh macOS Release shared build with warnings-as-errors: 6/6 tests PASS.
- Fresh shared install plus separately configured public package consumer: 1/1
  PASS.
- Export-table inspection exposes the public Reader, Writer, version, and enum
  string functions while hidden visibility remains enabled.
- Cross-platform shared-library evidence is pending the first CI run containing
  this matrix.

## Still required for public beta

- Complete and record the first static/shared cross-platform CI matrix.
- Run longer continuous fuzz campaigns and preserve any minimized regressions.
- Enable a private vulnerability-reporting channel before publication.
- Resolve native PixInsight interoperability gates for all claimed PFI writer
  profiles; UI automation remains intentionally excluded.
- Perform a final dependency/license/security audit before any public tag.
