# ADR 0011: SHA-3 block checksum support

- Status: Accepted for pre-1.0 implementation
- Date: 2026-09-14
- SemVer impact: additive source API; ABI-changing before 1.0

## Context

The pinned XISF 1.0 checksum profile includes SHA3-256 and SHA3-512. The reader
previously recognized those algorithm names but rejected decoding, while the
writer exposed only SHA-1, SHA-256, and SHA-512. OpenSSL 3 is already a required
private dependency and provides both SHA-3 digests, so leaving the standard
algorithms inspect-only no longer reduces the dependency or attack surface.

## Decision

The reader verifies lowercase `sha3-256` and `sha3-512` descriptors over the
exact serialized block bytes before decompression. Digest length and lowercase
hexadecimal rules remain identical to the other checksum families.

The public writer enum gains `sha3_256` and `sha3_512`. It emits the canonical
hyphenated XISF names and uses the same in-memory and bounded spool-file digest
paths as the existing algorithms. Invalid enum values and malformed descriptors
continue to fail explicitly.

## Consequences

This closes the implementation gap for the standard checksum family without
changing PFI's image/sample profile. It does not make a claim about producer
authenticity: all supported hashes provide integrity against a digest declared
inside the same file. Cross-platform CI and an independent/native fixture are
still required before a public conformance claim.
