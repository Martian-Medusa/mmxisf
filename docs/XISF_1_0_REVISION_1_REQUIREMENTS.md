# XISF 1.0 Revision 1 derived requirements

This checklist is derived independently from the public XISF 1.0 Revision 1
document, version 1.01, and its official source at commit
`9dbdd650bbc4b7e51884f6bceeb7607be5f678a3`. It records impact on the standalone
local-monolithic `mmxisf` profile; it is not a replacement for the normative
specification.

| Revision 1 item | Disposition | mmxisf action or boundary |
|---|---|---|
| Zstandard and Zstandard+shuffle are standard codecs | `CODE_CHANGE` | Reclassify existing reader/writer support as standard and required. |
| Shuffle the complete logical block before compression subblock division | `CODE_CHANGE` | Global writer mapping and global-offset reader scatter; independent unaligned-boundary fixture. |
| Trailing bytes not forming a complete shuffle item remain unshuffled | `TEST_CHANGE` | Reader/writer mapping preserves the logical tail and receives regression coverage. |
| Inline/embedded checksums cover decoded binary bytes; SHA-3 follows FIPS 202 | `PASS_EXISTING` | Existing checksum pipeline and SHA3-256/SHA3-512 support remain applicable. |
| Alternative non-finite spellings `nan`, `-nan`, `inf`, `-inf` | `CODE_CHANGE` | Reader and writer scalar/complex validators accept the exact new forms. |
| Boolean values also accept `1` and `0` | `PASS_EXISTING` | Existing reader/writer validation already accepts both numeric forms. |
| Empty vectors and matrices use empty inline blocks | `CODE_CHANGE` | Reader accepts zero typed extents; writer emits empty `inline:base64` character contents and no attachment. |
| `Image/@id` syntax and unit-wide uniqueness | `CODE_CHANGE` | Reader and writer validate `[_a-zA-Z][_a-zA-Z0-9]*` and case-sensitive uniqueness. |
| Extension elements are root children only | `CODE_CHANGE` | Root extension inventory remains bounded; extensions nested in core or extension elements are rejected. |
| Unrecognized elements, attributes, and properties do not hide supported data | `CODE_CHANGE` | Unknown Property and Table Field descriptors remain inspectable; typed access stays explicitly unavailable. Root extensions remain inspectable. |
| RGB luminance coefficients are derived from D50 primaries | `TEST_CHANGE` | Profile remains structural inspection only and does not claim RGB colorimetry or transform execution. |
| `AstrometricSolution:` property namespace | `TEST_CHANGE` | Generic scalar/vector/matrix Property support preserves the namespace; no astrometric evaluation API is claimed. |
| CIELab transformation corrections | `OUTSIDE_PROFILE` | CIELab descriptors remain inspectable and pixel conversion remains explicitly unsupported. |
| New standard metadata properties | `PASS_EXISTING` | Generic validated Property inventory preserves recognized scalar/string/vector/matrix values without semantic invention. |
| Official XSD | `DOCUMENTATION_ONLY` | Hash and audit provenance are recorded; no runtime schema dependency or network access is introduced. |
| String Table Cell examples corrected | `DOCUMENTATION_ONLY` | Attribute-form String Cell support remains a documented legacy compatibility policy. |
| Distributed XISF/XISB and external resource resolution | `OUTSIDE_PROFILE` | Existing explicit rejection/no-network boundary is unchanged. |

Release qualification is tracked separately in
`docs/production-readiness.json`. The immutable `v0.1.0-rc.1` tag remains the
historical document-1.00 candidate.

## Implementation checkpoint

All `CODE_CHANGE` and `TEST_CHANGE` items above are implemented in the local
tree. The four new standard metadata Properties have exact type/value retention
tests. Automatic writer emission of `XISF:ChecksumAlgorithms` and
`XISF:BlockAlignmentSize` is not required by the format (the specification uses
“should”); callers can declare them through the existing metadata writer API,
so automatic insertion is deferred to avoid silently rewriting caller-owned
metadata policy.

The corrected global-shuffle eight-subblock writer path passed source-bound
native PixInsight 1.9.5 arm64 validation with all 256 exact samples on
2026-09-21. Release qualification remains fail-closed pending the current
block-Property native PixInsight fixture plus exact-candidate macOS/Linux,
native Windows/MSVC, fuzz, and frozen-candidate gates. Four current writer
fixtures also passed a fresh independent public-API replay on 2026-09-21.
