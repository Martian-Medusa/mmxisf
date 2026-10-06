# ADR 0021: Candidate X, final Y, and exact-commit re-qualification

- Status: accepted (amended 2026-10-06)
- Date: 2026-10-06
- Decision authority: repository owner (decisions recorded 2026-10-06)

## Context

`docs/RELEASE_PROCESS.md` and ADR 0020 define a frozen candidate only through
an immutable annotated `vX.Y.Z-rc.N` ref. They did not define how a candidate
becomes the final `vX.Y.Z` release. `v0.1.0-rc.1` is already published and is
historical document-1.00 evidence; the Revision 1 conformance corrections made
after it require a new candidate.

A tracked file cannot contain the identifier of its own commit (ADR 0020), and
evidence about a commit can only be recorded after that commit exists. A final
tag placed on the frozen commit itself would therefore ship a readiness ledger
that cannot yet contain the candidate's own evidence (the original form of this
ADR accepted that consequence; the owner reversed it on 2026-10-06).

## Decision

1. **Candidate X.** X is the frozen commit of candidate `vX.Y.Z-rc.N`. For
   0.1.0, X is `d17605686768832226da55090a33b28888bfab35` (candidate
   `v0.1.0-rc.2`), derived from revision
   `dc82cadae38669629d548d9e7ed8767b338d08e3`. The annotated `rc.N` ref is
   created once at X and never moved. All candidate gates run on X.
2. **Final Y.** Y is the commit that receives the final annotated `vX.Y.Z`
   tag. Y is X plus one or more documentation-only evidence commits: the
   readiness ledger, quality/fuzz/performance/security evidence records,
   release notes, and process documents. Y is a descendant of X.
3. **Byte-identity rule.** Between X and Y, every source, header, build,
   test, tool, CI, container, example, viewer, SBOM and installed-package input
   file is byte-identical. Only these paths may differ:
   `docs/production-readiness.json`, `docs/RELEASE_NOTES_*.md`,
   `docs/M8_STATUS.md`, `docs/RELEASE_PROCESS.md`, `docs/decisions/**`,
   `docs/quality-runs/**`, `docs/fuzz-campaigns/**`,
   `docs/performance-runs/**`, `docs/security-audits/**`, and
   `docs/ROADMAP.md`, and `README.md`. In particular the support profile, the conformance matrix,
   the specification baseline, the public API baseline and
   `docs/PUBLIC_API_AUDIT.md` are unchanged. The rule is machine-checkable with
   `git diff --name-only X Y` filtered against that list and is part of the
   release verification.
4. **Re-qualification on Y.** After Y is final, the complete exact-commit gate
   set is re-run on Y itself: Linux amd64 (pinned-vcpkg production and system
   graphs, sanitizers, mutation smoke, TSan, byte-identical repeat builds, long
   fuzz), macOS arm64 (the same families), and the hosted CI/native
   Windows-MSVC matrix, plus deterministic source preparation with
   Git-archive verification. Y's own re-run evidence cannot be inside Y; it is
   published outside the tagged tree as release assets and release notes, and
   recorded in the repository by a later documentation commit that is not
   tagged.
5. **Refs.** `vX.Y.Z-rc.N` resolves to X. `vX.Y.Z` resolves to Y. Release
   verification requires: X is an ancestor of Y; the byte-identity rule holds;
   `rc.N` resolves to X and `vX.Y.Z` to Y; source preparation of Y uses the
   checked-out `HEAD` as candidate ref without `MMXISF_VERIFY_CANDIDATE_REF`
   (that mode verifies the profile's `rc.N` ref against `HEAD` and is for X
   only); and the Git archive of the final tag is reproduced byte-for-byte from
   the downloaded asset. `rc.N` is a candidate-identity ref and is not
   published as a separate GitHub prerelease. Neither ref is ever moved.
6. **Failure.** If any gate fails on X or Y, or any file outside the allowed
   set must change, the candidate is superseded by the next `rc.N`; no final tag
   is created for it and no existing ref is moved.
7. **Ledger scope.** `docs/production-readiness.json` in Y records evidence for
   the qualified candidate X and for properties that are provable before Y
   exists. Identity and integrity of Y's own archive and of published assets
   are verified outside Y (item 4) and are not ledger rows inside Y.
8. **Dependencies.** A dependency pin is accepted for a release only with a dated
   disposition. For `v0.1.0` the owner accepts OpenSSL 3.6.4 with an API-scoped
   disposition (`docs/security-audits/2026-10-06-rc2-advisory-review.json`);
   an OpenSSL bump is scheduled for the next mmxisf version.

## Consequences

- Consumers (PFI) pin the final tag, the source archive of Y, and the
  separately published Y re-qualification evidence; X's evidence and Y's differ
  only by documentation.
- The shipped ledger can derive `READY` without self-reference.
- Every exact-commit gate runs twice (X, then Y), including hosted CI; this is
  the accepted cost.
- Publication still requires explicit owner approval for each external write
  (branch/ref push, hosted CI dispatch, tags, GitHub Release, asset upload).
- There is no C++ source, API, or ABI change.
