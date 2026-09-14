# Fuzzing policy

The reader treats every XISF byte as untrusted. Fuzzing therefore exercises the
same public `Reader::open_source`, first-image decode, and bounded block-Property
decode paths used by applications. The harness applies deliberately tighter
limits than the normal API defaults so malformed inputs cannot consume a CI
runner without bound.

## Per-commit gates

The main CI workflow runs two fast checks under AddressSanitizer and
UndefinedBehaviorSanitizer:

- 20,000 deterministic mutations of a compact valid unit;
- 20,000 coverage-guided libFuzzer executions seeded from the versioned parser
  seeds and redistributable interoperability fixtures.

Any crashing input is retained as a short-lived workflow artifact. A confirmed
regression must be minimized, converted to a permanent test or seed, and
documented before the defect is closed.

## Long campaign

The `fuzz-long` workflow is intentionally manual. It runs one reader libFuzzer
process for 15 minutes with ASan/UBSan, a 1 MiB input cap, a 10-second per-input
timeout, and a 4 GiB RSS limit. It always retains the evolved corpus for 14 days
and retains crash artifacts for 30 days.

The workflow is not scheduled automatically: campaign frequency is a release
decision and must account for repository compute policy. Before a public beta,
run it against the exact candidate commit and record the workflow URL, final
coverage/features, corpus size, result, and any promoted regression fixtures in
the milestone evidence.

One successful bounded campaign is evidence for the tested build and seed
corpus, not a proof of parser safety. Re-run after parser, codec, metadata, or
resource-limit changes and use additional sanitizers/platforms when available.
