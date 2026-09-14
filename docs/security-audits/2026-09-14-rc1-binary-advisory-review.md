# RC1 binary advisory review

- Review date: 2026-09-14
- Candidate: `v0.1.0-rc.1`
- Commit: `fd62b5b2a0239638d2ea252912212b3fa4f92178`
- Result: **LIMITED — exact direct dependencies pass; complete toolchain audit pending**
- Publication authorized: **no**

The six retained candidate binary SBOMs for macOS arm64, Linux amd64, and
Windows-target MinGW static/shared builds contain the same five direct
dependencies from the pinned vcpkg registry snapshot. Their exact versions are
Expat 2.8.4, zlib 1.3.2#2, LZ4 1.10.0, Zstandard 1.5.7, and OpenSSL 3.6.4.

## Direct dependency disposition

| Dependency | Result | Dated disposition |
| --- | --- | --- |
| Expat 2.8.4 | PASS — fixed release | The exact upstream changelog records the reviewed 2026 security fixes; the project advisory page listed no published GitHub advisories. |
| zlib 1.3.2#2 | PASS — current release snapshot | The upstream release is marked latest and states that it addresses findings from its completed security audit; the project advisory page listed none. |
| LZ4 1.10.0 | PASS — API-scoped | Open issue 1783 is specific to deprecated unsafe `LZ4_decompress_fast`; the exact candidate calls bounded `LZ4_decompress_safe`. |
| Zstandard 1.5.7 | PASS — current release snapshot | The exact upstream release and project advisory page were reviewed; the project advisory page listed none. The candidate uses core block APIs, not the contributed seekable format. |
| OpenSSL 3.6.4 | PASS — fixed release | Official notes identify 3.6.4 as a security patch release and the vulnerability list identifies the reviewed 3.6-series issues as affecting earlier releases. |

## Remaining security boundary

This is not a complete toolchain audit. AppleClang/macOS SDK, the Ubuntu
compiler and container package sets, MinGW/Wine, and native MSVC must receive an
explicit release disposition. Native Windows/MSVC binaries and SBOMs do not yet
exist. Absence of a published upstream advisory is a dated observation, not
proof that no unknown or unpublished vulnerability exists.

The review must be refreshed after any source, dependency, toolchain, or binary
change and immediately before a later publication date. The machine-readable
record is `2026-09-14-rc1-binary-advisory-review.json`.
