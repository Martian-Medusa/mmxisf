# ADR 0020: Non-self-referential candidate identity

- Status: accepted
- Date: 2026-09-14

## Context

A tracked file cannot contain the Git commit identifier of the commit that
contains that file. Changing the identifier changes the tree and therefore
changes the commit identifier again. The original `candidateCommit` fields in
the support profile and readiness ledger made a literal exact-self binding
impossible.

The release process still needs two independently checkable identities: a
human-facing immutable candidate name and the exact source commit and archive
bytes exercised by the gates.

## Decision

A frozen support profile and readiness ledger bind an intended annotated
release-candidate ref named `vX.Y.Z-rc.N`. The deterministic source-candidate
manifest separately records the exact 40-hex commit, archive SHA-256, and the
support-profile state, ref, and SHA-256.

Before candidate publication, the annotated ref is created once at the tested
commit. Candidate verification resolves the ref to a commit and requires it to
equal both checked-out `HEAD` and the commit in `source-candidate.json`. A
published candidate ref is never moved; a corrected candidate uses the next
`rc.N` value.

Development rehearsals retain `PREPARED`/`UNFROZEN` state and an empty ref.
`PrepareSourceCandidate.cmake` can therefore produce explicitly unpublished
development evidence without inventing a candidate identity. Its optional
`MMXISF_VERIFY_CANDIDATE_REF=ON` mode is reserved for a frozen checkout where
the candidate ref is available locally.

## Consequences

- candidate identity is representable without a self-referential hash;
- the ref is readable in source while the generated manifest remains the
  authority for exact commit and archive bytes;
- support-profile and readiness state/ref pairs are machine-checked for
  equality;
- publication still requires explicit owner approval and post-tag identity
  verification;
- the support-profile schema advances to `1.1.0`, the readiness schema to 2,
  and the source-candidate manifest to `1.1.0` before the first public beta;
- there is no C++ source or ABI change.
