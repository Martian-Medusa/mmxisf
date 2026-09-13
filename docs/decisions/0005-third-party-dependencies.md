# ADR 0005: private and replaceable third-party dependencies

- Status: Proposed; freeze after M1/M2 spikes
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

## Consequences

Packaging work is higher than embedding convenience copies, but consumers keep
a clean API and can audit their dependency graph. M1 must verify Expat's strict
handler behavior and Windows/macOS packaging. M2 must benchmark codec and EVP
integration in static and shared builds before this ADR becomes Accepted.
