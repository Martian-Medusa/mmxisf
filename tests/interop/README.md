# Independent-producer interoperability fixtures

These five files were generated on 2026-09-13 from Martian Medusa-owned
deterministic numeric arrays by invoking only the documented public
`XISF.write` API of the independent PyPI package `xisf` 0.9.7. No third-party
image data is present and no generator implementation source was inspected.
The fixtures are repository test data under the repository's Apache-2.0
license.

The upstream package is GPLv3 and is not a build, runtime, or test dependency of
`mmxisf`. It was used once as an external black-box producer in an isolated
temporary environment. Its PyPI source archive SHA-256 is
`fcc8d33b3c45461abb0d71b3cd1207c08548ee11f51d4aaa5615f19cc54e1724`.

All images have width 257 and height 193. The Gray source is a one-channel
little-endian UInt16 array with value
`((x mod 17) + 17*(y mod 13))*257`. The RGB source is a channels-last Float32
array whose channels are `(x mod 11)/10`, `(y mod 7)/6`, and
`((x+y) mod 9)/8`. The producer serializes these as Planar XISF images.

The XISF bytes are committed as Base64 text so ordinary source checkouts do not
need Git LFS for this small test matrix. The committed executable test decodes
that transport in memory, reconstructs the source arrays independently, and
compares every decoded byte. It also asserts the declared codec and shuffle
profile. Exact file identities are in `manifest.json`.
