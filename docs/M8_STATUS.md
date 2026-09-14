# M8 progress: hardening and distribution

- Status: FIRST_LONG_FUZZ_PASS; CROSS_PLATFORM_MATRIX_PASS;
  ROUTINE_CI_MANUAL_ONLY_RUNNER_BUDGET;
  EXACT_HEAD_CROSS_PLATFORM_REVALIDATION_PENDING;
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
- Routine push and pull-request triggers are temporarily disabled to conserve
  hosted-runner minutes. The complete workflow remains available through
  manual dispatch for selected cross-platform checkpoints. Current development
  uses local static/shared, sanitizer, package-consumer, fuzz-smoke, and macOS
  viewer gates; local evidence does not substitute for Windows or Linux.

## Evidence available now

- Exact commit `a42f46fc6ab48d3bfc42afe84a5eb5fffd4ef7fa`, which switched
  routine CI to manual dispatch, passed the local macOS static and shared
  8/8 suites, both installed-package consumers (1/1 each), the ASan/UBSan
  8/8 suite, the deterministic 20,000-case mutation smoke, and generated API
  documentation. A remote run query after push returned no workflow for that
  commit, confirming that the automatic runner spend stopped. Windows and
  Linux for this exact commit remain **NOT_TESTED**.
- A 900-second local macOS arm64 coverage-guided campaign on commit
  `673d6179af37d6a7d55d24d94c65c170e37e2f53` executed 3,921,535 inputs,
  added 3,404 corpus units, peaked at 463 MiB RSS, and produced no crash or
  ASan/UBSan finding. Corpus replay reached `cov: 13866`, `ft: 38237`. It is
  `PASS_WITH_TOOLCHAIN_LIMITATION`, not candidate qualification, because the
  Apple Clang 21 compiler/sanitizers used an LLVM 17 libFuzzer runtime archive
  and the external symbolizer was unavailable. Exact evidence is retained in
  `docs/fuzz-campaigns/2026-09-14-local-macos-arm64.json`.
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
- The first complete Linux/macOS/Windows static/shared matrix passed on exact
  commit `cd78b31c2156a86439740cbd6a2f703d296dfc5c` in run
  [`34797999205`](https://github.com/Martian-Medusa/mmxisf/actions/runs/34797999205).
  All six library builds and their isolated installed-package consumers passed;
  the same run also passed deterministic source packaging, generated API
  documentation, the 20,000-case sanitizer smoke, and 20,000 coverage-guided
  mutations. The action-pin upgrade and retained binary-SBOM uploads were
  committed later and therefore remain pending their own exact-head run.
- The source/license/supply-chain review is recorded in
  `docs/SUPPLY_CHAIN_AUDIT.md` as a conditional source-only pass. Binary
  vulnerability review and the public security intake remain release gates.
- Exact-head run
  [`34799336562`](https://github.com/Martian-Medusa/mmxisf/actions/runs/34799336562)
  created no runner or build steps for commit
  `3bd2ef2f37d464cece5d4d88f9375ccaaf007ab8`. Every job received GitHub's
  account-level annotation that recent payments failed or the Actions spending
  limit must be increased. This is an external billing block, not a code,
  workflow, action-pin, or test failure. The official `v7.0.1` tags resolve to
  the exact checkout and upload-artifact commits configured in the workflow.
- A later exact-head attempt for row delivery, run
  [`34800837295`](https://github.com/Martian-Medusa/mmxisf/actions/runs/34800837295)
  on commit `fe588161485c8852dd30946669a79ddb5a96c739`, produced the same
  account-level billing/spending-limit annotation for all eight jobs before any
  runner step. This independently confirms that the current CI blocker remains
  external to repository code.
- Exact-head run
  [`34802246968`](https://github.com/Martian-Medusa/mmxisf/actions/runs/34802246968)
  repeated the same result on commit
  `1bf816ecc1c2351f23c50bbdfd791a4ee15f1912`: all eight jobs received the
  account payment/spending-limit annotation and created no build step. The
  exclusive temporary-file creation fix is therefore validated locally but
  remains pending cross-platform exact-head CI after the account block is
  resolved.

## Still required for public beta

- Manually dispatch and complete an exact-candidate Linux/macOS/Windows matrix
  with the upgraded immutable action pins, retained binary SBOMs, sequential
  writer-sink API, and row-delivery API; routine pushes intentionally remain
  local-only until the hosted-runner budget policy is revisited.
- Repeat the long campaign on the exact release candidate and preserve/promote
  any minimized regressions; the first retained campaign is complete.
- Enable a private vulnerability-reporting channel before publication.
- Resolve native PixInsight interoperability gates for all claimed PFI writer
  profiles; UI automation remains intentionally excluded.
- Perform a final dependency/license/security audit before any public tag.
