#include <stdlib.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include "ESP8266_ISR_Servo.h"
#define MIN_MICROS 500
#define MAX_MICROS 2500
int servoIndex = -1;

#define damperPIN 0

//const char* ssid     = "e2"; // Your ssid
//const char* password = "ChooseWisely"; // Your Password

String  oneWireScan_html;
String  config_html;
String  default_html;

struct configData_Struct {
  char  ssid[64];
  char  password[64]; // 63 is typical max
  byte tp_id[8];  // plenum sensor id
  float t_p;        // plenum temp
  byte tr_id[8];  // room sensor id
  float t_r;        // room temp
  float ts;
  int d_open;   // angle of open (5?)
  int d_close;  // angle of close (175?)
  int d_pulse;
} ;

configData_Struct config_data = {
  "e2",
  "ChooseWisely",
  {40,255,126,35,179,22,4,0},   // plenum one-wire ID
  21,  // initial plenum temp
  {40,255,159,44,179,22,4,141}, // room one-wire ID
  21,  // initial room remp
  21,        // default set point C * 10
  500,        // damper open ms
  2250,       // damper closed ms
  3000        // total servo pulse length
} ;

bool b_damperOpen = false;

OneWire  ds(2);  // on pin 2 (a 4.7K resistor is necessary)
WiFiServer server(80);

//========================================================
//
// setup
//
//========================================================
void setup() {
  
  Serial.begin(115200);

//  EEPROM.begin(512);
  getConfig();
  
  setupWiFi();
  
  // Setup for controlling a hobby servo

  servoIndex = ISR_Servo.setupServo(damperPIN, MIN_MICROS, MAX_MICROS); 
  //pinMode(damperPIN, OUTPUT);
  pulseDamper(true); // open the damper.
}

//========================================================
//
// main loop
//
//========================================================
void loop() {
  static unsigned long tic = 0;
  float celsius;

//  if ((tic % 1500000) == 0) { // about 1 / minute
  if ((tic % 250000) == 0) {  // about 1 / 10 seconds?
    celsius = getTemp(config_data.tp_id);
    if (celsius > 0) config_data.t_p = celsius;
    delay(100); // needed?
    celsius = getTemp(config_data.tr_id);
    if (celsius > 0) config_data.t_r = celsius;
  }

  if ((tic % 125000) == 0) {  // about 1 / 5 seconds?
//    controlDamper_2();
    controlDamper();
  }

  handleWebRequests();  

  tic++;
}

//========================================================
//
//
// controlDamper
//
// Logic to determine if the air in the plenum/vents is wanted/helpful or not.
//
// If the room temp is ok, damper should be open for fresh air.
// If the room temp is too warm, then the damper should close if plenum is also warm.
// If the room temp is too cold, then the damper should close if plenum is also cold.
//
// If Open Then
//    Close if (Tr+0.5 > Ts && Tp-0.5 > Ts) // too warm and heating
//      || (Tr-0.5 < Ts && Tp+0.5 > Ts) // too cool and cooling
//  Else
//    Open if (Tr+0.5 > Ts && Tp+0.5 < Ts) // too warm and cooling
//      || (Tr-0.5 < Ts && Tp-0.5 > Ts)  // too cool and heating
//
//========================================================
void controlDamper()
{
  float closeEnough = 1.0; // within a degree of setpoint, always stay open
  float hysteresis = 0.5; // how far into close zone must room temperature get before closing?
        // also, how far into the open quadrant must plenum temperature get before opening?
  
  bool withinTRCE = (    config_data.t_r - closeEnough < (config_data.ts)    &&    config_data.t_r + closeEnough > (config_data.ts)    ); // is the room temperature close enough to the setpoint to just stay open?
  bool withinTRH = (    config_data.t_r - closeEnough - hysteresis < (config_data.ts)    &&    config_data.t_r + closeEnough + hysteresis > (config_data.ts)    ); // is the room temperature within the hysteresis distance of the close enough zone?
  bool withinTPH = (    config_data.t_p - hysteresis < config_data.t_r    &&    config_data.t_p + hysteresis > config_data.t_r    ); // is the plenum temperature within the hysteresis distance of the room?
  bool openQuadrant = (    config_data.t_r < (config_data.ts)    ); // are we in one of the quadrants where the valve should be open?
      // in other words, is the plenum closer to the setpoint then the room is?
    
  if (    config_data.t_p < config_data.t_r    )
    openQuadrant = ! openQuadrant;
  
  if (b_damperOpen) {    // damper is open, stay open as long as we are in the open quadrants OR in room temperature hysteresis zone
    b_damperOpen = openQuadrant || withinTRH;
  } else {      // damper is closed, open if we enter the close enough zone or past the damper hysteresis in an open quadrant
    b_damperOpen = withinTRCE    ||    ( openQuadrant && ! withinTPH);
  }

  pulseDamper(b_damperOpen);
}

// A more complex hysteresis model Matt came up with.
void controlDamper_2(){
    double tempPlenum = config_data.t_p;
    double tempRoom = config_data.t_r;

    // begin portable section ------------------------------------------------------
    // tempPlenum = plenum
    // tempRoom = room
    // isOpen = persistent valve state

    double t_setpoint = 25.;
    double t_hysteresis = .5;
    double t_PlenumCloseEnough = 2.;
 
    // clip temperature boundaries, because outside this range is undefined
    tempPlenum= min(max(tempPlenum, 5.0), 45.0);
    tempRoom = min(max(tempRoom, 5.0), 45.0);
    
    // offset shape, centering it around setpoint
    tempPlenum -= t_setpoint;
    tempRoom -= t_setpoint;

    // create array of points for polygon
    
    double polygon[20];
    
  // left side
    polygon[0]=-100;
    polygon[1]=0;
    
    // clipped left side for plenum close enough zone
    polygon[2]=0-t_PlenumCloseEnough;
    polygon[3]=0;
    polygon[4]=0-t_PlenumCloseEnough;
    polygon[5]=0-t_PlenumCloseEnough;
    
  // bottom left
  polygon[6]=-100;
    polygon[7]=-100;
    // bottom right
    polygon[8]=100;
    polygon[9]=-100;
    // right side
    polygon[10]=100;
    polygon[11]=0;
    
    // clipped right side for plenum close enough zone
    polygon[12]=t_PlenumCloseEnough;
    polygon[13]=0;
    polygon[14]=t_PlenumCloseEnough;
    polygon[15]=t_PlenumCloseEnough;
    
    // top right
    polygon[16]=100;
    polygon[17]=100;
    // top left
    polygon[18]=-100;
    polygon[19]=100;
    
    // inigo's polygon code, i've replaced the math with less GLSL-specific syntax
    
    // The MIT License
    // Copyright © 2019 Inigo Quilez
    // Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
    
    int num = 10; // array length
                // (in arduino our array is fake vec2's so this is half the length of the real array)
    
    double d = (tempPlenum-polygon[0]) * (tempPlenum-polygon[0]) + // dot without dot
              (tempRoom  -polygon[1]) * (tempRoom  -polygon[1]);    
   
    double signOfField = 1.0;
    for( int i=0; i<num; i++ )
    {
        int j = (i + num - 1)%num; // j is the WRAPPED array coordinate 1 less then i
              
        double ex = polygon[j*2]   - polygon[i*2];
        double ey = polygon[j*2+1] - polygon[i*2+1];
                
        double wx = tempPlenum - polygon[i*2];
        double wy = tempRoom -   polygon[i*2+1];
        
        double dot1 = wx*ex + wy*ey;
        double dot2 = ex*ex + ey*ey;
        
        double clampedScale = dot1/dot2;
        
        clampedScale = min(max(clampedScale, 0.0), 1.0);
        
        double bx = wx - ex * clampedScale;
        double by = wy - ey * clampedScale;
        
        d = min( d, bx*bx+by*by );

        // winding number from http://geomalgorithms.com/a03-_inclusion.html
  
        if( (tempRoom >= polygon[i*2+1] && tempRoom <  polygon[j*2+1] && ex*wy >  ey*wx) || 
            (tempRoom <  polygon[i*2+1] && tempRoom >= polygon[j*2+1] && ex*wy <= ey*wx) )
            
            signOfField =- signOfField;  
    }
    
    d = signOfField*sqrt(d);
    
    // end inigo's polygon code

    // move hysteresis inside open shape, and clip SDF to 0
    d = max(d + t_hysteresis,0.0);
    
    double openZone = 1;
    
    // hysteresis zone
    if(d>0 && d < t_hysteresis)
        openZone = 0.5;
    
    // closed region
    if(d > t_hysteresis)
        openZone = 0;

    if(b_damperOpen && openZone == 0)
        b_damperOpen = false;
        
    if(!b_damperOpen && openZone == 1)
        b_damperOpen = true;
  
}

//========================================================
//
// pulseDamper (bool bOpen)
//
// drive the servo controlling the damper
//
//========================================================
void pulseDamper(bool bOpen) {
  static boolean bState = true;
  if (bOpen == bState) {
    // been here, turn off servo and bail.
    ISR_Servo.disableAll();
    return;
  }
  bState = bOpen; 
  ISR_Servo.enableAll();
  if (bOpen) {
    Serial.println("D:O");
    ISR_Servo.setPosition(servoIndex, config_data.d_open);
  } else {
    Serial.println("D:C");
    ISR_Servo.setPosition(servoIndex, config_data.d_close);
  }
}
void pulseDamper_old(bool bOpen){
  static boolean bState = true;
  static int Pc = config_data.d_close; // current position
  unsigned long uSecs_start, uSecs_now;
  
  if (bOpen != bState)
  {
    bState = bOpen;
    if (bOpen) Serial.println("D:O"); else Serial.println("D:C");
    for (int i; i<150; i++) {
      if (bOpen) {
        Pc = config_data.d_open;
      } else {
        Pc = config_data.d_close;
      }
      
//  500,        // damper open ms
//  2250,       // damper closed ms
//  3000        // total servo pulse length
      digitalWrite(damperPIN, HIGH);
      // delayMicroseconds(Pc);
      uSecs_start = micros();
      while (micros() > uSecs_start && micros() < uSecs_start + Pc) {}
      digitalWrite(damperPIN, LOW);
      //delayMicroseconds(config_data.d_pulse - Pc);
      uSecs_start = micros();
      while (micros() > uSecs_start && micros() < uSecs_start + (config_data.d_pulse - Pc)) {}
    }
  }
}

//========================================================
//
// int getTemp (int[8])
//
// Query device by ID passed in for it's temperature
//
//========================================================
// Retrieve sensor temperature in C*10
float getTemp(const uint8_t rom[8]) {
  byte present = 0;
  byte type_s;
  byte data[12];
  float celsius;

  Serial.print("ROM =");
  for ( int i = 0; i < 8; i++) {
    if (rom[i]<16) Serial.write('0');
    Serial.print(rom[i],HEX);
  }

  //if (OneWire::crc8(addr, 7) != addr[7]) {
  if (OneWire::crc8(rom, 7) != rom[7]) {
    Serial.println("CRC is not valid!");
    return -1;
  }

  // the first ROM byte indicates which chip
  switch (rom[0]) {
    case 0x10:
      Serial.print(" DS18S20 ");  // or old DS1820
      type_s = 1;
      break;
    case 0x28:
      Serial.print(" DS18B20 ");
      type_s = 0;
      break;
    case 0x22:
      Serial.print(" DS1822 ");
      type_s = 0;
      break;
    default:
      Serial.println(" Not a DS18x20 device.");
      return -1; 
  }

  ds.reset();
  ds.select(rom);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end
  
  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.

  present = ds.reset();
  ds.select(rom);
  ds.write(0xBE);         // Read Scratchpad
  
  for ( int i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
  }

  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }

  celsius = (float)raw / 16.0;
  Serial.print(" T = ");
  Serial.print(celsius);
  Serial.println("C");

  return celsius;

}

//========================================================
//
// setupWiFi
//
//========================================================
void setupWiFi() {
    // Connect to WiFi network
  Serial.print("Connecting to ");
  Serial.println(config_data.ssid);
  
  WiFi.begin(config_data.ssid, config_data.password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  // Start the server
  server.begin();
  Serial.println("Server started");
  
  // Print the IP address
  Serial.println(WiFi.localIP());
}


//========================================================
//
// handleWebRequests
//
//========================================================
void handleWebRequests()
{
  WiFiClient client = server.available();
  if (!client) { return; }
  client.setTimeout(5000);

  String req = client.readStringUntil('\r');
  Serial.print(F("request: "));
  Serial.println(req);

  if (req.indexOf(F("/favicon")) != -1) {
    // An icon? Get out of here :-)
    Serial.println("FavIcon");
    client.println("HTTP/1.1 404\r\nContent-Type: text/html\r\nConnection: close\r\n<!DOCTYPE HTML>\r\n<html>fav</html>");
    while (client.available()) {
      // byte by byte is not very efficient
      client.read();
    }
    return;
  }
  
  if (req.indexOf(F("/scan")) != -1) {
    // Create page w/ one wire scan data
    Serial.println("Request scan page");
    scanForDevices(client);
    while (client.available()) {
      // byte by byte is not very efficient
      client.read();
    }
    return;
  }

  if (req.indexOf(F("/get?")) != -1) {
    // Incoming config update request!
    processGet(req);
    while (client.available()) {
      // byte by byte is not very efficient
      client.read();
    }
    client.print("<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"0; url='/'\" /></head><body>root</body></html>");
    return;
  }
  
  client.print("TS=");
  client.print(config_data.ts);
  client.print(",TR=");
  client.print(config_data.t_r);
  client.print(",TP=");
  client.print(config_data.t_p);
  client.print(",D=");
  if (b_damperOpen) client.println("O"); else client.println("C");
  while (client.available()) {
    // byte by byte is not very efficient
    client.read();
  }
}

//========================================================
//
// process get update
//
//========================================================
void processGet(String req){
  int x;
  int i;
  float t;
  char twobytes[3];

//   request: GET /get?t_s=21%2C22%2C23 HTTP/1.1  (messing with comma seperated list of numbers)
// request: GET /get?t_s=21.00 HTTP/1.1
// Parse 3 fields

  if ((i=req.indexOf(F("ts="))) != -1) {
    t = req.substring(i+3).toFloat();
    Serial.print("new temp=");
    Serial.println(t);
    config_data.ts = t;
  }

  if ((i=req.indexOf(F("d_open="))) != -1) {
    x = req.substring(i+7).toInt();
    Serial.print("new open=");
    Serial.println(x);
    config_data.d_open = x;
//    pulseDamper(true);  // test open
  }

  if ((i=req.indexOf(F("d_close="))) != -1) {
    x = req.substring(i+8).toInt();
    Serial.print("new close=");
    Serial.println(x);
    config_data.d_close = x;
//    pulseDamper(false);  // test open
  }

  // OneWire ID of Plenum sensor
  if ((i=req.indexOf(F("tp_id="))) != -1) {
    Serial.print("Set TP ID=");
    for (int c=0; c< 8; c++) {
      req.substring( (i+6)+(c*2), (i+6)+(c*2)+2 ).toCharArray(twobytes,3);
      Serial.print(twobytes);
      config_data.tp_id[c] = strtol( twobytes, NULL, 16);
    }
    Serial.println();
  }

  // OneWire ID of Room sensor
  if ((i=req.indexOf(F("tr_id="))) != -1) {
    Serial.print("Set TR ID=");
    for (int c=0; c< 8; c++) {
      req.substring( (i+6)+(c*2), (i+6)+(c*2)+2 ).toCharArray(twobytes,3);
      Serial.print(twobytes);
      config_data.tr_id[c] = strtol( twobytes, NULL, 16);
    }
    Serial.println();
  }

  // SSID
  if ((i=req.indexOf(F("ssid="))) != -1) {
    String ssid = req.substring( (i+5), req.indexOf(F(" "), i) );
    Serial.print("Set SSID=");
    Serial.print(config_data.ssid); Serial.print("|"); Serial.print(ssid); Serial.print("|");
    ssid.getBytes((unsigned char*)config_data.ssid, 64);
    Serial.println(config_data.ssid);
  }

  // password
  if ((i=req.indexOf(F("pwd="))) != -1) {
    String pwd = req.substring( (i+4), req.indexOf(F(" "), i) );
    Serial.print("Set password=");
    Serial.print(config_data.password); Serial.print("|"); Serial.print(pwd); Serial.print("|");
    pwd.getBytes((unsigned char*)config_data.password, 64);
    Serial.println(config_data.password);
  }

  // Save any changes to EEPROM
  saveConfig();
}


//========================================================
//
// scanForDevices
//
// Scan for One Wire IDs
// Create web page response
//
//========================================================
void scanForDevices(WiFiClient client) {
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius;

  Serial.println("Scanning...");
  client.println("<html><body>Scanning...<br>");

  ds.reset_search();
  delay(250);
  
  while ( ds.search(addr)) {

    Serial.print("ROM =");
    client.print("ROM =");
    for ( i = 0; i < 8; i++) {
      if (addr[i]<16) {Serial.print('0');client.print('0');}
      Serial.print(addr[i],HEX);
      client.print(addr[i],HEX);
    }

    // get temperature of sensor
    celsius = getTemp(addr);
    Serial.print(" Temp=");
    Serial.println(celsius);

    client.print(" Temp=");
    client.print(celsius);
    client.print("<br>");
    
    delay(250);
  }

  client.print("<br><br><form action=\"/get\">Plenum ID:<input type=\"text\" name=\"tp_id\" value=");
  for ( i = 0; i < 8; i++) {
    if (config_data.tp_id[i]<16) client.print('0');
    client.print(config_data.tp_id[i],HEX);
  }
  client.println("><input type=\"submit\" value=\"Set\"></form>");
  
  client.print("<br><form action=\"/get\">Room ID:<input type=\"text\" name=\"tr_id\" value=");
  for ( i = 0; i < 8; i++) {
    if (config_data.tr_id[i]<16) client.print('0');
    client.print(config_data.tr_id[i],HEX);
  }
  client.println("><input type=\"submit\" value=\"Set\"></form>");

  client.print("<br><form action=\"/get\">Open damper angle:<input type=\"text\" name=\"d_open\" value=");
  client.print(config_data.d_open);
  client.println("><input type=\"submit\" value=\"Set\"></form>");
  
  client.print("<br><form action=\"/get\">Close damper angle:<input type=\"text\" name=\"d_close\" value=");
  client.print(config_data.d_close);
  client.println("><input type=\"submit\" value=\"Set\"></form>");

  client.print("<br><form action=\"/get\">Set Point:<input type=\"text\" name=\"ts\" value=");
  client.print(config_data.ts);
  client.println("><input type=\"submit\" value=\"Set\"></form>");

  Serial.println("Scan complete");
  client.println("Scan complete</body></html>");

  ds.reset_search();
}

//========================================================
//
// saveConfig
//
// write config to EEPROM
//
//========================================================
void saveConfig(){
  int eeAddress = 0;
  EEPROM.put(eeAddress, config_data);
  EEPROM.commit();
}

//========================================================
//
// getConfig
//
// read config from EEPROM
//
//========================================================
void getConfig(){
//  struct configData_Struct {
//    char  ssid[64];
//    char  password[64]; // 63 is typical max
//    byte tp_id[8];  // plenum sensor id
//    float t_p;        // plenum temp
//    byte tr_id[8];  // room sensor id
//    float t_r;        // room temp
//    float ts;
//    int d_open;
//    int d_close;
//    int d_pulse;
//  } ;
  int eeAddress = 0;
  EEPROM.begin(512);
  EEPROM.get(eeAddress, config_data);
  Serial.println(config_data.ssid);
  Serial.println(config_data.password);
  for(int i; i<8; i++) {
    if (config_data.tp_id[i]<16) Serial.print("0");
    Serial.print(config_data.tp_id[i],HEX);
  }
  Serial.println();
  for(int i; i<8; i++) {
    if (config_data.tr_id[i]<16) Serial.print("0");
    Serial.print(config_data.tr_id[i],HEX);
  }
  Serial.println();
  Serial.println(config_data.ts);
}
