# Sources and clean-room policy

## Normative and first-party sources

- XISF 1.0 specification: <https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html>
- PixInsight XISF landing page: <https://pixinsight.com/xisf/>
- Official XISF specification source: <https://gitlab.com/pixinsight/XISF-specification>
- PixInsight staff clarification of monolithic attached blocks and header-driven
  layout: <https://pixinsight.com/forum/index.php?threads/questions-on-xisf-format.14154/>
- PixInsight staff clarification of XISF left-to-right, top-to-bottom pixel
  order: <https://pixinsight.com/forum/index.php?threads/astrometric-solution-is-written-flipped-to-fits-and-xisf-file.20446/>

The official document and its source repository were accessed during M0. The
final XISF 1.0 source is pinned in `docs/specification-baseline.json` to commit
`7fd38ebc999c0dc18d7cdce3032407b20cada9ea` from 2017-04-17. The source itself
is not vendored; the repository records derived requirements and hashes.

- pugixml manual used for XML-parser risk evaluation:
  <https://pugixml.org/docs/manual.html>

## Local PFI evidence inspected

- `software/PFI-worktrees/cpp-parity-foundation/src/platform/pixinsight/batch_input.js`
- `software/PFI-worktrees/cpp-parity-foundation/src/core/measurement/frame_metadata.js`
- `software/PFI-worktrees/cpp-parity-foundation/docs/cpp/BUILD_AND_VALIDATION_AUDIT.md`
- `software/PFI-worktrees/cpp-parity-foundation/docs/cpp/CAPABILITY_MAP.md`

These files establish PFI's current adapter needs; they are not XISF normative
sources.

## Private current-producer interoperability evidence

- PixInsight 1.9.3 / XISF module 1.1.2 Linux resource `ar-src.xisf`, SHA-256
  `30006e9afb5708cbe8e8d7a9f74f86c78994a799602d6e675a4cbc6f4297f946`:
  embedded UInt8 RGB, `zstd`, inline and attached block-backed Properties.
- PixInsight 1.9.3 / XISF module 1.1.2 Linux resource `psf-fit.xisf`, SHA-256
  `0ad555e34e7ffc5ed0e9305ca53382537de6c075e9fe42ad886d2e67de1dccdf`:
  attached UInt16 Gray, `zstd`.
- PixInsight-produced `M106_RGB.xisf`, SHA-256
  `2e288eb3d5efe06b5d4db1da121e0ce6a378d2caca5af52f83948a4615e51d1d`:
  attached Float64 RGB with rich metadata.
- PixInsight-produced `NGC2244_linear.xisf`, SHA-256
  `9385cc12097e311ba566e639a299af4626b05dbb21ff8740824aaf74fd007f09`:
  attached Float32 RGB with astrometric metadata blocks.

These private files are used only as black-box inputs. They are not copied into
the repository and do not broaden the normative specification baseline.

## Redistributable independent-producer evidence

Five compact fixtures under `tests/interop` were generated from Martian
Medusa-owned deterministic numeric arrays by invoking only the documented
public `XISF.write` API of PyPI package `xisf` 0.9.7. The upstream source archive
SHA-256 is
`fcc8d33b3c45461abb0d71b3cd1207c08548ee11f51d4aaa5615f19cc54e1724`.
No implementation source was inspected. The GPLv3 package is not a dependency;
only our generated, non-astronomy test data is retained under the repository
license. Exact per-file provenance is recorded in
`tests/interop/manifest.json`.

## Ecosystem reconnaissance

An existing GPLv3+ C++ project already uses the `libXISF` name and is packaged
by Debian. This evidence is used only for naming, scope comparison, and license
risk awareness:

- <https://gitea.nouspiro.space/nou/libXISF>
- <https://sources.debian.org/src/libxisf/>

No source code from that implementation may be copied, translated, or used as
an implementation template. PixInsight/PCL implementation code is likewise out
of bounds. Independently generated files may be used as black-box
interoperability evidence when their provenance and redistribution rights are
recorded.

## Required clean-room record

Before implementation begins, add a log containing:

- contributor name and role;
- normative documents and exact revisions consulted;
- ambiguities and first-party clarifications;
- external fixtures, hashes, generator versions, and permissions;
- explicit confirmation that restricted implementation sources were not used;
- reviewer sign-off for each conformance milestone.
