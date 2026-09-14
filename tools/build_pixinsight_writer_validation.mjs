import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repositoryRoot = path.resolve(
  path.dirname( fileURLToPath( import.meta.url ) ), ".."
);
const sourcePath = path.join( repositoryRoot, "tests", "pixinsight",
  "MMXISFWriterNativeValidation.js" );
const placeholder =
  "const MMXISF_WRITER_NATIVE_VALIDATION_AUTOMATION = null;";

const sha256 = bytes => createHash( "sha256" ).update( bytes ).digest( "hex" );

function validateAutomation( automation )
{
  if ( !automation || typeof automation.fixturePath !== "string" ||
       !path.isAbsolute( automation.fixturePath ) )
    throw new Error( "Automation requires an absolute fixture path." );
  if ( typeof automation.evidenceOutput !== "string" ||
       !path.isAbsolute( automation.evidenceOutput ) )
    throw new Error( "Automation requires an absolute evidence output path." );
  return {
    fixturePath: automation.fixturePath,
    evidenceOutput: automation.evidenceOutput
  };
}

export async function buildPixInsightWriterValidation( outputPath, automation )
{
  if ( typeof outputPath !== "string" || outputPath.length === 0 )
    throw new Error( "An output script path is required." );
  const config = validateAutomation( automation );
  const source = await readFile( sourcePath, "utf8" );
  const first = source.indexOf( placeholder );
  if ( first < 0 || source.indexOf( placeholder, first + 1 ) >= 0 )
    throw new Error( "Writer validation automation placeholder is invalid." );
  const injected = source.replace( placeholder,
    "const MMXISF_WRITER_NATIVE_VALIDATION_AUTOMATION = " +
      JSON.stringify( config, null, 3 ) + ";" );
  const bytes = Buffer.from( injected );
  const resolvedOutput = path.resolve( outputPath );
  await mkdir( path.dirname( resolvedOutput ), { recursive: true } );
  await writeFile( resolvedOutput, bytes );
  const digest = sha256( bytes );
  await writeFile( resolvedOutput + ".sha256",
    `${digest}  ${path.basename( resolvedOutput )}\n` );
  return {
    outputPath: resolvedOutput,
    sizeBytes: bytes.length,
    sha256: digest,
    sourcePath,
    sourceSha256: sha256( Buffer.from( source ) )
  };
}

if ( process.argv[1] && import.meta.url === pathToFileURL( process.argv[1] ).href )
{
  const args = process.argv.slice( 2 );
  const value = name => {
    const index = args.indexOf( name );
    if ( index < 0 || !args[index + 1] || args[index + 1].startsWith( "--" ) )
      throw new Error( `Missing ${name} value.` );
    return args[index + 1];
  };
  const outputPath = path.resolve( value( "--output" ) );
  const built = await buildPixInsightWriterValidation( outputPath, {
    fixturePath: value( "--fixture" ),
    evidenceOutput: value( "--evidence-output" )
  } );
  console.log( `Built ${built.outputPath} (${built.sizeBytes} bytes)` );
  console.log( `SHA-256 ${built.sha256}` );
  console.log( `Source SHA-256 ${built.sourceSha256}` );
}
