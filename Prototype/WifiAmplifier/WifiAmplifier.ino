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
// TODO: left off trying to get the initial configuration in the file.  Need to decide how the initialization is handled when the port scan is done - is the port scan the source of truth, or the stored configuration?)  Then add in AP mode for initialization.
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include "FS.h"
#include <ArduinoJson.h>

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
#define CHANNELSFILENAME "channels.txt"
#define WIFIFILENAME "wifi.txt"
#define APSSID "WiFi amplifier"
#define APPASSWORD "admin"

// Define the channels as an array of structures.
typedef struct {
  boolean active = false;
  uint8_t volume = 0;
  String name = "Unnamed channel";
  uint8_t address = 0x4B;
} channel;

channel channels[MAXAMPLIFIERS];


// HTTP server will listen at port 80.
ESP8266WebServer server(80);

// WiFi credentials.
// TODO: read ssid and password from EEPROM and run an access point to allow for non-hard-coded configuration.  http://www.john-lassen.de/en/projects/esp-8266-arduino-ide-webconfig
const char* ssid = "";
const char* password = "";

// Web page components.
String header =
  "<!DOCTYPE html><html lang='en'>"
  "<head>"
  " <meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>WiFi Amplifier</title>"
  " <link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css'>"
  " <script src='https://ajax.googleapis.com/ajax/libs/jquery/1.12.4/jquery.min.js'></script>"
  " <script src='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js'></script>"
  "</head>";

String body_open = 
  "<body style='padding-top: 70px;'>"
  " <nav class='navbar navbar-inverse navbar-fixed-top'>"
  "   <div class='container-fluid'>"
  "     <div class='navbar-header'>"
  "       <button type='button' class='navbar-toggle collapsed' data-toggle='collapse' data-target='#navbar' aria-expanded='false' aria-controls='navbar'>"
  "         <span class='sr-only'>Toggle navigation</span>"
  "         <span class='icon-bar'></span>"
  "         <span class='icon-bar'></span>"
  "         <span class='icon-bar'></span>"
  "       </button>"
  "       <a class='navbar-brand' href='#'>WiFi Amplifier</a>"
  "     </div>"
  "     <div id='navbar' class='collapse navbar-collapse'>"
  "       <ul class='nav navbar-nav'>"
  "         <li class='active'><a href='#'>Channels</a></li>"
  "         <li><a id='mute-all-button' style='cursor: pointer;'>Mute all</a></li>"
  "         <li><a id='max-all-button' style='cursor: pointer;'>Max all</a></li>"
  "         <li><a href='./save'>Save</a></li>"
  "         <li class='dropdown'>"
  "           <a href='#' class='dropdown-toggle' data-toggle='dropdown' aria-haspopup='true' aria-expanded='false'>Network <span class='caret'></span></a>"
  "           <ul class='dropdown-menu' role='menu' style='min-width: 200px;'>"
  "             <form class='form' role='form' method='post' action='./network'>"
  "               <li>IP: $ip, strength: $strength"
  "               <li><input type='text' class='form-control' name='ssid' placeholder='Network SSID' required></li>"
  "               <li><input type='text' class='form-control' name='ssid' placeholder='Network password' required></li>"
  "               <li><input type='submit' class='btn btn-primary btn-block' value='Save and reset'></li>"
  "             </form>"
  "           </ul>"
  "       </ul>"
  "     </div>"
  "   </div>"
  " </nav>"
  " <div class='container-fluid'>";

String body_close =
  "   </div>"
  "   <script src='https://ajax.googleapis.com/ajax/libs/jquery/1.12.4/jquery.min.js'></script>"
  "   <script src='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js'></script>"
  "   <script src='https://cdnjs.cloudflare.com/ajax/libs/bootstrap-slider/9.5.4/bootstrap-slider.min.js'></script>"
  "   <script type='text/javascript'>"
  "     $('#mute-all-button').on('click', function() { var range = $(this); setVolume(-1, 0) });"
  "     $('#max-all-button').on('click', function() { var range = $(this); setVolume(-1, 100) });"
  "     $('[id^=mute-button]').on('click', function() { var range = $(this); setVolume(range.attr('data-channel'), 0) });"
  "     $('[id^=max-button]').on('click', function() { var range = $(this); setVolume(range.attr('data-channel'), 100) });"
  "     $('[id^=range-]').on('click', function() { var range = $(this); setVolume(range.attr('data-channel'), range.val()) });"
  "     function setVolume(channel, volume) { $.get('./volume?channel=' + channel + '&volume=' + volume); }"
  " </body>"
  "</html>";

String list_open = "<ul class='list-group'>";
String list_close = "</ul>";

String list_item = 
  "<li class='list-group-item'>"
  " $name"
  " <span class='pull-right'>"
  "   <div class='btn-group' role='group'>"
  "     <button type='button' id='mute-button-$channel' class='btn btn-default' data-channel='$channel'>"
  "       <span class='glyphicon glyphicon-volume-off' aria-hidden='true'></span>"
  "     </button>"
  "     <button type='button' id='max-button-$channel' class='btn btn-default' data-channel='$channel'>"
  "       <span class='glyphicon glyphicon-volume-up' aria-hidden='true'></span>"
  "     </button>"
  "   </div>"
  " </span>"
  " <input type='range' min='0' max='100' id='range-$channel' data-channel='$channel' style='padding-top: 10px;'>"
  "</li>";

////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Begin general functions (not specifically for the multiplexer or amplifiers). ///////////////////////////

// Get the parameters for a single channel if specified, or all channels if none specified.
String getChannel(int8_t channel = -1) {

  String returnString = "[";

  if (channel != -1) {
    returnString += '{ "name": "' + channels[channel].name + '",' + '"volume": ' + channels[channel].volume + ',' + '"address": ' + channels[channel].address + ',' + '"active": ' + channels[channel].active + '}';
  }

  else {

    // Iterate through all 8 multiplexed channels to detect i2c devices.
    for (uint8_t i = MINAMPLIFIERS; i < MAXAMPLIFIERS; i++) {
  
      returnString += '{ "name": "' + channels[channel].name + '",' + '"volume": ' + channels[channel].volume + ',' + '"address": ' + channels[channel].address + ',' + '"active": ' + channels[channel].active + '}';

      if (i <= MAXAMPLIFIERS) {
        returnString += ",";
      }
    }
  }

  returnString += "]";

  return returnString;
}


String getWifi(String inputSsid = "", String inputPassword = "") {

  String wifiConfig = "";

  if (inputSsid == "" && inputPassword == "") {
    String wifiConfig = '{ "ssid": "' + inputSsid + '", "password": "' + inputPassword + '" }';
  } else {
    String wifiConfig = '{ "ssid": "' + (String)ssid + '", "password": "' + (String)password + '" }';
  }

  return wifiConfig;
}


void configurationSetup() {

  String channelsConfiguration = "";
  String wifiConfiguration = "";

  // Open the channels configuration file, if it exists.
  File channelsFile = SPIFFS.open(CHANNELSFILENAME, "r");

  // Write the default configuration to the file.
  if (!channelsFile) {

    Serial.println("No channels configuration file detected.  Creating a new one.");
    channelsFile = SPIFFS.open(CHANNELSFILENAME, "w");
    channelsFile.println(getChannel());
    channelsFile.close();

    channelsFile = SPIFFS.open(CHANNELSFILENAME, "r");
  }

  while (channelsFile.available()) {
    channelsConfiguration += channelsFile.read();
  }


  // Open the wifi configuration file, if it exists.
  File wifiFile = SPIFFS.open(WIFIFILENAME, "r");

  // Write the default configuration to the file.
  if (!wifiFile) {

    Serial.println("No wifi configuration file detected.  Creating a new one.");
    wifiFile = SPIFFS.open(WIFIFILENAME, "w");
    wifiFile.println(getWifi());
    wifiFile.close();

    wifiFile = SPIFFS.open(WIFIFILENAME, "r");
  }

  while (wifiFile.available()) {
    wifiConfiguration += wifiFile.read();
  }

  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& wifiJson = jsonBuffer.parseObject(wifiConfiguration);

  if (!wifiJson.success()) {
    Serial.println("JSON parsing of wifi configuration failed.  Setting default values.");
    ssid = "";
    password = "";
  }

  else {
    ssid = wifiJson["ssid"];
    password = wifiJson["password"];
  }

}

// End general functions (not specifically for the multiplexer or amplifiers). /////////////////////////////


// Begin i2c multiplexer code. /////////////////////////////////////////////////////////////////////////////

// i2c multiplexer port selector (0 < i < 7).
void setChannel(uint8_t i) {

  if (i >= MAXAMPLIFIERS) {
    return;
  }

  Serial.print("Setting active channel to "); Serial.println(i);
  
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
        if (setVolume(DEFAULTVOLUME, addr)) {
          channels[i].name = "Unnamed channel";
          channels[i].volume = DEFAULTVOLUME;
          channels[i].active = true;
          channels[i].address = addr;
        }
      }
    }
  }

  Serial.println("\nChannel scan completed.");
}

// End i2c multiplexer code. /////////////////////////////////////////////////////////////////////////////


// Begin amplifier code.  ////////////////////////////////////////////////////////////////////////////////

// Set the volume of the amplifier.
boolean setVolume(uint8_t volume, uint8_t address) {

  // The volume is 0 <= v <= 63, so keep the input in this range.
  if (volume > 63) {
    volume = 63;
  }

  if (volume < 0) {
    volume = 0;
  }

  Serial.print("Setting volume to ");
  Serial.println(volume);

  //Wire.beginTransmission(TCAADDR);
  Wire.beginTransmission(address);
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
      setVolume(MINVOLUME, channels[i].address);
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
      setVolume(MAXVOLUME, channels[i].address);
      Serial.print("Setting channel "); Serial.print(i); Serial.print(" to "); Serial.println(MAXVOLUME);
    }
  }
}

// End amplifier code.  //////////////////////////////////////////////////////////////////////////////////


// Begin ESP8266 web server code. ////////////////////////////////////////////////////////////////////////

void root() {

  String channelMarkup = "";

   // Iterate through all multiplexed channels to detect i2c devices.
  for (uint8_t i = MINAMPLIFIERS; i < MAXAMPLIFIERS; i++) {

    if (channels[i].active) {

      String newChannel = list_item;
      newChannel.replace("$channel", String(i));
      newChannel.replace("$name", channels[i].name);

      channelMarkup += newChannel;
    }
  }

  /*char ipAddress[16];
  IPAddress ip = WiFi.localIP();
  sprintf(ipAddress, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
*/
  //String bodyMarkup = body_open; //.replace("$ip", ipAddress);
  //bodyMarkup = bodyMarkup.replace("$strength", String(WiFi.RSSI()));
 
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


// Save the new channel configuration.
void save() {

  // TODO: gather up the channel text labels and submit and save.

  // Redirect back to the index page.
  server.sendHeader("Location", String("/"), true);
  server.send (302, "text/plain", "");
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
    setVolume(channels[channel].volume, channels[channel].address);
    server.send(200, "text/json", getChannel(channel));
  }

  // Either the channel or volume was invalid, so return an error.
  else {
    server.send(400);
  }
}


void network() {

  if (server.hasArg("ssid") && server.hasArg("password")) {
    
    // Open the wifi configuration file, if it exists.
    File wifiFile = SPIFFS.open(WIFIFILENAME, "w");
    wifiFile.println(getWifi((String)server.arg("ssid"), (String)server.arg("password")));
    wifiFile.close();

    // Reset the module so the new configuration gets loaded.
    ESP.reset();

  } else {
    server.send(400);
  }
}

// Save the configuration to files.
void saveconfig() {

  // Open the channels configuration file, if it exists.
  File channelsFile = SPIFFS.open(CHANNELSFILENAME, "w");
  
  if (!channelsFile) {
    Serial.println("Error writing the channels configuration file.");
  } else {
    channelsFile.println(getChannel());
    channelsFile.close();
  }

  // Open the wifi configuration file, if it exists.
  File wifiFile = SPIFFS.open(WIFIFILENAME, "w");

  if (!wifiFile) {
    Serial.println("Error writing the channels configuration file.");
  } else {
    wifiFile.println(getWifi());
    wifiFile.close();
  }
}

void wifiSetup() {

  // If no wifi configuration could be loaded, then go into AP mode.
  if (ssid == "" && password == "") {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(APSSID, APPASSWORD);
  }

  // Configuration was loaded, so attempt to connect to it.
  else {

    uint8_t attemptSeconds = 0;

    // Attempt to connect to the default network.  If no connection is made in 30 seconds, then start in AP mode so that the network can be set.
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED && attemptSeconds < 30) {
      delay(1000);
      Serial.print("*");
      attemptSeconds++;
    }

    // Connection is successful to the default network, so start the server.
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    }

    // The connection was not successful, so start the AP mode.
    else {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(APSSID, APPASSWORD);
    }
  }

  // Set up the endpoints for HTTP server.  The functions are named very particularly, and will not compile if the function name is greater than 8 characters and contains a capital letter, but will accept a capital letter if it is less than 8 characters.
  server.on("/", root);
  server.on("/channels", getchannels);    // Get the number of channels and their volume.
  server.on("/network", network);         // Receive new network configuration and reset the server.
  server.on("/volume", setvolume);        // Process a volume command for a specific channel.
  server.on("/mute-all", muteall);        // Mute all channels.
  server.on("/max-all", maxall);          // Max all channels.
  server.on("/save", saveconfig);  // Save current configuration.

  // Start the server.
  server.begin();
  Serial.println("HTTP server activated.");
}

// End ESP8266 web server code. //////////////////////////////////////////////////////////////////////////


void setup() {

  // Begin the serial communication.
  Serial.begin(115200);
  Serial.println("Starting setup.");

  // Initialize the SPIFFS file system to read / write the configuration.
  configurationSetup();
  SPIFFS.begin();

  // Run the i2c multiplexer initialization to detect how many amplifier channels are detected.
  Wire.begin();
  channelsSetup();

  // Connect to WiFi network.
  wifiSetup();
}


void loop() {
  server.handleClient();
}
