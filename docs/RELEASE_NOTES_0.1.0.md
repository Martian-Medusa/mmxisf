# mmxisf 0.1.0 release notes (candidate record)

**Status: candidate record, not published.** Per
`docs/decisions/0021-final-release-is-the-frozen-candidate-commit.md`, the final
`v0.1.0` release is the exact commit of the frozen candidate `v0.1.0-rc.2`,
created only after every required gate passes on that commit and after explicit
owner approval for each external write. `docs/production-readiness.json` is the
authority for what has passed; this file must be finalized (qualification
section and native Windows result) before any publication.

`v0.1.0` is a source-only release of the standalone, reusable C++20 `mmxisf`
library. It distributes no platform binaries, no viewer, and no PFI artifact.
It supersedes `v0.1.0-rc.1`, which covered only the document-1.00 profile
(`docs/RELEASE_NOTES_0.1.0_RC1.md`, historical).

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
- Behavior behind unchanged public signatures: the low-copy row API
  (`Reader` row delivery) returns `unsupported_feature` before the first
  callback for byte-shuffled images with more than one compression subblock;
  `read_image` and `read_image_into` support them. Consumers of rc.1 that used
  row delivery on such images must use the owning or caller-buffer reads.
- No installed public header changed from the audited nine-header baseline or
  from rc.1.
- Development-harness fixes: the writer benchmark Image id and the native
  Windows performance-checkpoint step (documented row-API rejection).

## Compatibility policy

The API is pre-1.0. Source compatibility follows Semantic Versioning, but **no
ABI compatibility is promised before 1.0**; consumers must rebuild against the
selected version. No C ABI, exception-disabled profile, or stable binary plugin
boundary is claimed.

## Candidate identity

- Repository: `Martian-Medusa/mmxisf`
- Candidate ref: annotated `v0.1.0-rc.2` (not yet created); final ref
  annotated `v0.1.0` at the identical commit (not yet created)
- Commit: `d17605686768832226da55090a33b28888bfab35`
- Deterministic source archive: `mmxisf-0.1.0-source-d17605686768.tar.gz`,
  399,408 bytes
- Archive SHA-256:
  `11a215f24da98ec1cc012fd8b8fefde8f8be6264fc520d80fa7a1c92f2837c66`
- Support profile SHA-256:
  `748cd8e60d20bcab01aad945102d346980859500b1b63a2d4efae954b89711f7`

Release assets (planned): the deterministic archive, its SHA-256 sidecar, the
source-candidate identity manifest, and the source-dependency SPDX 2.3 SBOM.
The manifest's `PREPARED_NOT_PUBLISHED` state records the fail-closed state in
which the archive was generated; publication authority is granted separately by
the repository owner.

## Qualification state on the frozen commit (as of 2026-10-06)

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
- Native Windows/MSVC, hosted CI matrix, binary-SBOM advisory review for the
  final scope, and release-artifact verification: **not yet recorded**.

## Known limitations

- The support profile is bounded and includes explicit INSPECT_ONLY and
  REJECT_EXPLICITLY rows; this is not a universal XISF implementation claim.
- The API is pre-1.0 and carries no ABI promise.
- The row API does not support globally shuffled multi-subblock images.
- The independent `xisf` 0.9.7 package cannot decode the Revision 1
  multi-subblock fixture; native PixInsight acceptance of that writer path is
  recorded in `docs/quality-runs/2026-09-21-pixinsight-revision1-subblocks-macos-arm64.json`.
- The optional macOS viewer remains a local PoC and is not a release asset.
- PFI operator acceptance and rollback exercise are separate product gates and
  do not contribute to the standalone-library release claim.
