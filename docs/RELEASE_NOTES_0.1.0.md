# mmxisf 0.1.0 release notes

`v0.1.0` is a source-only release of the standalone, reusable C++20 `mmxisf`
library. It distributes no platform binaries, no viewer, and no PFI artifact.
It supersedes `v0.1.0-rc.1`, which covered only the document-1.00 profile
(`docs/RELEASE_NOTES_0.1.0_RC1.md`, historical).

## Commit model (ADR 0021)

- **Candidate X** is the commit tagged `v0.1.0-rc.3`. An earlier candidate,
  `v0.1.0-rc.2`, passed its gates but was superseded before tagging because
  Expat 2.8.5 and OpenSSL 3.6.5 required new dependency pins.
- **Final Y** is the commit tagged `v0.1.0`. It is X plus documentation-only
  evidence commits; every source, header, build, test, tool, CI, container,
  example, viewer, SBOM and installed-package input is byte-identical to X.
- All exact-commit gates are re-run on Y before `v0.1.0` is published. A tagged
  tree cannot contain its own commit identifier or its own re-run evidence, so
  the commit hashes of X and Y, the source archive names and SHA-256 values, and
  Y's re-run evidence are published as release assets (`SHA256SUMS`,
  `source-candidate.json`, and the Y gate-evidence bundle) and in the GitHub
  Release notes, not in this file.

## Readiness claim

`v0.1.0` is released as a **standalone beta**: the machine-checked ledger
(`docs/production-readiness.json`) derives `standaloneBeta` READY from the
required gates. `standaloneProduction` deliberately remains NOT_READY because
`performance.supported-host-review` is accepted as LIMITED (owner decision
2026-10-06): the low-copy row reader rejects globally shuffled multi-subblock
images by documented Revision 1 policy, so no row-reader timing exists, and
Linux and Windows writer throughput is about 27-35% below the v0.1.0-rc.1-era
checkpoints (not root-caused; investigation scheduled for the next mmxisf
version). `pfiProduction` is NOT_READY by design (operator acceptance and
rollback are separate product gates).

## Library scope

- Bounded XISF 1.0 (Revision 1) parsing and image decoding for the profiles
  classified in `docs/support-profile-0.1.0.json` (59 REQUIRED, 12 INSPECT_ONLY,
  7 REJECT_EXPLICITLY rows).
- Mono and RGB image data, supported integer/floating sample formats, declared
  compression/shuffle/checksum combinations, structured metadata, Properties,
  tables, ICC profiles, thumbnails.
- Deterministic XISF writing for the declared writer profiles, including
  caller-owned sequential byte sinks.
- Static and shared CMake package consumption, including embedding with
  `add_subdirectory`.
- Explicit resource budgets, cooperative cancellation, stable error categories,
  and no third-party implementation types in installed headers.

## Changes since v0.1.0-rc.1

- XISF 1.0 Revision 1 conformance: the complete logical block is byte-shuffled
  before division into compression subblocks (reader global-offset scatter and
  writer mapping), and Image identifier syntax is validated by the writer.
- Behavior behind unchanged public signatures: the low-copy row API returns
  `unsupported_feature` before the first callback for byte-shuffled images with
  more than one compression subblock; `read_image` and `read_image_into`
  support them. Consumers of rc.1 that used row delivery on such images must
  use the owning or caller-buffer reads.
- No installed public header changed from the audited nine-header baseline or
  from rc.1.
- Development-harness fixes: the writer benchmark Image id and the native
  Windows performance-checkpoint step (documented row-API rejection).

## Compatibility policy

The API is pre-1.0. Source compatibility follows Semantic Versioning, but **no
ABI compatibility is promised before 1.0**; consumers must rebuild against the
selected version. No C ABI, exception-disabled profile, or stable binary plugin
boundary is claimed.

## Dependencies and security

Pinned vcpkg graph (baseline `e182cb4dd2df2ab02f66a1aabd5f35bbdc9522c7`):
Expat 2.8.5, zlib 1.3.2#2, LZ4 1.10.0, Zstandard 1.5.7, OpenSSL 3.6.5. Expat
2.8.5 (2026-09-22) fixes CVE-2026-93990 (UTF-16 surrogate validation) and
OpenSSL 3.6.5 (2026-09-29) is a security patch release. mmxisf additionally
rejects every header that does not begin with an ASCII XML declaration, forces
the parser to UTF-8 and rejects non-UTF-8 declarations; regression tests prove
that UTF-16 and surrogate-malformed headers are rejected. The earlier rc.2
candidate pinned Expat 2.8.4 and OpenSSL 3.6.4 and is superseded. See
`docs/security-audits/2026-10-06-rc3-dependency-update.json`.

## Qualification of candidate X (historical rc.2 records in this tree; rc.3 evidence is published as release assets and recorded after the freeze)

- Linux amd64 (GCC 13 pinned-vcpkg production graph and system graph, Clang
  ASan/UBSan, 20,000-case mutation smoke, TSan, byte-identical repeat builds)
  and macOS arm64 (the same gate families with AppleClang): PASS. Evidence:
  `docs/quality-runs/2026-10-06-rc2-linux-macos-d176056.json`.
- 900-second Linux libFuzzer ASan/UBSan campaign: 2,344,549 executions, no
  crash, sanitizer finding, or timeout. Evidence:
  `docs/fuzz-campaigns/2026-10-06-linux-amd64-v0.1.0-rc.2.json`.
- Independent PyPI `xisf` 0.9.7 consumer replay: PASS with the documented
  limitation that it cannot read the Revision 1 multi-subblock fixture.
  Evidence: `docs/quality-runs/2026-10-06-rc2-independent-consumer-replay-macos-arm64.json`.
- Hosted CI matrix (ubuntu-latest and macos-15 static/shared, fuzz-smoke,
  source-package, api-reference) and native Windows Server 2025 amd64/MSVC
  (warnings-as-errors static 19/19 and shared 20/20, consumers, dependency
  floors, 39-symbol export surface): PASS, run 37461708502. Evidence:
  `docs/quality-runs/2026-10-06-rc2-windows-amd64-msvc.json` and
  `docs/quality-runs/2026-10-06-rc2-cross-platform.json`.
- The status of every gate is the ledger's (`docs/production-readiness.json`),
  not this file's.

## Known limitations

- The support profile is bounded and includes explicit INSPECT_ONLY and
  REJECT_EXPLICITLY rows; this is not a universal XISF implementation claim.
- The API is pre-1.0 and carries no ABI promise.
- The row API does not support globally shuffled multi-subblock images, so no row-reader throughput is reported for the benchmark profile; writer throughput on Linux and Windows is lower than the rc.1-era checkpoints (not root-caused).
- The independent `xisf` 0.9.7 package cannot decode the Revision 1
  multi-subblock fixture; native PixInsight acceptance of that writer path is
  recorded in `docs/quality-runs/2026-09-21-pixinsight-revision1-subblocks-macos-arm64.json`.
- The native MSVC static library is not byte-identical across build
  directories (documented non-blocking limitation, source-only release).
- The optional macOS viewer remains a local PoC and is not a release asset.
- PFI operator acceptance and rollback exercise are separate product gates and
  do not contribute to the standalone-library release claim.
