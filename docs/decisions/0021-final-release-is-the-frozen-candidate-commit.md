# ADR 0021: Final release is the exact frozen candidate commit

- Status: accepted
- Date: 2026-10-06
- Decision authority: repository owner (decision recorded 2026-10-06)

## Context

`docs/RELEASE_PROCESS.md` and ADR 0020 define a frozen candidate only through
an immutable annotated `vX.Y.Z-rc.N` ref. They do not define how a candidate
becomes the final `vX.Y.Z` release. `v0.1.0-rc.1` is already published and is
historical document-1.00 evidence; the Revision 1 conformance corrections made
after it require a new candidate.

Rebuilding or re-freezing between a tested candidate and a final tag would
make the gate evidence apply to a different commit than the one consumers pin.
A tracked file cannot contain the identifier of its own commit (ADR 0020), so
the final identity must be expressed by Git refs and generated manifests, not
by a source edit.

## Decision

1. The owner designates source revision
   `dc82cadae38669629d548d9e7ed8767b338d08e3` as the basis for the next
   candidate, `v0.1.0-rc.2`. The freeze commit changes only the support profile,
   readiness ledger, one test-contract file and documentation; `src/`,
   `include/`, `CMakeLists.txt`, `vcpkg.json` and the installed package inputs
   are byte-identical to that revision.
2. The annotated `v0.1.0-rc.2` ref is created once at the freeze commit. It is
   the candidate identity bound by the support profile and readiness ledger.
   Candidate gates run on that exact commit.
3. If and only if every gate required for the claimed profile passes on that
   exact commit, the final annotated `v0.1.0` tag is created at the same commit.
   **Final `v0.1.0` equals the exact frozen `v0.1.0-rc.2` candidate commit.**
   No rebuild, re-freeze, version bump or source edit is permitted between the
   two refs. Evidence, release notes and ledger updates committed after the
   freeze are documentation only and are not part of the tagged tree.
4. `v0.1.0-rc.2` is a candidate-identity ref only. It is not published as a
   separate GitHub prerelease for consumers; the GitHub Release is created for
   `v0.1.0` only. Both tags are never moved or deleted after publication.
5. The deterministic source archive of the shared commit is the single release
   asset; its name uses the project version and short commit and is identical
   for either ref. Release verification must resolve both refs to the same
   commit and recompute the archive hash from the final tag.
6. If any gate fails or any tagged-tree file must change, the candidate is
   superseded by `rc.3` and the same procedure restarts; the `rc.2` ref is not
   moved and no final tag is created for it.

## Consequences

- Consumers (PFI) pin the exact candidate commit and archive SHA-256; evidence
  and the pinned artifact are provably the same bytes.
- `MMXISF_VERIFY_CANDIDATE_REF=ON` continues to verify the `rc.N` ref recorded
  in the profile; the final tag is verified by an additional explicit
  same-commit and Git-archive check recorded in the release-verification
  evidence.
- Publication still requires explicit owner approval for each external write
  (ref push, GitHub Release, asset upload).
- There is no C++ source, API, or ABI change.
