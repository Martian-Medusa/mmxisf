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
- ASan/UBSan deterministic 20,000-case mutation smoke and a Linux Clang
  coverage-guided 20,000-run job are wired into CI.

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
- Add automated source archives and a reproducible dependency/SBOM artifact.
- Run longer continuous fuzz campaigns and preserve any minimized regressions.
- Complete API reference and contribution/release documentation.
- Resolve native PixInsight interoperability gates for all claimed PFI writer
  profiles; UI automation remains intentionally excluded.
- Perform a final dependency/license/security audit before any public tag.
