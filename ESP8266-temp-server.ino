#include <OneWire.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <list>

#include "index_htm.h"
#include "logs_htm.h"
#include "wifi_credentials.h"           // change your WiFi settings in this file

#define ONE_WIRE_BUS              D1
#define ONBOARD_LED               D4
#define SAVED_LOGFILES            30
#define SENSOR_ERROR              -273.15

#define TZ              1       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)


float currentTemp = SENSOR_ERROR;
bool dstStatus = true;

OneWire  ds( ONE_WIRE_BUS ); // (a 4.7K resistor is necessary)

void setup(void)
{
  Serial.begin( 115200 );
  Serial.println();

  configTime( TZ_SEC, DST_SEC, "nl.pool.ntp.org" );

  WiFi.mode( WIFI_STA );
  if ( !connectWifi() )
  {
    Serial.println( "No WiFi! Check 'wifi_credentials.h'" );
    delay(100);
    Serial.end();
    pinMode( ONBOARD_LED, OUTPUT );
    while ( true )
    {
      digitalWrite( ONBOARD_LED, LOW );
      delay( 100 );
      digitalWrite( ONBOARD_LED, HIGH );
      delay( 100 );
    }
  }

  Serial.println( WiFi.localIP().toString() );

  static AsyncWebServer server(80);
  static const char * HTML_HEADER = "text/html";

  server.on( "/", HTTP_GET, [] ( AsyncWebServerRequest * request )
  {
    AsyncWebServerResponse *response = request->beginResponse_P( 200, HTML_HEADER, index_htm, index_htm_len );
    request->send(response);
  });


  server.on( "/data", HTTP_GET, [] ( AsyncWebServerRequest * request )
  {
    if ( currentTemp == SENSOR_ERROR )
    {
      request->send( 200, HTML_HEADER, F( "<p style=\"font-size:100px;color:red;\">SENSOR ERROR</p>" ) );
    }
    else
    {
      AsyncResponseStream *response = request->beginResponseStream( HTML_HEADER );
      response->printf( "%.1f&deg;C", currentTemp );
      request->send( response );
    }
  });

  server.on( "/logs", HTTP_GET, [] ( AsyncWebServerRequest * request )
  {
    AsyncWebServerResponse *response = request->beginResponse_P( 200, HTML_HEADER, logs_htm, logs_htm_len );
    request->send(response);
  });

  server.on( "/files", HTTP_GET, [] ( AsyncWebServerRequest * request )
  {
    AsyncResponseStream *response = request->beginResponseStream( HTML_HEADER );
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) response->printf( "%s\n", dir.fileName().c_str() );
    request->send( response );
  });

  server.on( "/dststatus", HTTP_GET, [] ( AsyncWebServerRequest * request )
  {
    if ( request->hasArg( "dst" ) )
    {
      if ( request->arg( "dst" ) == "on" ) dstStatus = true;
      else if ( request->arg( "dst" ) == "off" ) dstStatus = false;
    }
    AsyncResponseStream *response = request->beginResponseStream( HTML_HEADER );
    response->printf( "DST is %s", dstStatus ? "on" : "off" );
    request->send( response );
  });

  server.serveStatic( "/", SPIFFS, "/" );

  server.onNotFound( []( AsyncWebServerRequest * request )
  {
    request->send( 404 );
  });

  DefaultHeaders::Instance().addHeader( "Access-Control-Allow-Origin", "*" );
  server.begin();

  time_t now;
  struct tm timeinfo;

  while ( timeinfo.tm_year < ( 2016 - 1900 ) ) {
    delay(50);
    time( &now );
    localtime_r( &now, &timeinfo );
  }
  Serial.print( F( "Time synced at " ) );
  Serial.println( asctime( localtime( &now ) ) );

  if ( !SPIFFS.begin() )
    Serial.println( F( "SPIFFS could not be mounted" ) );
}

time_t nextLogTime = 0;
const time_t logDelaySeconds = 120;

void loop(void)
{
  byte addr[8];

  if ( !ds.search(addr))
  {
    ds.reset_search();
    return;
  }

  if (OneWire::crc8(addr, 7) != addr[7])
  {
    Serial.println( F( "Sensor CRC is not valid!" ) );
    return;
  }

  byte type_s;

  switch (addr[0])
  {
    case 0x10:
      type_s = 1;
      break;
    case 0x28:
      type_s = 0;
      break;
    case 0x22:
      type_s = 0;
      break;
    default:
      /*OneWire device is not a DS18B20 sensor*/
      return;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);

  delay(750);

  ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad

  byte i;
  byte data[12];

  for ( i = 0; i < 9; i++) data[i] = ds.read();

  int16_t raw = (data[1] << 8) | data[0];
  if (type_s)
  {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10)
    {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  }
  else
  {
    byte cfg = (data[4] & 0x60);
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  currentTemp = (float)raw / 16.0;

  time_t now;
  time( &now );
  if ( now >= nextLogTime )
  {
    logToSPIFFS( now );
    nextLogTime = now + logDelaySeconds;
  }
  if ( !WiFi.isConnected() )
  {
    Serial.println( F( "WiFi is disconnected." ) );
    if ( !connectWifi() ) return;
    Serial.println( F( "WiFi just reconnected" ) );
  }
}

bool connectWifi()
{
  int netWorks = WiFi.scanNetworks();

  if ( netWorks )
  {
    for ( int i = 0; i < netWorks; ++i )
    {
      if ( WiFi.SSID(i) == WIFISSID )
      {
        Serial.print( F( "Found " ) );
        Serial.print( WiFi.SSID(i) );
        Serial.print( " " );
        Serial.print( WiFi.RSSI(i) );
        Serial.println( F( "dB" ) );
        WiFi.persistent( false );
        Serial.print( F( "Connecting" ) );
        WiFi.disconnect();
        WiFi.mode( WIFI_STA );
        WiFi.begin( WIFISSID, WIFIPSK );
        unsigned long timeout = millis() + 15000;
        while ( (long)( millis() - timeout ) < 0 && ( WiFi.status() != WL_CONNECTED ) )
        {
          delay ( 500 );
          Serial.print ( F( "." ) );
        }
        Serial.println();
      }
    }
  }
  return WiFi.isConnected();
}

