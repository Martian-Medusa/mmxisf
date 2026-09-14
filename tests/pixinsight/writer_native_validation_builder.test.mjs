import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

import { buildPixInsightWriterValidation } from
  "../../tools/build_pixinsight_writer_validation.mjs";

test( "writer native validator binds absolute automation paths", async () => {
  const directory = await mkdtemp( path.join( os.tmpdir(),
    "mmxisf-writer-native-builder-" ) );
  const output = path.join( directory, "validation.js" );
  const evidenceOutput = path.join( directory, "evidence.json" );
  const built = await buildPixInsightWriterValidation( output, {
    fixturePath: "/data/mmxisf-writer-native-properties.xisf",
    evidenceOutput
  } );
  const source = await readFile( output, "utf8" );
  assert.match( source,
    /MMXISF_WRITER_NATIVE_VALIDATION_AUTOMATION = \{/ );
  assert.match( source,
    /"fixturePath": "\/data\/mmxisf-writer-native-properties\.xisf"/ );
  assert.match( source, new RegExp( JSON.stringify( evidenceOutput ).replace(
    /[.*+?^${}()|[\]\\]/g, "\\$&" ) ) );
  assert.doesNotMatch( source,
    /MMXISF_WRITER_NATIVE_VALIDATION_AUTOMATION = null/ );
  const syntaxPath = path.join( directory, "syntax.js" );
  await writeFile( syntaxPath, source.replace( /^#engine[^\n]*\n/, "" ) );
  execFileSync( process.execPath, [ "--check", syntaxPath ] );
  const sidecar = await readFile( output + ".sha256", "utf8" );
  assert.equal( sidecar, `${built.sha256}  validation.js\n` );
} );

test( "writer native validator rejects relative automation paths", async () => {
  const directory = await mkdtemp( path.join( os.tmpdir(),
    "mmxisf-writer-native-builder-invalid-" ) );
  const output = path.join( directory, "validation.js" );
  await assert.rejects( () => buildPixInsightWriterValidation( output, {
    fixturePath: "fixture.xisf",
    evidenceOutput: path.join( directory, "evidence.json" )
  } ), /absolute fixture path/ );
  await assert.rejects( () => buildPixInsightWriterValidation( output, {
    fixturePath: "/data/fixture.xisf",
    evidenceOutput: "evidence.json"
  } ), /absolute evidence output path/ );
} );
