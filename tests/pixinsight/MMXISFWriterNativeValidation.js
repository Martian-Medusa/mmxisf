#engine v8

// Manual native evidence tool. It does not change PixInsight or PFI settings.
(function ()
{
   "use strict";

   const EXPECTED_SOURCE_SIZE = 12292;
   const EXPECTED_SOURCE_SHA256 =
      "b130c2a3b65180b1bf31b64e82bf82740fd105ba8cadda4d91d4355d4a6ea7b6";
   const EXPECTED_PIXEL_SHA256 =
      "ea99f710d9d0b8ba192295c969a63ed7ce8fc5743da20d2057fa2b6d2c404bfb";

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

   function plainMatrix( value )
   {
      if ( !value || value.rows !== 2 || value.columns !== 2 )
         throw new Error( "Test:Matrix is not a 2x2 native Matrix." );
      let result = [];
      for ( let row = 0; row < 2; ++row )
      {
         let values = [];
         for ( let column = 0; column < 2; ++column )
            values.push( value.at( row, column ) );
         result.push( values );
      }
      return result;
   }

   function plainVector( value )
   {
      if ( !value || typeof value.length !== "number" || value.length !== 2 )
         throw new Error( "Test:Vector is not a length-2 native vector." );
      let result = [];
      for ( let index = 0; index < value.length; ++index )
      {
         let item = value[index];
         if ( typeof item === "undefined" && typeof value.at === "function" )
            item = value.at( index );
         result.push( item );
      }
      return result;
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

      let extension = File.extractExtension( filePath ).toLowerCase();
      let format = new FileFormat( extension, true, false );
      if ( format.isNull ) throw new Error( "No XISF reader is installed." );
      let instance = new FileFormatInstance( format );
      if ( instance.isNull ) throw new Error( "Could not create XISF reader." );
      let properties = {};
      let description = null;
      try
      {
         let descriptions = instance.open( filePath, "verbosity 0" );
         if ( !descriptions || descriptions.length !== 1 )
            throw new Error( "Expected exactly one image description." );
         description = descriptions[0];
         for ( let property of instance.imageProperties )
            properties[property[0]] = instance.readImageProperty( property[0] );
      }
      finally
      {
         instance.close();
      }

      let matrix = plainMatrix( properties["Test:Matrix"] );
      let vector = plainVector( properties["Test:Vector"] );
      let label = properties["Test:Label"];
      if ( !same( matrix, [ [ 1, 2 ], [ 3, 4 ] ] ) )
         throw new Error( "Test:Matrix values differ." );
      if ( !same( vector, [ 513, 1027 ] ) )
         throw new Error( "Test:Vector values differ." );
      if ( label !== "mmxisf native writer validation" )
         throw new Error( "Test:Label differs." );

      let windows = [];
      try
      {
         windows = ImageWindow.open( filePath );
         if ( windows.length !== 1 || windows[0] === null || windows[0].isNull )
            throw new Error( "PixInsight did not open exactly one image." );
         let image = windows[0].mainView.image;
         if ( image.width !== 2 || image.height !== 2 ||
              image.numberOfChannels !== 1 || image.bitsPerSample !== 16 ||
              image.isReal || image.isComplex || image.isColor )
            throw new Error( "Native image representation differs." );
         let samples = new Uint16Array( 4 );
         image.getSamples( samples, new Rect( 0, 0, 2, 2 ), 0 );
         let bytes = new Uint8Array( samples.buffer );
         let pixelHash = new CryptographicHash( CryptographicHash.SHA256 );
         pixelHash.update( bytes );
         let pixelSha256 = pixelHash.finalize().toHex().toLowerCase();
         if ( pixelSha256 !== EXPECTED_PIXEL_SHA256 )
            throw new Error( "Native pixel bytes differ." );
         let finalSource = fileIdentity( filePath );
         if ( !same( source, finalSource ) )
            throw new Error( "Source changed during validation." );
         return { status: "PASS", source: source,
            image: { width: 2, height: 2, channels: 1,
               sampleFormat: "UInt16", colorSpace: "Gray",
               pixelSha256: pixelSha256 },
            properties: { "Test:Matrix": matrix, "Test:Vector": vector,
               "Test:Label": label } };
      }
      finally
      {
         for ( let window of windows )
            if ( window !== null && !window.isNull ) window.forceClose();
      }
   }

   console.show();
   let input = new OpenFileDialog;
   input.caption = "mmxisf writer native validation - select fixture";
   input.filters = [ [ "XISF files", ".xisf" ] ];
   if ( !input.execute() ) return;
   let output = new GetDirectoryDialog;
   output.caption = "mmxisf writer native validation - evidence directory";
   if ( !output.execute() ) return;

   let evidence = { schemaVersion: "mmxisf.writer-native-validation/1.0.0",
      runtime: runtimeIdentity(), fixturePath: input.fileName,
      result: null, authority: { writerInterop: "OBSERVED_NATIVE_HOST",
         pfiScientificParity: "NOT_ASSESSED", productEnablement: "NOT_AUTHORIZED" } };
   try
   {
      evidence.result = capture( input.fileName );
      console.writeln( "mmxisf writer native validation: PASS" );
   }
   catch ( error )
   {
      evidence.result = { status: "FAIL", diagnostic: String( error ) };
      console.criticalln( "mmxisf writer native validation: FAIL: " + error );
   }

   let stamp = (new Date).toISOString().replace( /[-:]/g, "" )
      .replace( /\.\d+Z$/, "Z" );
   let separator = output.directoryPath.endsWith( "/" ) ? "" : "/";
   let outputPath = output.directoryPath + separator +
      "mmxisf-writer-native-validation-" + stamp + ".json";
   if ( File.exists( outputPath ) )
      throw new Error( "Refusing to overwrite existing evidence." );
   File.writeTextFile( outputPath, JSON.stringify( evidence, null, 2 ) + "\n" );
   console.writeln( "Evidence written: " + outputPath );
})();
