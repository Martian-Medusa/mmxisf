# M0 / accelerated week-one status

- Started: 2026-09-13T15:12:16Z
- Completed: 2026-09-13T15:18:40Z
- Status: COMPLETE
- Scope: specification identity, PFI profile, conformance matrix, threat model,
  resource budgets, fixture policy, dependency/API decisions, revised estimate

The directly instrumented final M0 documentation interval was 6 minutes 24
seconds. The broader checkpoint-to-checkpoint interval since the initial
repository commit was 33 minutes 42 seconds. Earlier research and repository
foundation work before that commit was not instrumented, so neither number is a
person-hour accounting claim.

## Completed in the first measured session

- Official XISF 1.0 source repository HEAD verified and retrieved.
- Final specification identity pinned to commit
  `7fd38ebc999c0dc18d7cdce3032407b20cada9ea`.
- Source manifest and root PIDoc SHA-256 values recorded.
- Initial machine-readable conformance matrix created.
- PFI reader profile separated from formal baseline-decoder claims.
- Threat model and draft PFI resource budgets created.
- Fixture provenance schema and interoperability policy created.
- Strict streaming XML direction proposed with an explicit M1 acceptance spike.
- Initial nine-file local corpus measured without decoding pixels.
- Public error, ownership and cancellation semantics frozen for pre-1.0 work.
- Private dependency boundaries and candidate libraries documented.

## Follow-on work

- Validate the proposed XML and dependency choices with compile/behavior spikes;
  these are M1/M2 acceptance activities.
- Add redistributable fixtures when owner permission is known.
- Expand the conformance matrix from feature-level rows to individual normative
  clauses alongside implementation, keeping each claim linked to tests.
- Record end time, actual elapsed effort, and revised roadmap estimate.

## Owner inputs and gates

- G0 before publication: confirm public name `mmxisf` and Apache-2.0.
- Before PFI interoperability closure: provide or identify representative XISF
  files and whether each may be committed publicly. Work can continue with
  synthetic fixtures before this input arrives.

## Validation

- Planning JSON parse/schema checks: PASS.
- Conformance-row identifiers and status values: PASS, 66 unique rows.
- CMake build: PASS.
- CTest: PASS, 2/2 including the planning validator.
- Local corpus inspection: PASS for nine headers; pixel decoding NOT_TESTED.
