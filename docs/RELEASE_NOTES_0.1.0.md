# mmxisf 0.1.0 release notes

`v0.1.0` is a source-only release of the standalone, reusable C++20 `mmxisf`
library. It distributes no platform binaries, no viewer, and no PFI artifact.
It supersedes `v0.1.0-rc.1`, which covered only the document-1.00 profile
(`docs/RELEASE_NOTES_0.1.0_RC1.md`, historical).

## Commit model (ADR 0021)

- **Candidate X** is commit `d17605686768832226da55090a33b28888bfab35`, the
  frozen candidate `v0.1.0-rc.2`. Its deterministic source archive
  `mmxisf-0.1.0-source-d17605686768.tar.gz` (399,408 bytes) has SHA-256
  `11a215f24da98ec1cc012fd8b8fefde8f8be6264fc520d80fa7a1c92f2837c66`.
- **Final Y** is the commit tagged `v0.1.0`. It is X plus documentation-only
  evidence commits; every source, header, build, test, tool, CI, container,
  example, viewer, SBOM and installed-package input is byte-identical to X.
- All exact-commit gates are re-run on Y before `v0.1.0` is published. A tagged tree cannot contain its own
  commit identifier or its own re-run evidence, so Y's commit hash, Y's source
  archive name and SHA-256, and Y's re-run evidence are published as release
  assets (`SHA256SUMS`, `source-candidate.json`, and the Y gate-evidence bundle)
  and in these release notes on the GitHub Release page, not in this file.

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

Pinned vcpkg graph (baseline `a1cae005c39be7b18ba319fced856b68d7276271`):
Expat 2.8.4, zlib 1.3.2#2, LZ4 1.10.0, Zstandard 1.5.7, OpenSSL 3.6.4. On
2026-10-06 the owner accepted OpenSSL 3.6.4 for this release with an API-scoped
disposition: mmxisf uses only OpenSSL message-digest primitives (`EVP_Digest*`
with SHA-1, SHA-256, SHA-512, SHA3-256, SHA3-512), and the official OpenSSL
vulnerability list (https://openssl-library.org/news/vulnerabilities/) names no
3.6.4 or 3.6.5 issue in those primitives. OpenSSL 3.6.5 (2026-09-29) fixes one
High DTLS issue (CVE-2026-84782) and further Low/Moderate issues in DTLS, QUIC,
CMP, X.509, EC and SM2 code that mmxisf does not call. An OpenSSL bump is
scheduled for the next mmxisf version. See
`docs/security-audits/2026-10-06-rc2-advisory-review.json`.

## Qualification of candidate X (recorded in this tree)

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
- Hosted CI matrix and native Windows/MSVC for X: see the ledger and
  `docs/quality-runs/` for the recorded result; the status of every gate is the
  ledger's, not this file's.

## Known limitations

- The support profile is bounded and includes explicit INSPECT_ONLY and
  REJECT_EXPLICITLY rows; this is not a universal XISF implementation claim.
- The API is pre-1.0 and carries no ABI promise.
- The row API does not support globally shuffled multi-subblock images.
- The independent `xisf` 0.9.7 package cannot decode the Revision 1
  multi-subblock fixture; native PixInsight acceptance of that writer path is
  recorded in `docs/quality-runs/2026-09-21-pixinsight-revision1-subblocks-macos-arm64.json`.
- The native MSVC static library may not be byte-identical across build
  directories (documented non-blocking limitation, source-only release).
- The optional macOS viewer remains a local PoC and is not a release asset.
- PFI operator acceptance and rollback exercise are separate product gates and
  do not contribute to the standalone-library release claim.
