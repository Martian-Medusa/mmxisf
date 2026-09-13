# ADR 0002: PFI-first reader profile before baseline conformance

- Status: Accepted
- Date: 2026-09-13

## Context

XISF 1.0 section 7 defines a baseline decoder broader than PFI's immediate
needs. It includes multiple images, all six standard compression identifiers,
inline/embedded/attachment blocks, planar and normal storage, UInt8/UInt16/
Float32 samples, and Gray/RGB. PFI currently accepts exactly one image and needs
additional common scalar types and specific metadata fidelity.

## Decision

Release `0.2` as the explicitly named `PFI_READER_0_2` profile. Do not call it a
baseline XISF decoder. Complete and claim the specification's baseline decoder
profile in `0.3`, after generic inline-block support and its full evidence matrix
are green.

PFI-specific metadata interpretation stays in PFI. The library preserves typed
and raw source values without introducing astronomy semantics.

## Consequences

PFI can adopt a production reader earlier without overstating conformance.
Marketing and API documentation must always name the supported profile. The
matrix, not a general statement such as "supports XISF 1.0", defines coverage
until 1.0.
