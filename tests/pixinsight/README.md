# PixInsight native validation

Computer Use is intentionally excluded. The scripts can use a manual chooser or
an absolute-path-bound automation variant in a disposable PixInsight instance.
They create source-bound JSON evidence without changing PFI or PixInsight
settings.

## Writer block-Property fixture

Materialize the committed fixture locally:

```sh
mkdir -p artifacts/native-validation
base64 --decode -i tests/interop/mmxisf-writer-native-properties.xisf.b64 \
  -o artifacts/native-validation/mmxisf-writer-native-properties.xisf
shasum -a 256 artifacts/native-validation/mmxisf-writer-native-properties.xisf
```

The required source SHA-256 is
`e4cff5baa69d4cb952d95b1299fff63a3d6f24e2456a8546088487effdf8866d`.
In PixInsight, run:

```text
run -x=auto "/absolute/path/to/tests/pixinsight/MMXISFWriterNativeValidation.js"
```

Select the materialized XISF and an evidence directory. The script requires
exact native recovery of the 2x2 `F64Matrix` values `[[1,2],[3,4]]`, the
length-2 `UI16Vector` values `[513,1027]`, the String Property, UInt16 Gray
geometry, and working-sample pixel SHA-256. It rehashes the source after pixel
access, closes every opened image window, and records explicit authority limits.
A PASS is writer/property interoperability evidence only; it does not establish
PFI scientific parity or authorize product enablement.

For a noninteractive local run, generate an external script with absolute paths
and launch a new disposable PixInsight instance:

```sh
node tools/build_pixinsight_writer_validation.mjs \
  --output /absolute/path/to/run/MMXISFWriterNativeValidationAutomated.js \
  --fixture /absolute/path/to/mmxisf-writer-native-properties.xisf \
  --evidence-output /absolute/path/to/run/writer-native.json
/Applications/PixInsight/PixInsight.app/Contents/MacOS/PixInsight \
  --new --automation-mode --no-startup-scripts \
  --run=/absolute/path/to/run/MMXISFWriterNativeValidationAutomated.js \
  --force-exit
```

The builder rejects relative fixture/evidence paths and preserves the manual
script as the source of truth. The evidence path is create-only. On 2026-09-14,
this automation passed the predecessor fixture in PixInsight 1.9.4 arm64; the
sanitized result is retained in
`docs/quality-runs/2026-09-14-pixinsight-writer-properties-macos-arm64.json`.
The current Revision 1 fixture changes only the same-length `Image/@id` from
`native-validation` to `native_validation`, but its new source identity still
requires a fresh native PASS before release qualification.

## Revision 1 global-shuffle subblocks

Materialize the dedicated eight-subblock writer fixture:

```sh
mkdir -p artifacts/native-validation
base64 --decode -i tests/interop/mmxisf-writer-revision1-subblocks.xisf.b64 \
  -o artifacts/native-validation/mmxisf-writer-revision1-subblocks.xisf
shasum -a 256 \
  artifacts/native-validation/mmxisf-writer-revision1-subblocks.xisf
```

The required source SHA-256 is
`cb247d96a3b475d3189809f760552b41534f9b855df798e9e924db14f1c04f64`.
In PixInsight, run:

```text
run -x=auto "/absolute/path/to/tests/pixinsight/MMXISFRevision1SubblocksValidation.js"
```

Select the materialized XISF and an evidence directory. The script requires a
16x16 UInt16 Gray image, all 256 exact samples, and working-sample pixel
SHA-256
`3f662f7f1bde036f2bf11f11a30139b4226f8ebe941314eeda8425f5af190317`.
The source descriptor is bound to `zstd+sh:512:2` and eight independently
compressed subblocks. A PASS establishes native PixInsight acceptance of this
specific corrected Revision 1 writer path; it does not establish PFI parity or
authorize product enablement.
