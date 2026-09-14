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

## First retained campaign

The first manual campaign completed successfully on commit
`44d91da46655deb73c7b258a1204e757bb9bb90e` in GitHub Actions run
[`34792034236`](https://github.com/Martian-Medusa/mmxisf/actions/runs/34792034236).
ASan/UBSan executed 2,793,891 inputs in 15 minutes without a crash, timeout, or
sanitizer finding. libFuzzer ended at `cov: 10844`, `ft: 30853`, a 764-unit
1,119 KiB live corpus, about 3,100 executions/second, and 505 MiB RSS.

The retained upload contains 785 files (551,031 compressed bytes), artifact ID
`10328078617`, with workflow-reported ZIP SHA-256
`d629c94c8b581c6535840115cebc9a8d5b9fe82af565697350b261a7eaf02b30`.
No failure artifact was produced. This is a historical PASS for that exact
reader commit and seed set; SHA-3 support and later changes still require a
fresh candidate campaign.
