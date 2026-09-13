# Roadmap and complexity estimate

## Executive estimate

Overall complexity is **high (8/10)**. The XML syntax is not the hard part. The
cost comes from a safe binary container, many typed metadata forms, compressed
and shuffled blocks, exact pixel semantics, hostile-input handling, and proving
interoperability across independent producers.

Estimates are person-weeks for an experienced C++ engineer and include focused
tests and documentation. They are planning ranges, not delivery commitments;
confidence is approximately -20% / +40% until M0 freezes the conformance matrix
and representative fixtures are available.

| Milestone | Deliverable | Effort | Dependency |
| --- | --- | ---: | --- |
| M0 | Normative matrix, naming/license ADRs, fixture and threat model | 1.5-2.5 | official spec access |
| M1 | Safe preamble/XML parser and metadata-only inspector | 2.5-4 | M0 |
| M2 | Raw monolithic blocks and PFI scalar pixel profile | 3-4.5 | M1 |
| M3 | Required codecs, shuffle, checksum, resource budgets | 3.5-5.5 | M2 |
| M4 | PFI metadata fidelity, multi-image policy, interoperability corpus | 3-5 | M1-M3 |
| M5 | PFI adapter and native/standalone parity validation | 3-5 | M2-M4 |
| M6 | Deterministic monolithic writer and round trips | 4-6 | M1-M4 |
| M7 | Broader property/image coverage and extension preservation | 3-5 | M4, M6 |
| M8 | Fuzzing, cross-platform packaging, docs, public beta audit | 3.5-5.5 | M1-M7 |

Some work overlaps. Expected totals:

- **PFI-ready read path (`0.2`)**: 12-18 person-weeks.
- **Public read/write beta (`0.5`)**: 20-29 person-weeks.
- **Production `1.0` for the declared local/monolithic profile**: 26-38
  person-weeks.
- **Near-complete XISF 1.0 including distributed/network features and signature
  work**: 38-55+ person-weeks, and should be a separate program.

For one full-time engineer, the realistic calendar is about 3-4.5 months to the
PFI-ready reader and 6-9 months to a defensible 1.0. Two engineers can reduce the
calendar to roughly 2.5-3.5 and 4-6 months respectively, but conformance, API,
and final interoperability gates remain serial.

## AI-first rebaseline after M0

The original person-week estimates remain useful as a measure of engineering
surface and review burden. They are not the active calendar plan. The accelerated
M0 exercise completed its planned artifacts in one session, with a 33-minute
42-second repository checkpoint interval after the initial foundation commit.
This strongly reduces planning overhead, but parser correctness, dependency
spikes, fuzzing and independent interoperability evidence will not scale at the
same rate.

Active AI-first calendar targets, assuming sustained autonomous work and prompt
availability of test fixtures, are:

| Outcome | Revised calendar target | Planning window from 2026-09-13 |
| --- | ---: | --- |
| M1 metadata inspector | 2-4 working days | 2026-09-15 to 2026-09-17 |
| M2/M3 PFI pixel reader with codecs/integrity | 2-3 weeks total | 2026-09-27 to 2026-10-04 |
| M4/M5 PFI-ready parity path | 3-5 weeks total | 2026-10-04 to 2026-10-18 |
| Public read/write beta | 7-10 weeks total | 2026-11-01 to 2026-11-22 |
| Supported monolithic 1.0 | 11-15 weeks total | 2026-11-29 to 2026-12-27 |

These windows are recalibrated after every milestone using actual elapsed time,
defect/rework rate, conformance rows closed, tests added, and uncovered external
dependencies. A fast green prototype does not move the production gate unless
negative, fuzz and interoperability evidence moves with it.

## Release sequence

### 0.0.x - foundation

- Repository, build/install package, ADRs, conformance matrix, security model.
- No format-support claim.

### 0.1 - inspect

- Bounded monolithic header parser.
- Image enumeration and PFI-relevant metadata extraction.
- CLI inspection example is optional; the library API is primary.

### 0.2 - PFI reader

- Required scalar 2-D grayscale/RGB image decoding.
- Required local block locations, compression/shuffle/checksum profile.
- Full-precision pixel and metadata parity corpus.
- PFI adapter remains behind a feature switch until native parity passes.

### 0.3 - hardened reader

- Broader XISF object coverage.
- Stable resource-limit policy, fuzzing, sanitizers, cancellation, performance
  budgets, and multi-platform package tests.

### 0.4 - writer

- Deterministic monolithic writer.
- Multiple images and declared metadata profile.
- Independent-consumer round trips.

### 0.5 - public beta

- Documented source API, examples, SBOM, security and contribution process.
- Linux/macOS/Windows release artifacts after supply-chain and license audit.
- No ABI promise unless an ADR explicitly promotes it.

### 1.0 - supported profile

- Every claimed conformance row has positive, negative, and interoperability
  evidence.
- Stable source API and declared ABI policy.
- PFI production adoption complete with rollback path removed only after a
  separate acceptance decision.

## Critical path

`specification matrix -> safe parser -> raw pixel correctness -> codecs and
integrity -> PFI parity -> writer -> hardening/publication`

Writer work can start after the model stabilizes, while fuzzing and packaging
can run alongside later conformance expansion. PFI integration must not start
from a metadata-only prototype because that would hide the hardest pixel and
resource-safety risks.

## Principal risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Public name collides with existing `libXISF` | High | Use `mmxisf` identity; decide before remote creation |
| Clean-room/license contamination | High | Normative-source log; do not inspect/copy GPL/PCL implementation code |
| Spec ambiguity or drift | High | Versioned matrix, archived provenance, independent fixtures, explicit deferrals |
| Compression bomb or forged sizes | Critical | Hard budgets, checked arithmetic, bounded streaming, fuzzing |
| Pixel orientation/layout mismatch | Critical for PFI | Bitwise reference planes, non-symmetric fixtures, explicit top-down/planar tests |
| Metadata coercion changes scientific context | High for PFI | Preserve raw and typed forms; fail unavailable instead of defaulting |
| Public API freezes too early | Medium | No ABI promise before 1.0; adapters hide internal model changes |
| Fixture redistribution is unclear | Medium | Store provenance/license/hash; generate synthetic fixtures where possible |

## Decision gates

- **G0:** approve public name and Apache-2.0 licensing.
- **G1:** accept the PFI conformance profile and resource budgets.
- **G2:** accept independent pixel/metadata parity before PFI default-on.
- **G3:** accept writer interoperability before public beta.
- **G4:** security/API/packaging review before `1.0` or external publication.
