# ADR 0005: private and replaceable third-party dependencies

- Status: Accepted for the pre-1.0 read path
- Date: 2026-09-13

## Context

Baseline XISF decoding requires strict XML 1.0 processing, zlib, LZ4/LZ4HC,
byte shuffling, and SHA-1 checksum compatibility. PFI and public consumers need
predictable cross-platform packaging without third-party types in the ABI.

## Decision

Use private adapters with the following leading candidates:

| Capability | Candidate | Reason |
| --- | --- | --- |
| XML reader | Expat | strict SAX-style parsing, bounded domain construction |
| zlib codec | zlib | normative/recommended implementation family |
| LZ4/LZ4HC decoder | LZ4 | normative/recommended implementation family |
| checksums | OpenSSL EVP | avoid implementing cryptographic primitives; covers required and stronger algorithms |

Dependency policy:

- system/package-manager dependencies for development and distribution;
- no automatic network downloads during a normal CMake configure;
- an opt-in reproducible dependency bootstrap may be added with pinned versions
  and hashes;
- dependencies link privately unless their license or static-link requirements
  require documented propagation;
- CI and releases record exact versions, licenses, hashes and an SBOM;
- version minimums are selected from tested security-supported releases during
  the spike, not guessed in this ADR.

The selected dependency families have passed the library, installed-package,
and application-bundle gates locally, plus Linux, macOS, and Windows CI in run
`34775853605`. This accepts the dependency and encapsulation policy, not a
permanent version floor. Exact versions, hashes, licenses, and SBOM data remain
release-artifact records and must be refreshed for each publication candidate.

## Consequences

Packaging work is higher than embedding convenience copies, but consumers keep
a clean API and can audit their dependency graph. The development and CI gates
verify Expat's strict handler behavior, all selected codec/EVP integrations,
Windows package consumption, and macOS bundle relocation. Representative-image
performance measurements remain an M3 acceptance gate, not a reason to keep
the dependency-family decision provisional.
