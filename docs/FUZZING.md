# Fuzzing policy

The reader treats every XISF byte as untrusted. Fuzzing therefore exercises the
same public `Reader::open_source`, first-image decode, and bounded block-Property
decode paths used by applications. The harness applies deliberately tighter
limits than the normal API defaults so malformed inputs cannot consume a CI
runner without bound.

## Per-commit gates

The manually dispatched main CI workflow runs two checks under AddressSanitizer
and UndefinedBehaviorSanitizer:

- 20,000 deterministic mutations of a compact valid unit;
- 20,000 coverage-guided libFuzzer executions seeded from the versioned parser
  seeds and redistributable interoperability fixtures.

Any crashing input is retained as a short-lived workflow artifact. A confirmed
regression must be minimized, converted to a permanent test or seed, and
documented before the defect is closed.

For routine local development, `tools/run_local_quality_gates.sh` runs the
deterministic 20,000-case sanitizer smoke after the ASan/UBSan unit suite. A
coverage-guided local campaign additionally requires a Clang installation with
its matching libFuzzer and sanitizer runtimes; never combine a successful local
result with a Windows/Linux support claim.

The versioned `tools/run_long_fuzz_campaign.sh` runner is shared by CI and
direct compatible-host runs. It defaults to the declared 900-second, 1 MiB
input, 10-second per-input, and 4 GiB RSS limits, refuses to overwrite prior
evidence, builds with matching Clang ASan/UBSan/libFuzzer, prepares the complete
versioned seed corpus, and retains the full log plus evolved corpus and crash
directory. On a Docker-capable Linux amd64 host,
`tools/run_linux_long_fuzz_docker.sh` applies the pinned Ubuntu image and LLVM
symbolizer around the same runner. The viewer is explicitly disabled in both
paths.

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

## Local macOS campaign after CI budget pause

An additional 15-minute coverage-guided campaign completed on macOS arm64 at
commit `673d6179af37d6a7d55d24d94c65c170e37e2f53`. It executed 3,921,535
inputs, added 3,404 units, peaked at 463 MiB RSS, and produced no crash or
ASan/UBSan finding. A replay of the retained local corpus reached `cov: 13866`
and `ft: 38237`.

This is deliberately classified `PASS_WITH_TOOLCHAIN_LIMITATION`: Apple Clang
21 supplied the compiler and matching sanitizer runtimes, while a separately
installed LLVM 17 archive supplied the otherwise missing libFuzzer runtime.
It is useful additional parser pressure, but does not satisfy the matching-
toolchain candidate gate and adds no Windows/Linux evidence. The external
symbolizer also failed to start, so a future failure would require offline
symbolization. Exact binary, runtime, corpus-manifest, host, and limit evidence
is retained in
[`fuzz-campaigns/2026-09-14-local-macos-arm64.json`](fuzz-campaigns/2026-09-14-local-macos-arm64.json).

## Linux amd64 development campaign on mllse

A matching-toolchain 15-minute campaign also completed in the pinned Ubuntu
24.04 Docker environment on `mllse`, using only the deterministic extracted
source archive for exact commit `50fc120391e7f156c069bfdd13e41e4942be2ccf`.
Clang 18 with ASan/UBSan executed 2,378,414 inputs in 901 seconds. It ended at
`cov: 15638`, `ft: 43269`, an 853-unit 1,190 KiB effective corpus, 2,639
executions/second, and 487 MiB peak RSS. No crash artifact, timeout, or sanitizer
finding was produced.

The full 597,528-byte log and 865-file evolved corpus remain on the private
server with their hashes recorded in
[`fuzz-campaigns/2026-09-14-linux-amd64-50fc120.json`](fuzz-campaigns/2026-09-14-linux-amd64-50fc120.json).
The image lacked an external LLVM symbolizer, so a hypothetical failure would
have required offline stack symbolization. This is
`PASS_DEVELOPMENT_NOT_FROZEN`: it strengthens Linux parser evidence but does
not satisfy the exact frozen-candidate gate or the production dependency floor.

## Versioned-runner Linux campaign

The shared local/CI runner was then exercised from a deterministic source
archive for exact commit
`ed594d2583ff15df0cd4d2bc34bf2d2e36569cdf` in the updated pinned Ubuntu
24.04 Docker environment. Matching Clang 18 ASan/UBSan and libFuzzer, now with
`llvm-symbolizer-18` available, executed 2,504,364 inputs in 901 seconds. The
campaign ended at `cov: 15698`, `ft: 43613`, an 831-unit 1,090 KiB effective
corpus, 2,779 executions/second, and 482 MiB peak RSS. It produced no crash
artifact, timeout, or sanitizer finding.

The full 618,358-byte log and 840-file retained corpus remain on the private
server. Their identities and the exact source archive, binary, container, seed,
and limit records are retained in
[`fuzz-campaigns/2026-09-14-linux-amd64-ed594d2.json`](fuzz-campaigns/2026-09-14-linux-amd64-ed594d2.json).
This is still `PASS_DEVELOPMENT_NOT_FROZEN`: it proves the versioned campaign
runner and strengthens current parser evidence, but cannot satisfy the exact
frozen-candidate gate.
