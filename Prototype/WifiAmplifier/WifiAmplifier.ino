/*
 * WifiAmplifier: propagate an audio signal into multiple amplified channels controllable over wifi.  The ESP8266 runs a web server that sends up to 8 i2c commands to control up to 8 amplifiers.
 * https://github.com/justingogas/WifiAmplifier
 *
 * Components:
 *  Adafruit HUZZAH: https://www.adafruit.com/product/2471
 *  Adafruit i2c multiplexer: https://www.adafruit.com/product/2717
 *  Adafruit 20w stereo audio max9744 class 2 amplifier (x2): https://www.adafruit.com/product/1752
 *
 * Credits:
 *  ESP8266 Bootstrap: https://gist.github.com/JasonPellerin/5e7b305cb244fa9ecbca
 *  Adafruit amplifier: https://learn.adafruit.com/adafruit-20w-stereo-audio-amplifier-class-d-max9744/
 *  Adafruit HUZZAH: https://learn.adafruit.com/adafruit-huzzah-esp8266-breakout
 *  Adafruit i2c multiplexer: https://learn.adafruit.com/adafruit-tca9548a-1-to-8-i2c-multiplexer-breakout
 *
 * Connection:
 *   GPIO 4 = i2c SDA
 *   GPIO 5 = i2c SCL
 *   3.3v = multiplexer Vin, amplifier Vi2c
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

// i2c multiplexer values.
#include "Wire.h"
extern "C" { 
  #include "twi.h"  // from Wire library, so we can do bus scanning
}

#define TCAADDR 0x70
#define SDAPIN 4
#define SCLPIN 5
#define DEFAULTVOLUME 31
#define MINVOLUME 0
#define MAXVOLUME 63
#define MINMAPPEDVOLUME 0
#define MAXMAPPEDVOLUME 100
#define MINAMPLIFIERS 0
#define MAXAMPLIFIERS 8

// Define the channels as an array of structures.
typedef struct {
  boolean active = false;
  uint8_t volume = 0;
  String name = "";  
} channel;

channel channels[MAXAMPLIFIERS];


// HTTP server will listen at port 80.
ESP8266WebServer server(80);

// WiFi credentials.
// TODO: read ssid and password from EEPROM and run an access point to allow for non-hard-coded configuration.  http://www.john-lassen.de/en/projects/esp-8266-arduino-ide-webconfig
const char* ssid     = "yourSSID";
const char* password = "yourpasswd";

// Web page components.
String header        = "<!DOCTYPE html><html lang='en'><head> <meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>WiFi Amplifier</title><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css'><script src='https://ajax.googleapis.com/ajax/libs/jquery/1.12.4/jquery.min.js'></script><script src='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js'></script></head>";
String body_open     = "<body style='padding-top: 70px;'><nav class='navbar navbar-inverse navbar-fixed-top'><div class='container-fluid'><div class='navbar-header'><button type='button' class='navbar-toggle collapsed' data-toggle='collapse' data-target='#navbar' aria-expanded='false' aria-controls='navbar'><span class='sr-only'>Toggle navigation</span><span class='icon-bar'></span><span class='icon-bar'></span><span class='icon-bar'></span></button><a class='navbar-brand' href='#'>WiFi Amplifier</a></div> <div id='navbar' class='collapse navbar-collapse'><ul class='nav navbar-nav'><li class='active'><a href='#'>Channels</a></li><li><a href='#network'>Network</a></li><li><a href='./muteall'>Mute all</a></li><li><a href='./maxall'>Max all</a></li></ul></div></div></nav><div class='container-fluid'>";
String body_close    = "</div><script src='https://ajax.googleapis.com/ajax/libs/jquery/1.12.4/jquery.min.js'></script><script src='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js'></script><script src='https://cdnjs.cloudflare.com/ajax/libs/bootstrap-slider/9.5.4/bootstrap-slider.min.js'></script></body></html>";

String list_open     = "<ul class='list-group'>";
String list_close    = "</ul>";

String list_item     = "<li class='list-group-item'><span class='pull-right'><button onclick='$.get(\"./setvolume?channel=$channel&volume=0\");'><span class='glyphicon glyphicon-volume-off' aria-hidden='true'></span></button><button onclick='$.get(\"./setvolume?channel=$channel&volume=50\");'><span class='glyphicon glyphicon-volume-down' aria-hidden='true'></span></button><button onclick='$.get(\"./setvolume?channel=$channel&volume=100\");'><span class='glyphicon glyphicon-volume-up' aria-hidden='true'></span></button></span>$name</li>";

////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Begin general functions (not specifically for the multiplexer or amplifiers). ///////////////////////////

// Get the parameters for a single channel if specified, or all channels if none specified.
String getChannel(uint8_t channel = 8) {

  String returnString = "[";

  if (channel != 8) {
    returnString += '{ "name": "' + channels[channel].name + '",';
    returnString += '"volume": ' + channels[channel].volume + ',';
    returnString += '"active": ' + channels[channel].active + '}';
  }

  else {

    // Iterate through all 8 multiplexed channels to detect i2c devices.
    for (uint8_t i = MINAMPLIFIERS; i < MAXAMPLIFIERS; i++) {
  
      returnString += '{ "name": "' + channels[i].name + '",';
      returnString += '"volume": ' + channels[i].volume + ',';
      returnString += '"active": ' + channels[i].active + '}';
  
      if (i <= MAXAMPLIFIERS) {
        returnString += ",";
      }
    }
  }

  returnString += "]";

  return returnString;
}

// End general functions (not specifically for the multiplexer or amplifiers). /////////////////////////////


// Begin i2c multiplexer code. /////////////////////////////////////////////////////////////////////////////

// i2c multiplexer port selector (0 < i < 7).
void setChannel(uint8_t i) {

  if (i >= MAXAMPLIFIERS) {
    return;
  }
 
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
}


// Setup i2c multiplexer.
void channelsSetup() {
  
  while (!Serial) {};
  delay(1000);

  Serial.println("\nTCAScanner ready!");

  // Iterate through all 8 multiplexed channels to detect i2c devices.
  for (uint8_t i = MINAMPLIFIERS; i < MAXAMPLIFIERS; i++) {

    setChannel(i);
    Serial.print("TCA Port #"); Serial.println(i);

    // Try to write data to all 128 possible i2c addresses.  If it responds, a device is detected.
    for (uint8_t addr = 0; addr <= 127; addr++) {

      if (addr == TCAADDR) {
        continue;
      }

      uint8_t data;

      // If a device is found, set the volume to 50%, which is 31.  This was adjusted from the core Arduino twi_writeTo() version to the ESP8266 version: https://github.com/esp8266/Arduino/blob/master/cores/esp8266/twi.h
      // Reference: uint8_t twi_writeTo(unsigned char address, unsigned char * buf, unsigned int len, unsigned char sendStop);
      if (!twi_writeTo(addr, &data, 0, 1)) {

        Serial.print("Found I2C 0x");  Serial.println(addr, HEX);

        // Try to communicate with the amplifier and set its default volume.
        if (setVolume(DEFAULTVOLUME)) {
          channels[i].volume = DEFAULTVOLUME;
          channels[i].active = true;
        }
      }
    }
  }

  Serial.println("\nChannel scan completed.");
}

// End i2c multiplexer code. /////////////////////////////////////////////////////////////////////////////


// Begin amplifier code.  ////////////////////////////////////////////////////////////////////////////////

// Set the volume of the amplifier.
boolean setVolume(int8_t volume) {

  // The volume is 0 <= v <= 63, so keep the input in this range.
  if (volume > 63) {
    volume = 63;
  }

  if (volume < 0) {
    volume = 0;
  }

  Serial.print("Setting volume to ");
  Serial.println(volume);

  Wire.beginTransmission(TCAADDR);
  Wire.write(volume);

  if (Wire.endTransmission() == 0) {
    return true;
  } else {
    return false;
  }
}


// Mute all channels.
void muteAllChannels() {

  // Iterate through all 8 multiplexed channels to detect i2c devices.
  for (uint8_t i = MINAMPLIFIERS; i < MAXAMPLIFIERS; i++) {
    if (channels[i].active) {
      setChannel(i);
      channels[i].volume = MINVOLUME;
      setVolume(MINVOLUME);
      Serial.print("Setting channel "); Serial.print(i); Serial.print(" to "); Serial.println(MINVOLUME);
    }
  }
}


// Max all channels.
void maxAllChannels() {

  // Iterate through all 8 multiplexed channels to detect i2c devices.
  for (uint8_t i = MINAMPLIFIERS; i < MAXAMPLIFIERS; i++) {
    if (channels[i].active) {
      setChannel(i);
      channels[i].volume = MAXVOLUME;
      setVolume(MAXVOLUME);
      Serial.print("Setting channel "); Serial.print(i); Serial.print(" to "); Serial.println(MAXVOLUME);
    }
  }
}

// End amplifier code.  //////////////////////////////////////////////////////////////////////////////////


// Begin ESP8266 web server code. ////////////////////////////////////////////////////////////////////////

void root() {

  String channelMarkup = "";

   // Iterate through all 8 multiplexed channels to detect i2c devices.
  for (uint8_t i = MINAMPLIFIERS; i < MAXAMPLIFIERS; i++) {

    if (channels[i].active) {

      String newChannel = list_item;
      newChannel.replace("$channel", String(i));
      newChannel.replace("$name", channels[i].name);

      channelMarkup += newChannel;
    }
  }
  
  server.send(200, "text/html", header + body_open + list_open + channelMarkup + list_close + body_close);
}


// Analog to Digital Converter runing on GPIO 0
void muteall() {
  muteAllChannels();
  server.send(200, "text/json", getChannel());
}


// Analog to Digital Converter runing on GPIO 0
void maxall() {
  maxAllChannels();
  server.send(200, "text/json", getChannel());
}


void getchannels() {
  server.send(200, "text/json", getChannel());
}


// Note that the volume here is between 1 and 100 to signify a percentage, which is mapped to the amplifier's min and max.
void setvolume() {

  uint8_t channel = server.arg("channel").toInt();
  uint8_t volume = server.arg("volume").toInt();

  // If the channel and volume are valid, set the volume for the channel.
  if (channel >= MINAMPLIFIERS && channel <= MAXAMPLIFIERS && volume >= MINMAPPEDVOLUME && volume <= MAXMAPPEDVOLUME && channels[channel].active) {

    // Map the 0 - 100% volume to a 0 - 63 value.
    channels[channel].volume = map(volume, MINMAPPEDVOLUME, MAXMAPPEDVOLUME, MINVOLUME, MAXVOLUME);

    // Send the new volume to the active channel.
    setChannel(channel);
    setVolume(channels[channel].volume);
    server.send(200, "text/json", getChannel(channel));
  }

  // Either the channel or volume was invalid, so return an error.
  else {
    server.send(400);
  }
}


void wifiSetup() {

  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("*");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Set up the endpoints for HTTP server.  The functions are named very particularly, and will not compile if the function name is greater than 8 characters and contains a capital letter, but will accept a capital letter if it is less than 8 characters.
  server.on("/", root);
  server.on("/channels", getchannels); // Get the number of channels and their volume.
  server.on("/volume", setvolume); // Process a volume command for a specific channel.
  server.on("/mute-all", muteall);
  server.on("/max-all", maxall);

  // Start the server.
  server.begin();
  Serial.println("HTTP server activated.");
}

// End ESP8266 web server code. //////////////////////////////////////////////////////////////////////////


void setup() {

  // Begin the serial communication.
  Serial.begin(115200);
  Serial.println("");

  // Run the i2c multiplexer initialization to detect how many amplifier channels are detected.
  Wire.begin();
  channelsSetup();

  // Connect to WiFi network.
  wifiSetup();
}


void loop() {
  server.handleClient();
}
