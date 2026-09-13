# Sources and clean-room policy

## Normative and first-party sources

- XISF 1.0 specification: <https://pixinsight.com/doc/docs/XISF-1.0-spec/XISF-1.0-spec.html>
- PixInsight XISF landing page: <https://pixinsight.com/xisf/>
- PixInsight staff clarification of monolithic attached blocks and header-driven
  layout: <https://pixinsight.com/forum/index.php?threads/questions-on-xisf-format.14154/>
- PixInsight staff clarification of XISF left-to-right, top-to-bottom pixel
  order: <https://pixinsight.com/forum/index.php?threads/astrometric-solution-is-written-flipped-to-fits-and-xisf-file.20446/>

The official specification page was discoverable but rejected automated fetches
during repository creation. M0 therefore must obtain and identify the exact
revision before freezing the conformance matrix. This plan does not claim that
its provisional feature list is the normative matrix.

## Local PFI evidence inspected

- `software/PFI-worktrees/cpp-parity-foundation/src/platform/pixinsight/batch_input.js`
- `software/PFI-worktrees/cpp-parity-foundation/src/core/measurement/frame_metadata.js`
- `software/PFI-worktrees/cpp-parity-foundation/docs/cpp/BUILD_AND_VALIDATION_AUDIT.md`
- `software/PFI-worktrees/cpp-parity-foundation/docs/cpp/CAPABILITY_MAP.md`

These files establish PFI's current adapter needs; they are not XISF normative
sources.

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
