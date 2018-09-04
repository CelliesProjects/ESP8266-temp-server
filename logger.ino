void logToSPIFFS( const time_t now )
{
  deleteOldLogFiles();

  struct tm          timeinfo;
  char           fileName[17];
  char            content[60];

  localtime_r( &now, &timeinfo );
  strftime( fileName , sizeof( fileName ), "/%F.log", &timeinfo );

  snprintf( content, sizeof( content ), "%i,%3.2f", now - DST_SEC - TZ_SEC, currentTemp );

  if ( !writelnFile( SPIFFS, fileName, content ) )
  {
    Serial.println( F( "Something wrong writing to file" ) );
  }
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

void deleteOldLogFiles()
{
  std::list<String> logFiles;

  Dir dir = SPIFFS.openDir( F( "/" ) );
  while ( dir.next() )
  {
    if ( dir.fileName().endsWith( F( ".log" ) ) )
    {
      logFiles.push_back( dir.fileName() );
    }
  }

  if ( logFiles.size() > SAVED_LOGFILES )
  {
    logFiles.sort();
  }

  while ( logFiles.size() > SAVED_LOGFILES )
  {
    std::list<String>::iterator thisFile;

    thisFile = logFiles.begin();

    String filename = *thisFile;

    SPIFFS.remove( filename.c_str() );
    logFiles.erase( thisFile );
  }
}

