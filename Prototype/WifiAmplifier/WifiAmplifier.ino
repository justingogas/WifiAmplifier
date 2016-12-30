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
String header        = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>I <3 ESP8266</title><link rel='shortcut icon' href='http://www.jpellerin.info/bl0g/img/blog_ico.png' type='image/x-icon'><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css'><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/font-awesome/4.3.0/css/font-awesome.min.css' /></head>";
String header_Rfsh   = "<html><head><META HTTP-EQUIV='Refresh' CONTENT='1;URL=/leds'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>I <3 ESP8266</title><link rel='shortcut icon' href='http://www.jpellerin.info/bl0g/img/blog_ico.png' type='image/x-icon'><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css'><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css'><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/font-awesome/4.3.0/css/font-awesome.min.css' /></head>";
String headerRfshTkr = "<html><head><META HTTP-EQUIV='Refresh' CONTENT='1;URL=/tkr'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>I <3 ESP8266</title><link rel='shortcut icon' href='http://www.jpellerin.info/bl0g/img/blog_ico.png' type='image/x-icon'><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css'><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.4/css/bootstrap.min.css'><link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/font-awesome/4.3.0/css/font-awesome.min.css' /></head>";
String menu_btns     = "<br /><div class='row'><div class='col-xs-12 col-sm-12 col-md-6 col-md-offset-3'><center><div class='btn-group' role='group' aria-label='MENU'><a href='/leds' type='button' class='btn btn-md' style='background-color: #000; frameborder=1; border-color:#656565;'>Toggle LEDs <i class='fa fa-lightbulb-o'></i></a><a href='/adc' type='button' class='btn btn-md' style='background-color: #000; frameborder=1; border-color:#656565;'>A/D Converter <i class='fa fa-random'></i></a><a type='button' class='btn btn-md' style='background-color: #000; frameborder=1; border-color:#656565;' href='/tkr'>Ticker <i class='fa fa-spin fa-clock-o'></i></a></div></center></div></div>";
String toggle0       = "<div class='col-xs-6 col-sm-6 col-md-3 col-md-offset-3' style='padding: 15px;'><div class='well' style='background-color: #001; color: ffd700; border-color:#000; padding: 15px;'><div class=''><center><form class='form-inline' action='led0'><div class='radio'><label><input type='radio' name='state' value='1' checked style='width: 55px; height: 55px'> On </label></div><div class='radio'><label><input type='radio' name='state' value='0' style='width: 35px; height: 35px'> Off </label></div><br /><button type='submit' class='btn btn-primary btn-lg' value=''>Toggle LED0</button></form></center></div></div></div>";
String toggle2       = "<div class='col-xs-6 col-sm-6 col-md-3' style='padding: 15px;'><div class='well' style='background-color: #001; color: ffd700; border-color:#000; padding: 15px;'><div class=''><center><form class='form-inline' action='led2'><div class='radio'><label><input type='radio' name='state' value='1' checked style='width: 55px; height: 55px'> On </label></div><div class='radio'><label><input type='radio' name='state' value='0' style='width: 35px; height: 35px'> Off </label></div><br /><button type='submit' class='btn btn-primary btn-lg'  value=''>Toggle LED2</button></form></center></div></div></div>";
String tkrForm0      = "<div class='row'><div class='col-xs-12 col-sm-12 col-md-12 col-lg-12'><div class='col-md-6 col-md-offset-3'><form class='form-inline' action='tkr0'><div class='form-group'><label for='LED OFF time in ms.'> ms. OFF </label><input type='number' class='form-control' id='off' name='off'  placeholder='55'></div><div class='form-group'><label for='LED ON time in ms.'> ms. ON </label><input type='number' class='form-control' id='on' name='on'  placeholder='56'></div><button type='submit' class='btn btn-default'>Start The Party!</button></form></div></div></div>";
String adcFrame      = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/adc.html' scrolling='no' align='center' height='200px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String ledsFrame     = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/leds.html' scrolling='no' align='center' height='125px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String flameFrame    = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/fire.html' scrolling='no' align='center' height='115px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String tkrFrame      = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/tkr.html' scrolling='no' align='center' height='175px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String ESP_Sml_Frame = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/esp_sml.html' scrolling='no' align='center' height='150px' width='255px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String rainbowFrame  = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/rainbow.html' scrolling='no' align='center' height='200px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";  
String cookieFrame   = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/Cookie.html' scrolling='no' align='center' height='200px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String ESP01Frame    = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/esp01.html' scrolling='no' align='center' height='500px' width='650px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String ESP03Frame    = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/esp03.html' scrolling='no' align='center' height='330px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String jackFrame     = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/jackBurton.html' scrolling='no' align='center' height='300px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String brainFrame    = "<div class='row'><div class='col-xs-12 col-md-12 col-lg-12' style='text-align: center;'><iframe src='http://dev.jpellerin.info/ESP/brain.html' scrolling='no' align='center' height='500px' width='330px' frameborder=0; border-color:#000; background:#000; border-style:none;'></iframe></div></div>";
String back          = "<div class='row'><div class='col-xs-12 col-md-6 col-md-offset-3'<br /><a type='button' href='/' class='btn btn-lg btn-block' style='background-color: #000; frameborder=1; border-color:#656565;''>Back To Main</a><br /></div></div>";
String Chipset       = "<div class='row'><div class='col-md-12' style='text-align: center;'><h3><i class='fa fa-fire'></i> The Hottest Chipset This Side of the Matrix <i class='fa fa-fire-extinguisher'></i></h3></div></div>";
String body_open     = "<body style='background-color: #000;'><div class='container'>";
String row_open      = "<div class='row'>";
String div_open      = "<div class='col-md-12'>";
String div_close     = "</div>";
String body_close    = "</div></body></html>";                                              

////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Begin general functions (not specifically for the multiplexer or amplifiers). ///////////////////////////

//String getChannel(uint8_t channel = 8);

String getChannel() {

  String returnString = "[";

/*  if (channel != 8) {
    returnString += '{ "name": "' + channels[channel].name + '",';
    returnString += '"volume": ' + channels[channel].volume + ',';
    returnString += '"active": ' + channels[channel].active + '}';
  }

  else {
*/
  // Iterate through all 8 multiplexed channels to detect i2c devices.
  for (uint8_t i = MINAMPLIFIERS; i < MAXAMPLIFIERS; i++) {

    returnString += '{ "name": "' + channels[i].name + '",';
    returnString += '"volume": ' + channels[i].volume + ',';
    returnString += '"active": ' + channels[i].active + '}';

    if (i <= MAXAMPLIFIERS) {
      returnString += ",";
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
    }
  }
}

// End amplifier code.  //////////////////////////////////////////////////////////////////////////////////


// Begin ESP8266 web server code. ////////////////////////////////////////////////////////////////////////

void root() {
  server.send(200, "text/html", header + body_open + ledsFrame + rainbowFrame + row_open + toggle0 + toggle2 + div_close + back + body_close);
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
    //server.send(200, "text/json", getChannel(channel));
    server.send(200, "text/json", getChannel());
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
  // check for incomming client connections frequently in the main loop:
  server.handleClient();
}
