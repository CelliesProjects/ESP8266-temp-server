#include <OneWire.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "index_htm.h"

#define ONE_WIRE_BUS              D1
#define BLINK_LED_ON_REQUEST      true

const char * WIFISSID =           "huiskamer";
const char * WIFIPSK =            "0987654321";

AsyncWebServer server(80);

float currentTemp;

OneWire  ds( ONE_WIRE_BUS ); // (a 4.7K resistor is necessary)

void setup(void) {
  Serial.begin( 115200 );
  pinMode( BUILTIN_LED, OUTPUT );
  digitalWrite( BUILTIN_LED, LOW );

  WiFi.persistent( false );
  Serial.println( "Connecting..." );
  WiFi.mode( WIFI_STA );
  WiFi.begin( WIFISSID, WIFIPSK );
  unsigned long timeout = millis() + 15000;
  while ( (long)( millis() - timeout ) < 0 && WiFi.status() != WL_CONNECTED )
  {
    delay ( 500 );
    Serial.print ( F( "." ) );
  }
  Serial.println();

  if ( WiFi.status() != WL_CONNECTED )
  {
    Serial.println( "No WiFi!" );
    while ( true )
    {
      digitalWrite( BUILTIN_LED, LOW );
      delay( 100 );
      digitalWrite( BUILTIN_LED, HIGH );
      delay( 100 );
    }
  }
  else
  {
    Serial.println( "Connected!" );
    Serial.println( WiFi.localIP().toString() );
    digitalWrite( BUILTIN_LED, HIGH );
  }

  static const char * HTML_HEADER = "text/html";

  server.on( "/", HTTP_GET, [] ( AsyncWebServerRequest * request )
  {
    AsyncWebServerResponse *response = request->beginResponse_P( 200, HTML_HEADER, index_htm, index_htm_len );
    request->send(response);
  });

  server.on( "/data", HTTP_GET, [] ( AsyncWebServerRequest * request )
  {
    if ( BLINK_LED_ON_REQUEST ) digitalWrite( BUILTIN_LED, LOW );
    AsyncResponseStream *response = request->beginResponseStream( HTML_HEADER );
    response->printf( "%.1f%&deg;C", currentTemp );
    request->send( response );
    if ( BLINK_LED_ON_REQUEST ) digitalWrite( BUILTIN_LED, HIGH );
  });

  server.onNotFound( []( AsyncWebServerRequest * request )
  {
    Serial.printf( "Not found http://%s%s\n", request->host().c_str(), request->url().c_str());
    request->send( 404 );
  });
  DefaultHeaders::Instance().addHeader( "Access-Control-Allow-Origin", "*" );
  server.begin();
}

void loop(void) {
  byte i;
  byte type_s;
  byte data[12];
  byte addr[8];

  if ( !ds.search(addr)) {
    ds.reset_search();
    return;
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
    Serial.println("Sensor CRC is not valid!");
    return;
  }

  // the first ROM byte indicates which chip
  switch (addr[0]) {
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
      return;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);

  delay(750);

  ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad

  for ( i = 0; i < 9; i++) data[i] = ds.read();

  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  currentTemp = (float)raw / 16.0;
}
