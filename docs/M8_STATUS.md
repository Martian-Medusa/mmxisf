# M8 progress: hardening and distribution

- Status: FIRST_LONG_FUZZ_PASS; CROSS_PLATFORM_MATRIX_PASS;
  ROUTINE_CI_MANUAL_ONLY_RUNNER_BUDGET;
  SOURCE_CANDIDATE_REHEARSAL_PASS;
  PFI_PRODUCT_ROUTER_FOUNDATION_LIMITED;
  PFI_SECURE_STATIC_PROVIDER_GATE_PASS;
  ISOLATED_MACOS_DEPENDENCY_BASELINE_PASS;
  EXACT_HEAD_LINUX_PRODUCTION_GATE_PASS;
  EXACT_HEAD_CROSS_PLATFORM_REVALIDATION_PENDING;
  PUBLICATION_NOT_AUTHORIZED
- Started: 2026-09-14
- Publication status: private repository; no tag or release

The machine-checked `docs/production-readiness.json` ledger currently derives
`NOT_READY` independently for standalone beta, standalone production, and PFI
production. It prevents a documentation-only readiness claim while required
candidate, interoperability, security, distribution, or product gates remain
open; it does not turn historical evidence into exact-candidate evidence.

## Implemented distribution gates

- Linux, macOS, and Windows Release builds in CI.
- Installed-package consumer configured only against the installed CMake
  package, not source-tree headers.
- The maintained Linux production-dependency gate builds each static and shared
  library twice in distinct build directories and requires byte identity.
- The maintained viewer-free local Unix gate applies the same distinct-build
  byte-identity check to static and shared libraries, including macOS arm64.
  The macOS gate explicitly enables Apple's `ZERO_AR_DATE=1` reproducible-
  archive mode instead of post-processing the resulting static library.
- The pinned-vcpkg macOS arm64 production gate is library-first: viewer-free
  static/shared builds, direct and relocated installed consumers, embedded
  consumers, documentation, dependency floors, and binary reproducibility run
  by default. The viewer requires the separate `MMXISF_VCPKG_VIEWER=ON` opt-in.
- Static library is the default; shared-library builds use explicit public
  symbol import/export annotations, hidden non-public symbols on supported
  compilers, and the same installed-package consumer.
- Linux and macOS shared links apply platform-native allowlists for the public
  `mmxisf` namespace, preventing symbols from statically linked implementation
  dependencies from becoming part of the dylib/ELF interface.
- CI covers static and shared installs on Linux, macOS, and Windows. Windows
  shared-library tests add only the installed DLL directory to the test process
  path.
- A supplemental Linux amd64 Docker gate cross-compiles static and shared
  Windows amd64 artifacts with the pinned vcpkg graph, runs the executable
  suites and direct/relocated/embedded consumers under 64-bit Wine, checks the
  named PE export surface, and requires byte-identical repeated libraries. It
  is an inexpensive compatibility signal, not a substitute for native MSVC on
  a supported Windows host.
- A viewer-free native Windows amd64 PowerShell gate now packages the complete
  MSVC path into one fail-closed command: exact vcpkg-baseline verification,
  dependency floors, warnings-as-errors static/shared suites, direct and
  relocated installed consumers, embedded consumers, SBOM checks, MSVC DLL
  export inspection, and distinct-build binary identity. Its parser has
  positive, unexpected-export, and empty-export contracts on every host; the
  native gate itself remains NOT_TESTED until a Windows amd64 host runs it.
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
  identity, computes SHA-256, and retains the archive/checksum plus a source-
  candidate identity manifest for 14 days without creating a tag or release.
  The shared local/CI preparation script rejects a dirty worktree, non-HEAD
  ref, or existing output directory and records publication as unauthorized.
- Contributor clean-room/testing requirements, the current untrusted-input
  security boundary, and an exact candidate/publication/rollback checklist are
  documented without claiming an unreleased support policy.
- A versioned Doxygen target generates the installed public-header API
  reference with warnings treated as errors. CI verifies its entry point and
  retains the HTML output as a short-lived, non-published artifact.
- ASan/UBSan deterministic 20,000-case mutation smoke and a Linux Clang
  coverage-guided 20,000-run job are wired into CI.
- A ThreadSanitizer suite is wired into the existing manual Linux fuzz job and
  the local quality gate. It exercises concurrent owning/row reads through one
  built-in file-backed Reader plus the existing concurrent same-destination
  writer contract. The exact-commit local AppleClang run passes; Linux candidate
  evidence remains to be recorded when the manual matrix is dispatched.
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
  uses local macOS and Linux static/shared, sanitizer, package-consumer,
  fuzz-smoke gates, the supplemental MinGW/Wine cross-gate, and the separate
  macOS viewer gate. None substitutes for the frozen-candidate platform matrix.

## Evidence available now

- Exact development commit
  `6d1eab0e6ae45041b967021cfc3979fd8421882e` passed the supplemental Windows
  amd64 cross-gate from its deterministic 342,832-byte extracted source archive
  (SHA-256 `209f1314bf5e7754275d19b83fd7ea4a2002898d269bd15eb01d44e0711b6dad`)
  on `mllse`. MinGW GCC 13 warnings-as-errors static 19/19 and shared 20/20 suites
  ran under 64-bit Wine, as did direct and relocated installed consumers 2/2
  and embedded consumers 1/1. The DLL exposes exactly 39 named `mmxisf`
  symbols, and distinct builds produced byte-identical static libraries, DLLs,
  and import libraries. The pinned production dependency graph and binary SBOM
  gates also passed. The new MSVC export parser passed positive, unexpected,
  and empty-table contracts, while the native gate script passed a syntax check
  using a digest-pinned official PowerShell container. Exact source, container,
  toolchain, artifact, and retained-path evidence is recorded in
  `docs/quality-runs/2026-09-14-mingw-amd64-6d1eab0.json`. This is a useful
  Windows-target compatibility result, but MinGW/Wine and a PowerShell syntax
  check are not native Windows or MSVC execution evidence and do not close the
  frozen-candidate platform gate.

- Exact development commit
  `bec3e79ae48e0e8381abd7c22bbe588baa815e86` produced a deterministic
  334,413-byte source archive with SHA-256
  `2c73cc0674093caa25c5745b69b86de6cc67920c26bf39fcf0f173aea99507fa`.
  Its extracted source passed the pinned-vcpkg macOS arm64 production gate:
  warnings-as-errors static 18/18 and shared 19/19, direct and relocated
  package consumers 2/2, embedded consumers 1/1, production dependency floors,
  SBOMs, API documentation, and byte-identical repeated static and shared
  builds. An initial shared build exposed 8,145 implementation-dependency
  symbols; the committed Darwin export allowlist reduced the final dylib to
  exactly the intended 39 `mmxisf` exports and system-runtime-only dynamic
  linkage. Full evidence is retained in
  `docs/quality-runs/2026-09-14-macos-arm64-bec3e79.json`. This remains an
  unpublished, unfrozen, library-only result; the viewer was excluded.

- Exact development commit
  `9a1331baa3ff76108fe64f8296f8b3c80fde55b8` produced a deterministic
  332,493-byte source archive with SHA-256
  `d6610086403f52a746bf5839a367983b448af576bdd2f2edcadb2f6ee8a92822`.
  Its extracted source passed the viewer-free macOS arm64 gate: static 18/18,
  shared 19/19, direct and relocated package consumers 2/2, embedded consumers
  1/1, ASan/UBSan 18/18 plus 20,000 mutations, ThreadSanitizer 18/18, API
  documentation, and the 39-of-39 export check. Apple's reproducible archive
  mode produced byte-identical static libraries, and the two shared-library
  builds were also byte-identical. Full evidence is retained in
  `docs/quality-runs/2026-09-14-macos-arm64-9a1331b.json`. The system dependency
  graph is not production-baseline evidence; this remains an unpublished,
  unfrozen, library-only result.

- Exact development commit
  `28aef678ebc104aa924f38d929bdbe15edb6db03` produced a deterministic
  330,833-byte source archive with SHA-256
  `b11b601471da2834f00947c70ffcc466bccd2929c0ae447df3674e566cc5b2ae`.
  The archive passed independent commit/archive verification before transfer;
  its extracted source passed the pinned Linux amd64 production gate: static
  18/18, shared 19/19, direct and relocated package consumers 2/2, and embedded
  consumers 1/1 for both linkage forms. Distinct repeated builds produced
  byte-identical static and shared libraries. The public API baseline,
  dependency isolation and floors, SBOMs, generated API documentation, and
  exact 39-of-39 shared-export contract also passed. Full evidence is retained
  in `docs/quality-runs/2026-09-14-linux-amd64-28aef67.json`. This remains
  unpublished, unfrozen, library-only evidence; the viewer was excluded.

- Candidate identity no longer relies on the impossible requirement that a
  tracked file contain the hash of its own commit. Support-profile schema 1.1
  and readiness schema 2 bind a future immutable `vX.Y.Z-rc.N` name, while the
  deterministic source manifest independently binds the exact commit, archive
  bytes, and support-profile hash. Profile and ledger state/ref pairs are
  machine-checked; post-tag source preparation can require the ref to resolve
  to checked-out `HEAD`. This is release-mechanism hardening only: the current
  profile remains `PREPARED`, the ledger remains `UNFROZEN`, no tag exists, and
  no publication is authorized.

- Exact development commit
  `ca0ead3c0165257b874a6eb3f65e2f4fea04b261` adds a machine-enforced
  public-header baseline for the audited 0.1 API. Its deterministic
  329,621-byte source archive (SHA-256
  `fa7c154c00f9b5599135ee5b09c98a7b3921151652786116a086537d085a4eed`)
  passed commit/archive verification and the extracted Linux production gate:
  static 18/18, shared 19/19, direct and relocated package consumers 2/2, and
  embedded consumers 1/1 for both linkage forms. All nine public headers match
  their audited hashes; four mutation cases fail closed, and both installed
  linkage variants export and revalidate the same baseline. Full evidence is
  retained in `docs/quality-runs/2026-09-14-linux-amd64-ca0ead3.json`. This is
  still unpublished, unfrozen library evidence and not an ABI promise.

- Exact development commit
  `a0bb9a6fe794d9074eb3d163e66b9b8e7152f9f2` adds an independent,
  fail-closed source-candidate verifier. Its positive contract and five
  mutations cover publication authority, expected commit, archive name,
  archive SHA-256, and support-profile binding. The clean commit produced a
  deterministic 325,686-byte archive with SHA-256
  `4f7469def9cf95c28ee1c2eff12c34d004cc88f7d3358700ad401af7310bda10`.
  Verification passed both locally and after transfer to the pinned Linux
  container. The extracted source then passed static 16/16, shared 17/17,
  direct and relocated installed consumers 2/2, and embedded consumers 1/1
  for both linkage forms. Full evidence is retained in
  `docs/quality-runs/2026-09-14-linux-amd64-a0bb9a6.json`; this remains an
  unpublished, unfrozen library-only result.

- Exact development commit
  `1c4c8119ebe65732ad13cd953563a14db6e83b91` produced a deterministic
  323,047-byte source archive with SHA-256
  `0a2f9c90ef69914753bbba57ec499b21d910d2b5cffdcde4c193415081bd1917`
  and a 632-byte schema-1.1 source manifest. The manifest binds the exact
  commit and archive to the PREPARED support-profile hash without inventing an
  immutable candidate ref; forcing ref verification in that state failed
  closed as designed. The extracted archive then passed the pinned Linux amd64
  production gate: static 15/15, shared 16/16, direct and relocated installed
  consumers 2/2, and embedded consumers 1/1 for both linkage forms. Installed
  examples, all nine public headers, dependency floors, SBOMs, documentation,
  package isolation, and the exact 39-of-39 shared-export contract passed. Full
  evidence is retained in
  `docs/quality-runs/2026-09-14-linux-amd64-1c4c811.json`; no viewer was built
  or counted, and this remains unfrozen development evidence.

- Exact development commit
  `7a259188b91de0d92dddf0a79628e8ca7e40a148` produced a deterministic
  320,657-byte source archive with SHA-256
  `e046ae7018b7f895ddd3845fce2d7de5896f6e096f7fa8df66a9f3422896f615`.
  Its extracted-source Linux amd64 gate passed static 15/15 and shared 16/16.
  Direct and relocated installed consumers passed 2/2 for both linkage forms:
  the package contract plus a real write/read round trip built from installed
  public examples. Shared consumers found no private development dependency;
  embedded consumers passed 1/1 without creating example targets or install
  payload. All nine public headers, documentation, dependency floors, SBOMs,
  and the exact 39-of-39 shared-export surface passed. Full evidence is retained
  in `docs/quality-runs/2026-09-14-linux-amd64-7a25918.json`; the viewer was not
  built and contributes nothing to this result.

- Exact development commit
  `81b26911afb14ba2aa3ba7e09854301ed7387be0` produced a deterministic
  318,030-byte source archive with SHA-256
  `f5a634d2d4c84fc4ebea32f09d9119c00c5b0f6a06f659acbc6cba77e4813ff1`.
  Its extracted-source Linux amd64 production-dependency gate passed static
  14/14 and shared 15/15, direct and relocated installed consumers 1/1, and
  embedded consumers 1/1 for both linkage forms. Both embedded variants also
  produced no mmxisf install payload when their parent install command ran,
  while top-level installation remained enabled and complete. Shared consumers
  configured with discovery of Expat, LZ4, OpenSSL, zlib, and Zstandard
  explicitly disabled. All nine public headers, the pre-1.0 package-version
  rule, documentation, dependency floors, SBOMs, and the exact 39-of-39 shared-
  export surface passed. Full evidence is retained in
  `docs/quality-runs/2026-09-14-linux-amd64-81b2691.json`; the viewer was not
  built and contributes nothing to this result.

- Exact development commit
  `6edba5fd7fe2b12fbf30a598b7ff1b7615014ce3` produced a deterministic
  316,685-byte source archive with SHA-256
  `f72528dd4456b7d3a2dce95ae15d79a72af6c7118d3073f06fdcaa8534ad0d90`.
  Its extracted-source Linux amd64 production-dependency gate passed static
  14/14 and shared 15/15, direct and relocated installed consumers 1/1, and
  embedded consumers 1/1 for both linkage forms. Shared consumers configured
  with discovery of Expat, LZ4, OpenSSL, zlib, and Zstandard explicitly
  disabled, proving those private implementation targets do not cross the
  shared package's build interface. All nine public headers, the pre-1.0
  package-version rule, documentation, dependency floors, SBOMs, and the exact
  39-of-39 shared-export surface also passed. Full evidence is retained in
  `docs/quality-runs/2026-09-14-linux-amd64-6edba5f.json`; the viewer was not
  built and contributes nothing to this result.

- Exact development commit
  `90771c20ee81909701fdc7a24bf08ce79663e8a0` produced a deterministic
  312,291-byte source archive with SHA-256
  `bbf66d474614ada352a999534ac4cb4cad0732b0f17d8e1b5a6d6eea22081d57`.
  Its extracted-source Linux amd64 production-dependency gate passed static
  13/13 and shared 14/14, installed and embedded consumers 1/1 for both
  linkage forms, and independent compilation of all nine public headers in
  each consumer mode. Documentation, dependency floors, SBOMs, and the exact
  39-of-39 shared-export surface also passed. Full evidence is retained in
  `docs/quality-runs/2026-09-14-linux-amd64-90771c2.json`; the viewer was not
  built and contributes nothing to this result.

- PFI commit `013c482725d9c243878972e97c55fa8da9889ee1` routes current
  Batch backend construction through a fail-closed product seam. The host
  remains the zero-configuration default; mmxisf requires a present provider,
  qualification, and explicit authorization; rejected or incomplete providers
  cannot trigger an automatic host fallback; and explicit rollback has distinct
  provenance. The focused PFI source/build suite passed 21/21 and its matching
  native C++ policy passed 5/5 locally. This is `LIMITED`: no native provider,
  PCL bridge, UI preference, granted qualification, or scientific/operator
  acceptance exists.
- PFI then advanced the limited seam through commit
  `7793cd19ced437b2f045453338bca875bfaa6169`. The source-built PJSR provider
  validates the create-only Float32 Gray/RGB transport and populates a temporary
  `ImageWindow`; the macOS-arm64 packaging target copies five non-system dylibs,
  rewrites them to bundle-relative `@rpath`, verifies arm64 identity, and
  ad-hoc signs each copied Mach-O object. A fresh warnings-as-errors build
  against the archive-rehearsed `mmxisf` package passed 8/8 CTest entries. The
  exact 447,776-byte provider has SHA-256
  `1c81854a2f977cd8c375bff02f829bb683e321fc5442bfad5cd8f1fec54861c1`.
  A source-bound manual PixInsight gate has also been generated locally; its
  builder/tamper contracts pass, but the PJSR lifecycle itself remains
  **NOT_TESTED**. The bundle is local, ad-hoc signed, unpublished, unconfigured,
  and unqualified, so `pfi.product-input-wiring` remains `LIMITED`.
- PFI commit `dd0e88b18faaf96ef8161a476b8251ea1c85cec5` removes the
  selected provider's remaining host-metadata preflight dependency. A separate
  create-only manifest enumerates the container, projects ordered metadata, and
  decodes only the three bounded PFI WCS blocks without reading image pixels;
  PJSR caps the manifest at 16 MiB and rechecks source identity. The test proves
  that metadata inspection succeeds on a fixture whose invalid declared pixel
  checksum makes the later image decode fail. The exact 465,856-byte updated
  provider has SHA-256
  `f01ef8fe5f0a1872d6e0fcd7fe74d290519766e82974274eddd5636c7dfb0559`
  and passed the fresh warnings-as-errors 8/8 gate. Native execution and product
  qualification remain open, so the ledger status does not change.
- PFI commit `43b7ca190d2dd946c2be3925a73d90ac5f5a5574` extends the
  manual provider gate beyond byte identity. It binds the exact current
  detector, Moffat4 fitter, filtering, field-model, and reliability sources,
  runs the provider and host windows through both, and predeclares exact
  canonical-result equality after excluding only `sourceViewId`. The ignored
  125,895-byte generated script has SHA-256
  `6cf567db81344aa22827dfd6987ba1e692864abceec40e1e5a572e66236ef9a3`.
  Generation and tamper contracts pass, but native execution is pending;
  `pfi.detection-fitting-scientific-parity` therefore remains `NOT_TESTED`.
- PFI commit `1452d1d5b0b11d6f985c0a1fa75fc63b73e2f7cc` adds the
  create-only production-baseline provider gate. Against clean `mmxisf` commit
  `405aba83ba5292dd3ff0e8bf46728c4edf317c1a` and pinned vcpkg registry commit
  `a1cae005c39be7b18ba319fced856b68d7276271`, the static build passed `mmxisf`
  12/12 and PFI native 8/8 CTest entries with warnings-as-errors. The
  5,705,392-byte provider has SHA-256
  `90e5a167c0de68c4c8fa7289a77bda463f54ce3734bdf1c39b5e159d9cac6aba`,
  links only macOS system runtimes, and bundles no non-system dylibs. Its
  manifest binds both clean source commits and the exact SPDX 2.3 binary SBOM,
  and the PFI generator independently rehashes all runtime inputs before a
  native candidate can be produced. A representative private Float32 Gray file
  completed both metadata-only and full-pixel transports. The generated
  PixInsight candidate remains **NOT_TESTED**, so product wiring stays
  `LIMITED` and qualification remains ungranted.
- Exact commit `75b1cc588254d01f801efe4de597a9b66a1347f1` produced the
  deterministic unpublished source archive
  `mmxisf-0.1.0-source-75b1cc588254.tar.gz` twice byte-identically. The
  270,930-byte archive has SHA-256
  `f0523b278ba3a3970acbdaa4027da9864e082a9c2593c9011bf0d25cb0fd03fe`.
  A clean-room macOS arm64 Release build extracted only from that archive
  passed `-Wall -Wextra -Wpedantic -Werror`, 11/11 library tests, installation,
  and the separately configured installed-package consumer 1/1. The manifest
  remained `PREPARED_NOT_PUBLISHED` with `publicationAuthorized: false`; this
  rehearsal neither freezes a candidate nor validates Linux/Windows or final
  release assets.
- The current source-archive rehearsal supersedes that package checkpoint at
  exact commit `ba360cc53b97bcdff3e198f576bfe82bca5d5a44`. Two byte-identical
  281,410-byte archives have SHA-256
  `ed2dceaaa4536b0bda8694b4be17ef9d7f50d193b8295fd95c6456db41796558`.
  A build using only the extracted archive and the pinned production vcpkg
  graph passed warnings-as-errors, 12/12 tests, install, installed consumer
  1/1, binary SBOM validation, system-only viewer linkage, and strict ad-hoc
  signature verification. The exact evidence is retained in
  `docs/security-audits/2026-09-14-source-archive-vcpkg-macos-arm64.json`.
  This is still an unpublished macOS rehearsal, not a frozen candidate or
  Linux/Windows release-artifact result.
- The pre-1.0 public API, error, ownership, resource, cancellation, concurrency,
  dependency-boundary, and shared-export review passed for exact commit
  `85b94f4deb1ec59ac996ce72b2e4b4fc338bcb7f`. Its complete local macOS gate
  passed warnings-as-errors static/shared 11/11, installed consumers 1/1,
  ASan/UBSan 11/11 plus 20,000 mutations, ThreadSanitizer 11/11, API docs, and
  strict deep bundle signature verification. The audit is retained in
  `docs/PUBLIC_API_AUDIT.md`; an exact frozen-candidate diff review remains
  **NOT_TESTED**, and no pre-1.0 ABI promise was added.
- Exact commit `3724fc02e772a90260c98f942c1e99c138754575` passed the complete
  maintained local macOS gate after adding the readiness ledger: warnings-as-
  errors static/shared suites 10/10 each, installed-package consumers 1/1 each,
  ASan/UBSan 10/10 plus the deterministic 20,000-case mutation smoke,
  ThreadSanitizer 10/10, generated API documentation, and strict deep viewer-
  bundle signature verification. The two added contracts validate the current
  ledger and prove rejection of a false `READY` claim, malformed frozen commit,
  and unknown gate status. This is macOS arm64 evidence; the exact-candidate
  Linux/Windows and external gates remain open.
- Exact commit `d5a81ed566d02b1222e9e2baf51ecf2faffbef24` passed the complete
  maintained local macOS gate: warning-as-error static/shared suites 8/8 each,
  installed-package consumers 1/1 each, ASan/UBSan 8/8 plus the deterministic
  20,000-case mutation smoke, ThreadSanitizer 8/8, generated API documentation,
  and strict deep viewer-bundle signature verification. The same commit adds a
  checked, independently decoded native-writer property fixture whose exact
  compiled bytes validate as a 2x2 UInt16 Gray image with a compressed,
  shuffled, SHA-256-protected F64 matrix property, a UI16 vector property, and
  a string property. The source-bound PixInsight validation script and fixture
  are prepared, but native PixInsight execution remains **NOT_TESTED** and is
  not implied by this local gate.
- Exact commit `88f12a7611c4897e82238b734648b32306d2b74d` passed the complete
  maintained local macOS gate: warning-as-error static/shared suites 8/8 each,
  installed-package consumers 1/1 each, ASan/UBSan 8/8 plus the deterministic
  20,000-case mutation smoke, ThreadSanitizer 8/8, generated API documentation,
  and strict deep viewer-bundle signature verification. The reader contract
  includes eight threads performing 256 owning reads and 256 bounded row reads
  through one file-backed `Reader`; the writer contract retains sixteen
  simultaneous attempts at one no-overwrite destination. No sanitizer finding
  occurred. This is macOS arm64 evidence, not Linux/Windows substitution.
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
- A separate matching-toolchain Linux amd64 campaign on `mllse` used the pinned
  Ubuntu 24.04 Docker environment and only the deterministic extracted archive
  for exact commit `50fc120391e7f156c069bfdd13e41e4942be2ccf`. Clang 18
  ASan/UBSan executed 2,378,414 inputs in 901 seconds, ending at `cov: 15638`,
  `ft: 43269`, 853 effective corpus units/1,190 KiB, and 487 MiB peak RSS with
  no crash artifact, timeout, or sanitizer finding. The full log and 865-file
  evolved corpus remain on the private server with identities retained in
  `docs/fuzz-campaigns/2026-09-14-linux-amd64-50fc120.json`. The external LLVM
  symbolizer was absent, so a hypothetical failure would have required offline
  stack symbolization. This is `PASS_DEVELOPMENT_NOT_FROZEN`; the dependency
  graph is below the production floor and the exact-candidate gate remains open.
- The versioned long-campaign runner subsequently passed from a deterministic
  source archive for exact commit
  `ed594d2583ff15df0cd4d2bc34bf2d2e36569cdf` on the pinned Ubuntu 24.04 amd64
  Docker environment. Matching Clang 18 ASan/UBSan/libFuzzer with an available
  LLVM 18 symbolizer executed 2,504,364 inputs in 901 seconds, ending at
  `cov: 15698`, `ft: 43613`, 831 effective corpus units/1,090 KiB, and 482 MiB
  peak RSS without a crash, timeout, or sanitizer finding. Exact archive,
  binary, log, corpus, container, and limit evidence is retained in
  `docs/fuzz-campaigns/2026-09-14-linux-amd64-ed594d2.json`. This remains
  `PASS_DEVELOPMENT_NOT_FROZEN`; it does not satisfy the frozen-candidate gate.
- The maintained `tools/run_local_quality_gates.sh` completed on exact commit
  `33da19ec13a2e045944012088a76e6f2ca789d5b`: warning-as-error static and
  shared suites 8/8 each, installed-package consumers 1/1 each, ASan/UBSan
  suite 8/8, deterministic 20,000-case mutation smoke, generated API reference,
  and a freshly built macOS bundle with strict deep code-signature verification
  all passed. This is the reproducible routine macOS gate, not a cross-platform
  substitute.
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
  `docs/SUPPLY_CHAIN_AUDIT.md` as a conditional source-only pass. The dated
  legacy-host macOS arm64 binary review is retained in
  `docs/security-audits/2026-09-14-macos-arm64.md` and remains **BLOCKED /
  UPGRADE REQUIRED** for that old graph: Expat 2.5.0 predates security fixes,
  OpenSSL 3.2.0 is end-of-life, and zlib, LZ4, and Zstandard are below the
  conservative production floors. An opt-in configure gate rejects those
  versions for candidate builds;
  a checked vcpkg manifest pins official registry commit
  `a1cae005c39be7b18ba319fced856b68d7276271` with all five ports at or above
  the corrected Expat 2.8.4 floor. Exact commit
  `269d676aecb459b6d3a09284de7c44e38e0c18b2` then passed a fresh isolated
  macOS arm64 warnings-as-errors build, 12/12 tests, install, installed consumer
  1/1, exact binary SBOM, system-only viewer linkage, and strict ad-hoc bundle
  signature verification. The public security intake, Linux/Windows pinned-
  graph builds, notarization, and a fresh frozen-candidate review remain open.
- Exact commit `405aba83ba5292dd3ff0e8bf46728c4edf317c1a` then passed the
  complete maintained local macOS gate after exporting the installed binary
  SBOM as part of the CMake package contract: static/shared 12/12 each,
  installed consumers 1/1 each, ASan/UBSan 12/12 plus 20,000 mutations,
  ThreadSanitizer 12/12, generated API docs, and strict deep viewer signature.
  The separate pinned-static vcpkg build also passed 12/12 plus install and its
  installed consumer 1/1. This is current local evidence, not frozen-candidate
  Linux/Windows evidence.
- Exact commit `50fc120391e7f156c069bfdd13e41e4942be2ccf` passed the
  reproducible local Linux amd64 Docker gate using only its deterministic
  extracted source archive. On Ubuntu 24.04, GCC 13 warnings-as-errors static
  and shared builds passed 12/12 each and both installed consumers passed 1/1;
  Clang 18 ASan/UBSan passed 12/12 plus 20,000 deterministic mutations,
  ThreadSanitizer passed 12/12, and the generated API documentation passed.
  The pinned base image, toolchain, exact system packages, source archive and
  static/shared binary-SBOM hashes are retained in
  `docs/security-audits/2026-09-14-docker-ubuntu-24.04-amd64.json`. The Ubuntu
  system packages are intentionally recorded as compatibility evidence only:
  they do not satisfy the separate conservative production dependency floor,
  and this is not Windows or frozen-candidate evidence.
- The same extracted source at `50fc120391e7f156c069bfdd13e41e4942be2ccf`
  then passed a Linux amd64 production-dependency gate using the exact official
  vcpkg baseline `a1cae005c39be7b18ba319fced856b68d7276271`.
  The resolved graph is Expat 2.8.4, zlib 1.3.2#2, LZ4 1.10.0,
  Zstandard 1.5.7, and OpenSSL 3.6.4. GCC 13 warnings-as-errors static/shared
  suites passed 12/12 each, both installed consumers passed 1/1, dependency
  floors and generated documentation passed, and the shared library linked
  dynamically only to the standard Linux runtime. Exact binary/SBOM identities
  are retained in
  `docs/security-audits/2026-09-14-vcpkg-linux-amd64.json`. This remains local
  development evidence; Windows, a frozen candidate, and the final advisory
  review are still open.
- The versioned Docker and vcpkg wrappers were then replayed from the
  deterministic extracted archive for exact current development commit
  `392ebd328858696b86c3e02a4e7cb9b57f956297`. The complete system-dependency
  quality gate again passed static/shared 12/12, consumers 1/1, ASan/UBSan plus
  20,000 mutations, TSan 12/12, and documentation. The production vcpkg gate
  again passed static/shared 12/12, consumers 1/1, dependency floors, SBOM, and
  documentation checks. The rebuilt image now includes an explicit matching
  LLVM symbolizer. Source archive, image, library, and SBOM identities are
  retained in `docs/security-audits/2026-09-14-linux-amd64-392ebd3.json`.
  This supersedes the local Linux development checkpoint, but does not claim a
  frozen candidate or Windows result.
- Exact development commit `8c33a6526efa7ee968d26c40faedd7ede1e383d8`
  then exercised the persistent vcpkg binary-cache path from its deterministic
  extracted source archive. An isolated cache-population tree and a second
  cache-restoration tree each passed GCC 13 warnings-as-errors static/shared
  12/12 suites and installed-package consumers 1/1. The restore consumed all
  eight cached packages, and the resulting static and shared libraries were
  byte-identical to their population-build counterparts. The shared object
  dynamically links only the standard Linux runtime. Exact archive, image,
  cache, library, SBOM, and retained-log identities are recorded in
  `docs/security-audits/2026-09-14-vcpkg-cache-linux-amd64-8c33a65.json`.
  This is the newest Linux development checkpoint; it is not a frozen
  candidate, Windows evidence, or a final vulnerability audit.
- The exact five direct dependencies embedded in the later macOS arm64 PFI
  provider snapshot for `mmxisf` commit
  `3fd55e6a0ad99a645388ef842fe040b996756a91` passed a dated official-upstream
  advisory review. Expat 2.8.4 and OpenSSL 3.6.4 include the reviewed fixes;
  zlib 1.3.2, LZ4 1.10.0, and Zstandard 1.5.7 match the current upstream release
  pages. An open report against deprecated unsafe `LZ4_decompress_fast` was
  screened against the exact source: `mmxisf` uses `LZ4_decompress_safe` and
  does not call the reported API. The exact provider and SBOM hashes, sources,
  dispositions, and limitations are retained in
  `docs/security-audits/2026-09-14-development-binary-advisory-review-3fd55e6.json`.
  This is a passing unfrozen development snapshot, not the mandatory final
  frozen-candidate binary and complete-toolchain audit.
- Exact development commit `1489bc13f07a4db0b0077a9dc9b4abd62ea293ef`
  then passed an extracted-source Linux amd64 production-graph replay on
  `mllse`: static/shared suites 12/12, installed consumers 1/1, dependency
  floors, SBOM, and generated documentation. Five deterministic 6064x4040
  UInt8 RGB writer and reader runs added a second-host performance profile.
  Median writer throughput was 138.357 MiB/s, owning decode plus SHA-256 was
  250.231 MiB/s, and row decode plus SHA-256 was 204.294 MiB/s. Row delivery
  reduced maximum RSS from 148,111,360 to 53,428,224 bytes. Exact inputs,
  per-run values, toolchain, binary identities, and retained-log hashes are in
  `docs/performance-runs/2026-09-14-linux-amd64-1489bc1.json`. This advances the
  supported-host review to **LIMITED**; the profile is unfrozen and Windows
  remains unmeasured, so no portable SLA or candidate claim is made.
- Exact clean commit `53c4845857eaeed7bfd610c7f41a170862a633e1`
  produced an updated macOS arm64 viewer using the retained production vcpkg
  graph. The sealed app now includes `LICENSE`, `NOTICE`, `SECURITY.md`,
  `CONTRIBUTING.md`, `THIRD_PARTY_NOTICES.md`, and its exact binary SPDX SBOM.
  The warnings-as-errors build and 13/13 suite passed, including a new
  fail-closed bundle-resource/SBOM/signature contract. The 2,671,596-byte ZIP
  has SHA-256
  `2c126034b3d64272474311c941924a1f21c41ab2f83ddc3db479f605d4c6288a`;
  after extraction the deep signature and exact executable/SBOM hashes passed.
  This materially hardens the requested runnable PoC, but it remains ad-hoc
  signed, unnotarized, unlaunched in this gate, unpublished, and not a frozen
  candidate. The viewer is an optional add-on; its signing or notarization does
  not block the standalone C++ library's production-readiness claim.
- Exact development commit `aac94f80ac54c198ad3918840bf3a41186d3957f`
  separated all viewer source and preview targets from the default library
  build, installed the machine-bound standalone support profile with the CMake
  package, and passed the complete local macOS arm64 quality gate. The explicit
  viewer configuration passed 15/15, while viewer-free shared, ASan/UBSan, and
  ThreadSanitizer configurations each passed 13/13; installed static/shared
  consumers passed 1/1, the mutation smoke completed 20,000 cases, and API
  documentation generation passed. Exact host, toolchain, resolved development
  dependencies, library/SBOM/profile hashes, and limitations are retained in
  `docs/quality-runs/2026-09-14-macos-arm64-aac94f8.json`. The system dependency
  graph is not promoted to production-baseline evidence, and the ancillary
  viewer result does not contribute to the library claim.
- Exact development commit `436a9b443bab67a043842dcbda9168e502cf3c56`
  passed the deterministic extracted-source Linux amd64 production-dependency
  gate on `mllse`: warnings-as-errors static 13/13, shared 14/14, installed
  consumers 1/1 each, dependency floors, SBOM, and API documentation. The new
  shared-export contract reports exactly 39 `mmxisf` symbols out of 39 dynamic
  exports. This corrects the 8,239-symbol dependency leak detected at `5ee4646`
  and the remaining 18 weak standard-library exports detected at `f5cf404`;
  those failed checks remain diagnosis rather than PASS evidence. Exact source,
  environment, artifact, and defect-history evidence is retained in
  `docs/quality-runs/2026-09-14-linux-amd64-436a9b4.json`. The viewer was not
  built and does not contribute to this standalone-library gate. Windows and a
  frozen exact candidate remain open.
- Exact development commit `69a7037a858cf9c2056e37fbb2047a7f413c6ddb`
  made the product boundary executable in both local and hosted test
  orchestration. The default local gate and all four Unix entries of the manual
  CI matrix now build the reusable C++ library with the viewer disabled. The
  exact clean macOS arm64 run passed static 13/13, shared 14/14, installed
  consumers 1/1 each, ASan/UBSan 13/13 plus 20,000 mutations, TSan 13/13, and
  API documentation. A separately opted-in viewer build passed 15/15 plus deep
  signature verification, including a repeated incremental build after fixing
  SBOM resealing. Its result remains ancillary. Exact evidence is retained in
  `docs/quality-runs/2026-09-14-macos-arm64-69a7037.json`; the manual CI workflow
  was not dispatched.
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
- Rebuild all five dependencies in a controlled supported environment at or
  above the dated production floors, enable the fail-closed baseline option,
  and perform a fresh exact-version audit before any public tag.
