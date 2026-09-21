#engine v8

// Manual native evidence tool. It does not change PixInsight or PFI settings.
const MMXISF_REVISION1_SUBBLOCKS_AUTOMATION = null;

(function ()
{
   "use strict";

   const EXPECTED_SOURCE_SIZE = 4488;
   const EXPECTED_SOURCE_SHA256 =
      "cb247d96a3b475d3189809f760552b41534f9b855df798e9e924db14f1c04f64";
   const EXPECTED_PIXEL_SHA256 =
      "3f662f7f1bde036f2bf11f11a30139b4226f8ebe941314eeda8425f5af190317";

   function fileIdentity( filePath )
   {
      let file = new File;
      let hash = new CryptographicHash( CryptographicHash.SHA256 );
      try
      {
         file.openForReading( filePath );
         while ( file.position < file.size )
         {
            if ( console.abortRequested ) throw new Error( "Cancelled" );
            let count = Math.min( 1024*1024, file.size - file.position );
            let bytes = file.read( DataType.ByteArray, count );
            if ( bytes.length !== count ) throw new Error( "Short source read." );
            hash.update( bytes );
         }
         return { sizeBytes: file.size,
            sha256: hash.finalize().toHex().toLowerCase() };
      }
      finally
      {
         if ( file.isOpen ) file.close();
      }
   }

   function same( actual, expected )
   {
      return JSON.stringify( actual ) === JSON.stringify( expected );
   }

   function runtimeIdentity()
   {
      return { programName: CoreApplication.programName,
         version: String( CoreApplication.versionMajor ) + "." +
            String( CoreApplication.versionMinor ) + "." +
            String( CoreApplication.versionRelease ) + "." +
            String( CoreApplication.versionRevision ),
         platform: CoreApplication.platform, engine: "PJSR V8" };
   }

   function capture( filePath )
   {
      let source = fileIdentity( filePath );
      if ( source.sizeBytes !== EXPECTED_SOURCE_SIZE ||
           source.sha256 !== EXPECTED_SOURCE_SHA256 )
         throw new Error( "Selected file is not the source-bound fixture." );

      let windows = [];
      try
      {
         windows = ImageWindow.open( filePath );
         if ( windows.length !== 1 || windows[0] === null || windows[0].isNull )
            throw new Error( "PixInsight did not open exactly one image." );
         let image = windows[0].mainView.image;
         if ( image.width !== 16 || image.height !== 16 ||
              image.numberOfChannels !== 1 || image.bitsPerSample !== 16 ||
              image.isReal || image.isComplex || image.isColor )
            throw new Error( "Native image representation differs." );

         let samples = new Uint16Array( 256 );
         image.getSamples( samples, new Rect( 0, 0, 16, 16 ), 0 );
         for ( let index = 0; index < samples.length; ++index )
         {
            let expected = (index % 32)*127;
            if ( samples[index] !== expected )
               throw new Error( "Native pixel differs at index " + index + "." );
         }
         let pixelHash = new CryptographicHash( CryptographicHash.SHA256 );
         pixelHash.update( new Uint8Array( samples.buffer ) );
         let pixelSha256 = pixelHash.finalize().toHex().toLowerCase();
         if ( pixelSha256 !== EXPECTED_PIXEL_SHA256 )
            throw new Error( "Native pixel bytes differ." );

         let finalSource = fileIdentity( filePath );
         if ( !same( source, finalSource ) )
            throw new Error( "Source changed during validation." );
         return { status: "PASS", source: source,
            image: { width: 16, height: 16, channels: 1,
               sampleFormat: "UInt16", colorSpace: "Gray",
               pixelSha256: pixelSha256, exactSamplePattern: "(index%32)*127" },
            exercisedDescriptor: {
               compression: "zstd+sh:512:2", subblockCount: 8,
               decompressedBytesPerSubblock: 64 } };
      }
      finally
      {
         for ( let window of windows )
            if ( window !== null && !window.isNull ) window.forceClose();
      }
   }

   console.show();
   let fixturePath = null;
   let outputPath = null;
   if ( MMXISF_REVISION1_SUBBLOCKS_AUTOMATION !== null )
   {
      fixturePath = MMXISF_REVISION1_SUBBLOCKS_AUTOMATION.fixturePath;
      outputPath = MMXISF_REVISION1_SUBBLOCKS_AUTOMATION.evidenceOutput;
   }
   else
   {
      let input = new OpenFileDialog;
      input.caption = "mmxisf Revision 1 subblocks - select fixture";
      input.filters = [ [ "XISF files", ".xisf" ] ];
      if ( !input.execute() ) return;
      fixturePath = input.fileName;
      let output = new GetDirectoryDialog;
      output.caption = "mmxisf Revision 1 subblocks - evidence directory";
      if ( !output.execute() ) return;
      let stamp = (new Date).toISOString().replace( /[-:]/g, "" )
         .replace( /\.\d+Z$/, "Z" );
      let separator = output.directoryPath.endsWith( "/" ) ? "" : "/";
      outputPath = output.directoryPath + separator +
         "mmxisf-revision1-subblocks-validation-" + stamp + ".json";
   }

   let evidence = { schemaVersion: "mmxisf.revision1-subblocks-validation/1.0.0",
      runtime: runtimeIdentity(), fixturePath: fixturePath,
      result: null, authority: { revision1WriterInterop: "OBSERVED_NATIVE_HOST",
         pfiScientificParity: "NOT_ASSESSED", productEnablement: "NOT_AUTHORIZED" } };
   try
   {
      evidence.result = capture( fixturePath );
      console.writeln( "mmxisf Revision 1 subblocks validation: PASS" );
   }
   catch ( error )
   {
      evidence.result = { status: "FAIL", diagnostic: String( error ) };
      console.criticalln( "mmxisf Revision 1 subblocks validation: FAIL: " + error );
   }

   if ( File.exists( outputPath ) )
      throw new Error( "Refusing to overwrite existing evidence." );
   File.writeTextFile( outputPath, JSON.stringify( evidence, null, 2 ) + "\n" );
   console.writeln( "Evidence written: " + outputPath );
})();
