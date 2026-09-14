# Development binary advisory review — `3fd55e6`

- Review date: 2026-09-14
- Result: **PASS — development snapshot only**
- Publication authorized: **no**
- Frozen candidate: **no**
- `mmxisf`: `3fd55e6a0ad99a645388ef842fe040b996756a91`
- PFI provider integration: `c44f9453a4b83bd33d958068e78a4dab1d7d4da7`
- Provider SHA-256: `90e5a167c0de68c4c8fa7289a77bda463f54ce3734bdf1c39b5e159d9cac6aba`
- Binary SBOM SHA-256: `df28b1f4a7bbe5fb11ff8fd3e42db825b3238c34eb48fdd04a3cf53ae2e61a6d`
- Bundle manifest SHA-256: `eb13578b7ca8e1bf03dd12c601e76b3bf60d7efb9883e390203ce266b4908136`
- Native validation script SHA-256: `5cfbaab33b0d1e989e1c1f0f047dc5cb38ac256a6bcf0b8463a8f42983c91a2e`

This time-sensitive review covers the exact five direct dependencies recorded
in the development provider's binary SBOM. It demonstrates that the current
controlled dependency graph has no known upstream blocker in the reviewed
official channels. It does **not** close the production-readiness gate: there
is no frozen candidate, the complete build toolchain is outside this pass, and
the review must be repeated immediately before publication.

## Disposition

| Dependency | Exact version | Result | Evidence |
| --- | --- | --- | --- |
| Expat | 2.8.4 | PASS — fixed release | The exact [2.8.4 changelog](https://github.com/libexpat/libexpat/blob/R_2_8_4/expat/Changes) records fixes for four CVEs. The [upstream security page](https://github.com/libexpat/libexpat/security) listed no published GitHub advisories at review time. |
| zlib | 1.3.2#2 | PASS — current release snapshot | The [upstream releases page](https://github.com/madler/zlib/releases) identifies 1.3.2 as the latest published release and says it addresses findings from a security audit. The [security page](https://github.com/madler/zlib/security) listed no published GitHub advisories at review time. |
| LZ4 | 1.10.0 | PASS — API-scoped | The [release page](https://github.com/lz4/lz4/releases) identifies 1.10.0 as latest and the [security page](https://github.com/lz4/lz4/security) listed no published GitHub advisories. Open [issue 1783](https://github.com/lz4/lz4/issues/1783) concerns deprecated unsafe `LZ4_decompress_fast`; `mmxisf` uses `LZ4_decompress_safe` and never calls the reported API. |
| Zstandard | 1.5.7 | PASS — current release snapshot | The [release page](https://github.com/facebook/zstd/releases) identifies 1.5.7 as latest and the [security page](https://github.com/facebook/zstd/security) listed no published GitHub advisories. `mmxisf` uses the core compression/decompression APIs, not the contributed seekable format. |
| OpenSSL | 3.6.4 | PASS — fixed release | The [3.6 release notes](https://mirror.openssl-library.org/news/openssl-3.6-notes/) identify 3.6.4 as a security patch. The [official vulnerability list](https://mirror.openssl-library.org/news/vulnerabilities/) marks the reviewed 3.6-series issues as affecting versions before 3.6.4; the [source page](https://www.openssl-library.org/source/) supplies the current official release context. |

The LZ4 and Zstandard API-scope claims were checked against `src/reader.cpp`
and `src/writer.cpp` at the exact `mmxisf` commit above.

## Remaining boundary

- “No published advisory” is a dated observation, not proof that no unknown or
  unpublished vulnerability exists.
- The final audit must bind a frozen source commit, exact release binaries,
  their SBOMs, and the complete supported build toolchains.
- Any dependency/toolchain change invalidates this snapshot.
- Signing, notarization, Windows evidence, and publication authorization remain
  separate gates.

The machine-readable record is
[`2026-09-14-development-binary-advisory-review-3fd55e6.json`](2026-09-14-development-binary-advisory-review-3fd55e6.json).
