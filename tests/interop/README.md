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
