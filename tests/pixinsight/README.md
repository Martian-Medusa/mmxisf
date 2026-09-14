# Manual PixInsight validation

Computer Use is intentionally excluded. These scripts are run manually by an
operator in PixInsight and create source-bound JSON evidence without changing
PFI or PixInsight settings.

## Writer block-Property fixture

Materialize the committed fixture locally:

```sh
mkdir -p artifacts/native-validation
base64 --decode tests/interop/mmxisf-writer-native-properties.xisf.b64 \
  > artifacts/native-validation/mmxisf-writer-native-properties.xisf
shasum -a 256 artifacts/native-validation/mmxisf-writer-native-properties.xisf
```

The required source SHA-256 is
`b130c2a3b65180b1bf31b64e82bf82740fd105ba8cadda4d91d4355d4a6ea7b6`.
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
