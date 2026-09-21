# Independent-producer interoperability fixtures

These nine files were generated on 2026-09-13 from Martian Medusa-owned
deterministic numeric arrays by invoking only the documented public
`XISF.write` API of the independent PyPI package `xisf` 0.9.7. No third-party
image data is present and no generator implementation source was inspected.
The fixtures are repository test data under the repository's Apache-2.0
license.

The upstream package is GPLv3 and is not a build, runtime, or test dependency of
`mmxisf`. It was used once as an external black-box producer in an isolated
temporary environment. Its PyPI source archive SHA-256 is
`fcc8d33b3c45461abb0d71b3cd1207c08548ee11f51d4aaa5615f19cc54e1724`.

All images have width 257 and height 193. Deterministic sources cover UInt8,
UInt16, UInt32, Float32, and Float64 samples plus Gray and RGB color spaces.
The arrays use modular integer ramps or bounded rational gradients; their exact
decoded byte counts and SHA-256 identities are recorded in `manifest.json`.
The producer serializes these as little-endian Planar XISF images.

The XISF bytes are committed as Base64 text so ordinary source checkouts do not
need Git LFS for this small test matrix. The committed executable test decodes
that transport in memory and compares the SHA-256 identity of every decoded
byte with the original source array. It also asserts the declared sample type,
color space, storage, byte order, codec, and shuffle profile. Exact file
identities are in `manifest.json`.

The additional `mmxisf-writer-rgb-u16` fixture is the exact deterministic
little-endian Planar UInt16 RGB output anchored by `writer_test.cpp`. It was
read by the same independent package through its documented public API as a
channels-last `(2, 2, 3)` `uint16` array, with every sample equal to the writer
input. Its own writer provenance and hashes are separate in `manifest.json`.

The `mmxisf-writer-multi-scalars` fixture extends the consumer-side oracle to
four images in one monolithic unit: UInt8 Gray, UInt32 Gray, Float32 RGB, and
Float64 Gray. Package `xisf` 0.9.7 enumerated all four through its documented
public API and returned the exact source values, dtypes, geometries, Planar
storage, little-endian order, and floating-point bounds. The fixture is also a
fuzz seed and its file and per-image pixel hashes are asserted in compiled
tests. The current serialization was regenerated after Revision 1 identifier
validation was enabled and was re-read through the same independent public API
on 2026-09-21 with exact shapes, dtypes, and values.

The `mmxisf-writer-metadata` fixture covers deterministic direct metadata
serialization without exposing raw XML: image-scoped String and TimePoint
Properties, an image FITS keyword, and an XISF-unit String Property. The same
independent consumer preserved the exact Property values, FITS value/comment,
scope split, and UInt16 pixels through its public metadata and image APIs.
The current serialization was regenerated after Revision 1 identifier
validation was enabled and was re-read through the same independent public API
on 2026-09-21 with exact pixels and metadata values.

The `mmxisf-writer-codecs` fixture covers zlib, LZ4, LZ4HC, and Zstandard
output, both shuffled and unshuffled blocks, and SHA-1/256/512 declarations.
Compiled tests verify every checksum before decompression and compare the exact
decoded pixel hash for all five images. The independent consumer accepted the
same descriptors and returned the exact shapes, dtypes, and pixels. Because
codec byte streams can change across dependency versions, this committed file
anchors the observed external result without imposing its whole-file hash on
future writer builds.

A 2026-09-14 black-box consumer check covered the block-Property writer API.
Package `xisf` 0.9.7 returned the exact Zstandard+shuffle+SHA-256 2x2 F64Matrix
values, a big-endian UI16Vector as `[513, 1027]`, formatting metadata, byte
order, checksum, and attachment locations from the public metadata API. The
deterministic 12,292-byte probe had SHA-256
`4da1d1bef566e6cbe626522cf938d738db667e7b2648a26bc1a2c376eb125abb`.
The compiled writer test regenerates and verifies its stronger self-round-trip
contract; the external package is not a project dependency.

The `mmxisf-writer-native-properties` fixture is a separate source-bound input
for manual PixInsight validation. It contains one 2x2 UInt16 Gray image and
image-scoped `F64Matrix`, `UI16Vector`, and String Properties with exact owned
values. `tests/pixinsight/MMXISFWriterNativeValidation.js` requires native
recovery of those values and the exact working-sample pixel hash. Until that
manual script produces a PASS record, the manifest and M6 status retain native
writer Property interoperability as pending.

The Revision 1 migration changed only this fixture's same-length `Image/@id`
from `native-validation` to `native_validation`. The historical PixInsight
record remains bound to the predecessor source hash; the current hash therefore
requires a new native run.
The current fixture also passed a 2026-09-21 independent public-API read with
exact pixels, matrix, vector, String value, and descriptors; that result does
not replace the separate native PixInsight gate.

The `mmxisf-writer-revision1-subblocks` fixture isolates the normative
Revision 1 shuffle change. Its complete 512-byte UInt16 image block is shuffled
once and only then divided into eight independently Zstandard-compressed
64-byte regions. The compiled test binds the exact descriptor, file hash,
checksum, and decoded pixel hash. The source-bound
`tests/pixinsight/MMXISFRevision1SubblocksValidation.js` script is the separate
native-consumer gate for this corrected writer path. Public API replay with
independent package `xisf` 0.9.7 exposes the exact compression and eight-pair
`subblocks` descriptors, but its image API decodes only 32 samples and fails
the required 1x16x16 reshape. This is recorded as `LIMITED`, not as evidence
against the Revision 1 algorithm or as an independent-consumer PASS.

The `mmxisf-writer-sha3-rgb` fixture is a deterministic 2x2 Planar UInt16 RGB
writer output using Zstandard+shuffle and SHA3-256. `mmxisf` verifies the exact
serialized checksum and recovers source-order pixel SHA-256
`adc4289fa7f0c65f72ac49b058d1368e7028ab84cd7c91eb027b4a589d21bbc6`.
The independent package `xisf` 0.9.7 exposed the exact checksum descriptor and
returned channels-last values `[[[1,5,9],[2,6,10]],[[3,7,11],[4,8,12]]]` as a
2x2x3 `uint16` array. That proves external acceptance and pixel identity, not
that the independent package itself verified the digest.
The Revision 1 migration changed only its same-length `Image/@id` from
`sha3-rgb` to `sha3_rgb`; compressed pixels and the declared digest are
unchanged. A 2026-09-21 independent public-API replay recovered the exact RGB
values and digest descriptor from the current source hash.
