# M8 progress: hardening and distribution

- Status: FIRST_LONG_FUZZ_PASS; CURRENT_HEAD_MATRIX_PENDING;
  PUBLICATION_NOT_AUTHORIZED
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
- CI retains the exact generated binary SBOM for every Unix matrix entry and
  both Windows linkage variants, independently from the source SBOM.
- All third-party GitHub Actions are pinned to immutable commit identifiers,
  checkout does not persist the read-only workflow token, and weekly
  Dependabot proposals track action updates without bypassing the test matrix.
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
- A separate manual candidate workflow runs a 15-minute ASan/UBSan libFuzzer
  campaign with explicit input/time/RSS bounds, retains its evolved corpus, and
  preserves crash inputs longer for minimization and regression promotion.
- CI keeps one active run per workflow/ref. A newer push cancels only the
  superseded run for the same branch, so the final commit receives the complete
  matrix without duplicating long Windows dependency builds.
- The Windows static and shared gates share one dependency installation in a
  single job, then use isolated build/install/consumer directories. This keeps
  both linkage variants while removing the dominant duplicated vcpkg setup.

## Evidence available now

- Fresh macOS Release shared build with warnings-as-errors: 6/6 tests PASS.
- Fresh shared install plus separately configured public package consumer: 1/1
  PASS.
- Export-table inspection exposes the public Reader, Writer, version, and enum
  string functions while hidden visibility remains enabled.
- The first retained 15-minute ASan/UBSan reader campaign passed on exact
  commit `44d91da46655deb73c7b258a1204e757bb9bb90e` in run
  [`34792034236`](https://github.com/Martian-Medusa/mmxisf/actions/runs/34792034236):
  2,793,891 executions, final `cov: 10844`, `ft: 30853`, 764 live corpus
  units/1,119 KiB, 505 MiB RSS, and no crash, timeout, or sanitizer finding.
  The 785-file evolved-corpus artifact has ID `10328078617` and
  workflow-reported ZIP SHA-256
  `d629c94c8b581c6535840115cebc9a8d5b9fe82af565697350b261a7eaf02b30`.
  Later SHA-3 and CI-only changes are outside this historical campaign.
- Cross-platform shared-library evidence is pending the first CI run containing
  this matrix.
- The source/license/supply-chain review is recorded in
  `docs/SUPPLY_CHAIN_AUDIT.md` as a conditional source-only pass. Binary
  vulnerability review and the public security intake remain release gates.

## Still required for public beta

- Complete and record the first static/shared cross-platform CI matrix.
- Repeat the long campaign on the exact release candidate and preserve/promote
  any minimized regressions; the first retained campaign is complete.
- Enable a private vulnerability-reporting channel before publication.
- Resolve native PixInsight interoperability gates for all claimed PFI writer
  profiles; UI automation remains intentionally excluded.
- Perform a final dependency/license/security audit before any public tag.
