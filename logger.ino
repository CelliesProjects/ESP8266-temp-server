void logToSPIFFS( const time_t now )
{
  struct tm          timeinfo;
  char           fileName[17];
  char            content[60];

  localtime_r( &now, &timeinfo );
  strftime( fileName , sizeof( fileName ), "/%F.log", &timeinfo );

  Serial.printf(  "Current logfile: %s\n", fileName );

  snprintf( content, sizeof( content ), "%i,%3.2f", now - DST_SEC - TZ_SEC, currentTemp );

  if ( !writelnFile( SPIFFS, fileName, content ) )
  {
    Serial.println( F( "Something wrong writing to file" ) );
  }

  Serial.println();
  Serial.println( F( "Files on spiffs:" ) );
  Dir dir = SPIFFS.openDir( "/" );
  while ( dir.next() ) {
    Serial.print( dir.fileName() );
    Serial.print( F( " size: " ) );
    File f = dir.openFile( "r" );
    Serial.println( f.size() );
  }
  Serial.println();
}

bool writelnFile( fs::FS &fs, const char * path, const char * message )
{
  File file = fs.open( path, "a+" );
  if ( !file )
  {
    Serial.println( F( "file could not be opened" ) );
    return false;
  }
  if ( !file.println( message ) )
  {
    Serial.println( F( "file could not be written to" ) );
    file.close();
    return false;
  }
  file.close();
  return true;
}
