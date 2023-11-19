/*
  ArtGateOne v2.0 LED
*/
#include <Adafruit_NeoPixel.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>
#include <Wire.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include <EEPROM.h>
#include <avr/interrupt.h>
#define MAX_BUFFER_SIZE 530
#define LED_PIN 6
#define LED_COUNT 170      // How many NeoPixels are attached to the Arduino? - MAX 170 RGB
#define LED_BRIGHTNESS 50  // 1-255
#define analogPin A3       //Factory default
#define I2C_ADDRESS 0x3C   // OLED i2c addres
SSD1306AsciiAvrI2c oled;
// Declare our NeoPixel strip object:
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
// Argument 1 = Number of pixels in NeoPixel strip
// Argument 2 = Arduino pin number (most are valid)
// Argument 3 = Pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
//   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)

bool invert = false;
bool post = 0;
unsigned int datalen;
int data;
String strwww = String();
byte ArtPollReply[239];
bool dhcp_status = 0;
volatile bool ledState = LOW;
unsigned int ledMode = 0;
unsigned int counter = 5;              //LED on/off Counter
unsigned int nodeReportCode = 0x0001;  //ArtPollReply code

int NodeReportCounter = -1;  //ArtPollReply Counter
const int nodeReportLength = 64;
char nodeReport[nodeReportLength];

// Struktura do przechowywania informacji o kodach i ich opisach
struct NodeReportInfo {
  uint16_t code;
  const char* description;
};

// Tablica z informacjami o kodach i ich opisach
NodeReportInfo nodeReportTable[] = {
  { 0x0000, "Boot in debug mode. (Only used in development)" },
  { 0x0001, "Power On Tests successful." },
  { 0x0002, "Hardware tests failed at Power On" },
  { 0x0003, "Last UDP from Node failed due to truncated length, Most likely caused by a collision." },
  { 0x0004, "Unable to identify last UDP transmission. Check OpCode and packet length." },
  { 0x0005, "Unable to open UDP Socket in last transmission attempt" },
  { 0x0006, "Confirms that Short Name programming via ArtAddress, was successful." },
  { 0x0007, "Confirms that Long Name programming via ArtAddress, was successful." },
  { 0x0008, "DMX512 receive errors detected." },
  { 0x0009, "Ran out of internal DMX transmit buffers." },
  { 0x000A, "Ran out of internal DMX Rx buffers." },
  { 0x000B, "Rx Universe switches conflict." },
  { 0x000C, "Product configuration does not match firmware." },
  { 0x000D, "DMX output short detected. See GoodOutput field." },
  { 0x000E, "Last attempt to upload new firmware failed." },
  { 0x000F, "User changed switch settings when address locked by remote programming. User changes ignored." },
  { 0x0010, "Factory reset has occurred." },
};

// Get data from EEPROM
byte intN = EEPROM.read(531);  // NET
byte intS = EEPROM.read(532);  // Subnet
byte intU = EEPROM.read(533);  // Universe
unsigned int intUniverse = ((intN * 256) + (intS * 16) + intU);
int packetSize;

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
unsigned int localPort = 6454;  // local port to listen on
byte packetBuffer[MAX_BUFFER_SIZE];
char ReplyBuffer[] = "acknowledged";  // a string to send back

EthernetUDP Udp;
EthernetServer server(80);

void setup() {
  strip.begin();  // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();   // Turn OFF all pixels ASAP
  strip.setBrightness(LED_BRIGHTNESS);

  oled.begin(&Adafruit128x32, I2C_ADDRESS);
  oled.setFont(Arial14);
  oled.set1X();
  oled.clear();
  oled.println("     ArtGateOne 2");

  Ethernet.init(10);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(analogPin, INPUT_PULLUP);

  int pinValue = digitalRead(analogPin);

  if (EEPROM.read(1023) == 0 || EEPROM.read(1023) == 255 || pinValue == LOW)  // check first run or PIN3 to GND (FACTORY RESET)
  {                                                                           // write default config
    Factory_reset();
  }

  byte mac[] = { EEPROM.read(525), EEPROM.read(526), EEPROM.read(527), EEPROM.read(528), EEPROM.read(529), EEPROM.read(530) };
  IPAddress ip(EEPROM.read(513), EEPROM.read(514), EEPROM.read(515), EEPROM.read(516));
  IPAddress dns(0, 0, 0, 0);
  IPAddress subnet(EEPROM.read(517), EEPROM.read(518), EEPROM.read(519), EEPROM.read(520));
  IPAddress gateway(EEPROM.read(521), EEPROM.read(522), EEPROM.read(523), EEPROM.read(524));

  // initialize the ethernet device
  if (EEPROM.read(512) == 0) {
    dhcp_status = 0;
    Ethernet.begin(mac, ip, dns, gateway, subnet);
  } else {
    dhcp_status = 1;
    oled.println("        DHCP ...");
    if (Ethernet.begin(mac) == 0) {
      dhcp_status = 0;
      Ethernet.begin(mac, ip, dns, gateway, subnet);
    }
  }

  // Open serial communications and wait for port to open:
  //Serial.begin(9600);

  // Wyświetlenie wielkości bufora
  int bufferSize = sizeof(packetBuffer) / sizeof(packetBuffer[0]);

  //display info
  //Serial.println("ArtGateOne  : 2.0");
  //Serial.println("Output mode : LED");
  //Serial.print("LED PIN     : ");
  //Serial.println(LED_PIN);
  //Serial.print("LED COUNT   : ");
  //Serial.println(LED_COUNT);

  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    //Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    while (true) {
      delay(1);  // do nothing, no point running without Ethernet hardware
    }
  }
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    //Serial.println("Ethernet    : Ethernet shield was not found.");
  } else if (Ethernet.hardwareStatus() == EthernetW5100) {
    //Serial.println("Ethernet    : W5100");
  } else if (Ethernet.hardwareStatus() == EthernetW5200) {
    //Serial.println("Ethernet    : W5200");
  } else if (Ethernet.hardwareStatus() == EthernetW5500) {
    //Serial.println("Ethernet    : W5500");
  }

  delay(1000);
  auto link = Ethernet.linkStatus();
  //Serial.print("Link status : ");
  switch (link) {
    case Unknown:
      //Serial.println("Unknown");
      break;
    case LinkON:
      //Serial.println("CONNECTED");
      break;
    case LinkOFF:
      //Serial.println("DICONNECTED");
      break;
  }

  // start UDP
  Udp.begin(localPort);

  //display info
  displaydata();

  //Serial.print("Buffer size : ");
  //Serial.println(bufferSize);
  //Serial.print("IP Address  : ");
  //Serial.println(Ethernet.localIP());
  //Serial.print("Counter    ");

  if (EEPROM.read(534) == 1) {
    PlayBootScene();
  }

  makeArtPollReply();

  // Oto przykładowy komunikat NodeReport:
  const char* nodeReport = "#0001 [0000] ArtGateOne Art-Net Product. Good Boot.";

  // Utwórz kopię komunikatu NodeReport w formacie szesnastkowym (HEX)
  for (int i = 0; i < strlen(nodeReport); i++) {
    ArtPollReply[108 + i] = nodeReport[i];
  }

}  // end setup()

void loop() {

  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    // //Serial.println("new client");
    //  an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        strwww += c;
        // Serial.write(c);
        //  if you've gotten to the end of the line (received a newline
        //  character) and the line is blank, the http request has ended,
        //  so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          if (strwww[0] == 71 && strwww[5] == 32) {
            client.println(F("HTTP/1.1 200 OK"));
            client.println(F("Content-Type: text/html, charset=utf-8"));
            client.println(F("Connection: close"));  // the connection will be closed after completion of the response
            client.println(F("User-Agent: ArtGateOne"));
            client.println();

            client.println(F("<!DOCTYPE HTML>"));
            client.println(F("<html lang='en'>"));
            client.println(F("<head>"));
            client.println(F("<link rel='icon' type='image/png' sizes='16x16' href='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABHNCSVQICAgIfAhkiAAAAAFzUkdCAK7OHOkAAAAEZ0FNQQAAsY8L/GEFAAAACXBIWXMAAA7EAAAOxAGVKw4bAAAAX3pUWHRSYXcgcHJvZmlsZSB0eXBlIEFQUDEAAAiZ40pPzUstykxWKCjKT8vMSeVSAANjEy4TSxNLo0QDAwMLAwgwNDAwNgSSRkC2OVQo0QAFmJibpQGhuVmymSmIzwUAT7oVaBst2IwAAAGiSURBVDhPfZI7TwJREIVH0Ig8FHQRRPH1C9TSGAM/guVhjCQGSXzFytigndFKTbTAVu1JNBZEG6w0kRBsaDARwRYCKGAz7l4mi7i7ftXMObNnZ+9dQBUymQyenZ5Sp45qQHg5hACqtoTqhPiwrrML7+/uSVFGIwzKuIvHQTQ4joPzaLQpqtAhplAtEfD5oPb1BcnnJOQ+CuKW5MhR3KBWr8PG5haUSiUwG4xwdXFJjgLiBr853D/Ah0SC1aI96hjG+dk51ishC5iZmqYKkfd4cMQ+9O9tyD7BOmilCiC4FIRisQic2QInx0ek/oGCGJFIBAv5AnVNOoSR8REnTo6NkdJO2y3YLAPALy5ApVxmvV6vh9vrG9BqNPCae4NyuQImk5F5EixG4LvRQL/XT12Lp8dH7NZ2op2z4s72NqktpID1tTUsvOepa0d8z4RzFI3dOlJaSAFOh4MqOavhMDoGbagVgl7SaVKbsIBsNovh0AoTlEilUmyL3h4DBrxeUpuwQ4zFYuB2uaDPbGbnosTe7i70WyxQrX5CXvi93W438DwPP40aa/Jc/0S8AAAAAElFTkSuQmCC'>"));
            client.println(F("<title>ArtGateOne setup</title>"));
            client.println(F("<meta name='viewport' content='width=device-width, initial-scale=1.0'>"));
            client.println(F("<style>"));
            client.println(F("body {text-align: center;}"));
            client.println(F("div {width: 340px;display: inline-block;text-align: center;}"));
            client.println(F("label {width: 130px;display: inline-block;}"));
            client.println(F("input {width: 130px;display: inline-block;}"));
            client.println(F("</style>"));
            client.println(F("</head>"));
            client.println(F("<body>"));
            client.println(F("<div>"));
            client.println(F("<h2>ArtGateOne Setup</h2>"));
            client.println(F("<form action='/ok' method='post'>"));
            client.println(F("<fieldset>"));
            client.println(F("<legend>Ethernet:</legend>"));
            client.println(F("<label for='mode'>Mode:</label>"));
            client.println(F("<select id='mode' name='dhcp'>"));

            if (EEPROM.read(512) == 0) {
              client.println(F("<option value='0' selected> Static </option>"));
              client.println(F("<option value='1'> DHCP </option>"));
            } else {
              client.println(F("<option value='0'> Static </option>"));
              client.println(F("<option value='1' selected> DHCP </option>"));
            }

            client.println(F("</select>"));
            client.println(F("<br>"));
            client.println(F("<label for='ipaddress'>IP Address:</label>"));
            client.print(F("<input type='tel' id='ipaddress' name='ipaddress' value='"));
            client.print(Ethernet.localIP());
            client.println(F("' pattern='((^|\\.)((25[0-5])|(2[0-4]\\d)|(1\\d\\d)|([1-9]?\\d))){4}$' required>"));
            client.println(F("<br>"));
            client.println(F("<label for='subnet'>Subnet mask:</label>"));
            client.print(F("<input type='tel' id='subnet' name='subnet' value='"));
            client.print(Ethernet.subnetMask());
            client.println(F("' pattern='((^|\\.)((25[0-5])|(2[0-4]\\d)|(1\\d\\d)|([1-9]?\\d))){4}$' required>"));
            client.println(F("<br>"));
            client.println(F("<label for='gateway'>Gateway:</label>"));
            client.print(F("<input type='tel' id='gateway' name='gateway' value='"));
            client.print(Ethernet.gatewayIP());
            client.println(F("' pattern='((^|\\.)((25[0-5])|(2[0-4]\\d)|(1\\d\\d)|([1-9]?\\d))){4}$' required>"));
            client.println(F("<br>"));
            client.println(F("<label for='mac'>MAC Address:</label>"));
            client.print(F("<input type='text' id='mac' name='mac' value='"));

            if (EEPROM.read(525) <= 15) {
              client.print(F("0"));
            }
            client.print(EEPROM.read(525), HEX);
            client.print(F(":"));
            if (EEPROM.read(526) <= 15) {
              client.print(F("0"));
            }
            client.print(EEPROM.read(526), HEX);
            client.print(F(":"));
            if (EEPROM.read(527) <= 15) {
              client.print(F("0"));
            }
            client.print(EEPROM.read(527), HEX);
            client.print(F(":"));
            if (EEPROM.read(528) <= 15) {
              client.print(F("0"));
            }
            client.print(EEPROM.read(528), HEX);
            client.print(F(":"));
            if (EEPROM.read(529) <= 15) {
              client.print(F("0"));
            }
            client.print(EEPROM.read(529), HEX);
            client.print(F(":"));
            if (EEPROM.read(530) <= 15) {
              client.print(F("0"));
            }
            client.print(EEPROM.read(530), HEX);

            client.println(F("' pattern='[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}:[A-F0-9]{2}$' required>"));
            client.println(F("<br>"));
            client.println(F("</fieldset>"));
            client.println(F("<br>"));
            client.println(F("<fieldset>"));
            client.println(F("<legend>ArtNet:</legend>"));
            client.println(F("<label for='net'>Net:</label>"));
            client.print(F("<input type='number' id='net' name='net' min='0' max='127' required value='"));
            client.print(EEPROM.read(531));
            client.println(F("'>"));
            client.println(F("<br>"));
            client.println(F("<label for='sub'>Subnet:</label>"));
            client.print(F("<input type='number' id='sub' name='subnet' min='0' max='15' required value='"));
            client.print(EEPROM.read(532));
            client.println(F("'>"));
            client.println(F("<br>"));
            client.println(F("<label for='uni'>Universe:</label>"));
            client.print(F("<input type='number' id='uni' name='universe' min='0' max='15' required value='"));
            client.print(EEPROM.read(533));
            client.println(F("'>"));
            client.println(F("<br>"));
            client.println(F("</fieldset>"));
            client.println(F("<br>"));
            client.println(F("<fieldset>"));
            client.println(F("<legend>Boot:</legend>"));
            client.println(F("<label for='scene'>Startup scene:</label>"));
            client.println(F("<select id='scene' name='scene'>"));
            if (EEPROM.read(534) == 0) {
              client.println(F("<option value='0' selected> Disabled </option>"));
              client.println(F("<option value='1' > Enable </option>"));
            } else {
              client.println(F("<option value='0' > Disable </option>"));
              client.println(F("<option value='1' selected> Enabled </option>"));
            }
            client.println(F("<option value ='2'> Record new scene </option>"));

            client.println(F("</select>"));
            client.println(F("<br>"));
            client.println(F("</fieldset>"));
            client.println(F("<br>"));
            client.println(F("<input type='reset' value='Reset'>"));
            client.println(F("<input type='submit' value='Submit'>"));
            client.println(F("<br>"));
            client.println(F("<br>"));
            client.println(F("<br>"));
            client.println(F("</form>"));
            client.println(F("Art-Net&trade; Designed by and Copyright Artistic Licence Engineering Ltd"));
            client.println(F("</div>"));
            client.println(F("</body>"));
            client.println(F("</html>"));

            delay(10);
            strwww = String();
            client.stop();
            displaydata();
            break;
          }
          if (strwww[0] == 71 && strwww[5] == 102) {  // check favicon
            client.println("HTTP/1.1 200 OK");
            client.println();
            client.stop();
            strwww = String();
            break;
          }
          if (strwww[0] == 80) {  // check POST frame
            datalen = 0;
            char* position = strstr(strwww.c_str(), "Content-Length");
            if (position != NULL) {
              int startIndex = position - strwww.c_str() + 15;  // Adjust the starting index based on the pattern
              char* endLine = strchr(position, '\n');           // Search for the end of the line
              if (endLine != NULL) {
                int endIndex = endLine - strwww.c_str();
                char lengthValue[10];  // Assuming the length value is within 10 digits
                strncpy(lengthValue, strwww.c_str() + startIndex, endIndex - startIndex);
                lengthValue[endIndex - startIndex] = '\0';
                datalen = atoi(lengthValue);
              }
            }
            post = 1;  // ustawia odbior danych
            strwww = String();
            client.println("HTTP/1.1 200 OK");
            // client.println();
            break;
          }
        }
        if (post == 1 && strwww.length() == datalen) {  // recive data
          datadecode();
          delay(1);
          // PRZETWARZA ODEBRANE DANE I WYŚWIETLA STRONE KONCOWA
          client.println(F("HTTP/1.1 200 OK"));
          client.println(F("Content-Type: text/html, charset=utf-8"));
          client.println(F("Connection: close"));  // the connection will be closed after completion of the response
          client.println(F("User-Agent: ArtGateOne"));
          client.println();
          client.println(F("<!DOCTYPE HTML>"));
          client.println(F("<html lang='en'>"));
          client.println(F("<head>"));
          client.println(F("<link rel='icon' type='image/png' sizes='16x16' href='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAQAAAC1+jfqAAAAE0lEQVR42mP8X8+AFzCOKhhJCgAePhfxCE5/6wAAAABJRU5ErkJggg=='>"));  //szara
          client.println(F("<title>ArtGateOne setup</title>"));
          client.println(F("<meta charset='UTF-8'>"));
          client.print(F("<meta http-equiv='refresh' content='5; url=http://"));
          client.print(EEPROM.read(513));
          client.print(F("."));
          client.print(EEPROM.read(514));
          client.print(F("."));
          client.print(EEPROM.read(515));
          client.print(F("."));
          client.print(EEPROM.read(516));
          client.println(F("'>"));
          client.println(F("<meta name='viewport' content='width=device-width, initial-scale=1.0'>"));
          client.println(F("<style>"));
          client.println(F("body {text-align: center;}"));
          client.println(F("div {width:340px; display: inline-block; text-align: center;}"));
          client.println(F("label {width:120px; display: inline-block;}"));
          client.println(F("input {width:120px; display: inline-block;}"));
          client.println(F("</style>"));
          client.println(F("</head>"));
          client.println(F("<body>"));
          client.println(F("<div>"));
          client.println(F("<h2>ArtGateOne</h2>"));
          client.println(F("<h2>Save configuration ...</h2>"));
          client.println(F("</div>"));
          client.println(F("</form>"));
          client.println(F("</body>"));
          client.println(F("</html>"));
          delay(1);
          client.stop();
          strwww = String();
          post = 0;
          if (EEPROM.read(512) == 1) {
            //oled.clear();
            //oled.println("        ArtGateOne");
            oled.println("DHCP...");
            Ethernet.begin(mac);
            Ethernet.maintain();
          } else {
            IPAddress newIp(EEPROM.read(513), EEPROM.read(514), EEPROM.read(515), EEPROM.read(516));
            Ethernet.setLocalIP(newIp);
            IPAddress newSubnet(EEPROM.read(517), EEPROM.read(518), EEPROM.read(519), EEPROM.read(520));
            Ethernet.setSubnetMask(newSubnet);
            IPAddress newGateway(EEPROM.read(521), EEPROM.read(522), EEPROM.read(523), EEPROM.read(524));
            Ethernet.setGatewayIP(newGateway);
          }
          IPAddress localIP = Ethernet.localIP();
          delay(500);
          displaydata();
          makeArtPollReply();
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        } else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    // delay(1);
    // close the connection:
    // client.stop();
    // //Serial.println("client disconnected");
    // //Serial.print(strwww[0]);
    // strwww = String();
  }


  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize) {

    Udp.read(packetBuffer, MAX_BUFFER_SIZE);

    if (memcmp(packetBuffer, "Art-Net", 8) == 0) {
      uint16_t opCode = (packetBuffer[9] << 8) | packetBuffer[8];

      switch (opCode) {
        case 0x2000:
          //Serial.print("ArtPoll");
          invert = !invert;
          oled.invertDisplay(invert);
          updateNodeReportCounter();
          sendArtPollReply();
          break;
        case 0x2100:
          //Serial.println("ArtPollReply");
          break;
        case 0x2300:
          //Serial.println("ArtDiagData");
          break;
        case 0x2400:
          //Serial.println("ArtCommand");
          break;
        case 0x5000:
          //Serial.println("ArtDmx");
          if (packetBuffer[15] == intN && packetBuffer[14] == intUniverse) {

            counter = 0;

            int ledIndex = 18;  // Offset początku danych DMX
            for (unsigned int i = 0; i < LED_COUNT; i++) {
              uint8_t r = packetBuffer[ledIndex];
              uint8_t g = packetBuffer[ledIndex + 1];
              uint8_t b = packetBuffer[ledIndex + 2];
              strip.setPixelColor(i, r, g, b);

              ledIndex += 3;  // Przesunięcie indeksu o 3 bajty na każdą diodę
            }
            strip.show();
          }
          break;
        case 0x5100:
          //Serial.println("ArtNzs");
          break;
        case 0x5200:
          //Serial.println("ArtSync");
          break;
        case 0x6000:
          //Serial.print("ArtAddress ");
          ArtAddress();
          displaydata();
          break;
        case 0x7000:
          //Serial.println("ArtInput");
          break;
        case 0x8000:
          //Serial.println("ArtTodRequest");
          break;
        case 0x8100:
          //Serial.println("ArtTodData");
          break;
        case 0x8200:
          //Serial.println("ArtTodControl");
          break;
        case 0x8300:
          //Serial.println("ArtRdm");
          break;
        case 0x8400:
          //Serial.println("ArtRdmSub");
          break;
        case 0xA010:
          //Serial.println("ArtVideoSetup");
          break;
        case 0xA020:
          //Serial.println("ArtVideoPalette");
          break;
        case 0xA040:
          //Serial.println("ArtVideoData");
          break;
        case 0xF000:
          //Serial.println("ArtMacMaster (Deprecated)");
          break;
        case 0xF100:
          //Serial.println("ArtMacSlave (Deprecated)");
          break;
        case 0xF200:
          //Serial.println("ArtFirmwareMaster");
          break;
        case 0xF300:
          //Serial.println("ArtFirmwareReply");
          break;
        case 0xF400:
          //Serial.println("ArtFileTnMaster");
          break;
        case 0xF500:
          //Serial.println("ArtFileFnMaster");
          break;
        case 0xF600:
          //Serial.println("ArtFileFnReply");
          break;
        case 0xF800:
          //Serial.print("ArtIpProg");
          ArtIpProg();
          updateNodeReportCounter();
          displaydata();
          sendArtIpProgReply();
          break;
        case 0xF900:
          //Serial.println("ArtIpProgReply");
          break;
        case 0x9000:
          //Serial.println("ArtMedia");
          break;
        case 0x9100:
          //Serial.println("ArtMediaPatch");
          break;
        case 0x9200:
          //Serial.println("ArtMediaControl");
          break;
        case 0x9300:
          //Serial.println("ArtMediaControlReply");
          break;
        case 0x9700:
          //Serial.println("ArtTimeCode");
          break;
        case 0x9800:
          //Serial.println("ArtTimeSync");
          break;
        case 0x9900:
          //Serial.println("ArtTrigger");
          break;
        case 0x9A00:
          //Serial.println("ArtDirectory");
          break;
        case 0x9B00:
          //Serial.println("ArtDirectoryReply");
          break;
        default:
          //Serial.println("Unknown Art-Net packet");
          //Serial.println(opCode, HEX);
          break;
      }
    }
  }
}  // end loop()

void ArtIpProg() {

  if ((packetBuffer[14] & 0x80) != 0) {  // Enable programing = True

    //Serial.print("Enable Programing: ");

    if ((packetBuffer[14] & 0x01) != 0) {  // Program Port = True
      //Serial.print("Program Port ");
    }
    if ((packetBuffer[14] & 0x02) != 0) {  // Program Subnet Mask
      //Serial.print("Program Subnet Mask ");
      EEPROM.update(512, 0);
      EEPROM.update(517, packetBuffer[20]);
      EEPROM.update(518, packetBuffer[21]);
      EEPROM.update(519, packetBuffer[22]);
      EEPROM.update(520, packetBuffer[23]);
    }
    if ((packetBuffer[14] & 0x04) != 0) {  // Program IP = True
      //Serial.print("Program IP ");
      EEPROM.update(512, 0);
      EEPROM.update(513, packetBuffer[16]);
      EEPROM.update(514, packetBuffer[17]);
      EEPROM.update(515, packetBuffer[18]);
      EEPROM.update(516, packetBuffer[19]);
    }
    if ((packetBuffer[14] & 0x08) != 0) {  // Reset Parameters = True
      //Serial.print("Reset Parameters ");
      Factory_reset();
      makeArtPollReply();
      displaydata();
      generateNodeReport(0x0010);
    }
    /*if ((packetBuffer[14] & 0x10) != 0) {  // Unused == True
      //Serial.print("Unused ");
    }
    if ((packetBuffer[14] & 0x20) != 0) {  // Unused == True
      //Serial.print("Unused ");
    }*/
    if ((packetBuffer[14] & 0x40) != 0) {  // Enable DHCP = True
      //Serial.print("Enable DHCP ");
      EEPROM.update(512, 1);
    }
    //Serial.println();
    if ((packetBuffer[14] & 0x02) != 0 || (packetBuffer[14] & 0x04) != 0 || (packetBuffer[14] & 0x08) != 0) {
      Ethernet_reset();
      ArtPollReply[10] = Ethernet.localIP()[0];  // IPV4 [0]
      ArtPollReply[11] = Ethernet.localIP()[1];  // IPV4 [1]
      ArtPollReply[12] = Ethernet.localIP()[2];  // IPV4 [2]
      ArtPollReply[13] = Ethernet.localIP()[3];  // IPV4 [3]
    }
  }
  return;
}

void sendArtIpProgReply() {
  // Wypełnij pakiet odpowiedzi ArtIpProgReply
  byte artIpProgReplyPacket[] = {
    // ID
    'A', 'r', 't', '-', 'N', 'e', 't', 0x00,
    // OpCode (0xf900 - ArtIpProgReply)
    0x00, 0xf9,
    // VersInfo
    0x00, 0x0e,
    //filler
    0x00, 0x00, 0x00, 0x00,
    // IP Address
    Ethernet.localIP()[0], Ethernet.localIP()[1], Ethernet.localIP()[2], Ethernet.localIP()[3],
    //Subnet mask //static - no dhcp! ?????
    EEPROM.read(517), EEPROM.read(518), EEPROM.read(519), EEPROM.read(520),
    // Port
    highByte(localPort), lowByte(localPort),
    // Status (0x00 - OK)
    (EEPROM.read(512) * 0x40),
    // Spare2
    0x00,
    //def gateway
    EEPROM.read(521), EEPROM.read(522), EEPROM.read(523), EEPROM.read(524),
    //Spare 7
    0x00,
    //Spare 8
    0x00
  };

  // Rozpocznij komunikację UDP
  if (Udp.beginPacket(Udp.remoteIP(), localPort)) {
    // Wyślij dane pakietu ArtIpProgReply
    Udp.write(artIpProgReplyPacket, sizeof(artIpProgReplyPacket));
    // Zakończ pakiet
    Udp.endPacket();
  }
}

void sendArtPollReply() {
  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());  //send ArtPollReply
  Udp.write(ArtPollReply, 239);
  Udp.endPacket();
  return;
}

void makeArtPollReply() {

  //1.  ID                | 8    | "Art-Net" null-terminated string
  //2.  OpCode            | 16   | OpPollReply (0x2100)
  //3.  IP Address        | 32   | Node's IP Address (4 bytes)
  //4.  Port              | 16   | Port (0x1936)
  //5.  VersInfoH         | 8    | High byte of Node's firmware revision number
  //6.  VersInfoL         | 8    | Low byte of Node's firmware revision number
  //7.  NetSwitch         | 8    | Bits 14-8 of the 15-bit Port-Address (used with SubSwitch)
  //8.  SubSwitch         | 8    | Bits 7-4 of the 15-bit Port-Address (used with NetSwitch)
  //9.  OemHi             | 8    | High byte of the Oem code
  //10. Oem               | 8    | Low byte of the Oem code
  //11. UbeaVersion       | 8    | Firmware version of the User Bios Extension Area (UBEA)
  //12. Status1           | 8    | General status register containing various bit fields
  //13. EstaManLo         | 8    | ESTA manufacturer code (low byte)
  //14. EstaManHi         | 8    | ESTA manufacturer code (high byte)
  //15. ShortName         | 144  | Null-terminated short name for the Node (max 17 characters)
  //16. LongName          | 512  | Null-terminated long name for the Node (max 63 characters)
  //17. NodeReport        | 512  | Textual report of the Node's operating status or errors
  //18. NumPortsHi        | 8    | High byte of the number of input or output ports
  //19. NumPortsLo        | 8    | Low byte of the number of input or output ports
  //20. PortTypes         | 32   | Array defining the operation and protocol of each channel
  //21. GoodInput         | 32   | Array defining input status of the node
  //22. GoodOutputA       | 32   | Array defining output status of the node
  //23. SwIn              | 32   | Bits 3-0 of the 15-bit Port-Address for each of the 4 possible input ports
  //24. SwOut             | 32   | Bits 3-0 of the 15-bit Port-Address for each of the 4 possible output ports
  //25. AcnPriority       | 8    | The sACN priority value used when converting received DMX to sACN
  //26. SwMacro           | 8    | Macro trigger values for remote event triggering or cueing
  //27. SwRemote          | 8    | Remote trigger values for remote event triggering or cueing
  //28. Spare             | 8    | Not used, set to zero
  //29. Spare             | 8    | Not used, set to zero
  //30. Spare             | 8    | Not used, set to zero
  //31. Style             | 8    | Equipment style of the device (Style codes defined in Table 4)
  //32. MAC Hi            | 8    | MAC Address Hi Byte (set to zero if not available)
  //33. MAC               | 8    | MAC Address (6 bytes in total)
  //34. MAC               | 8    | MAC Address
  //35. MAC               | 8    | MAC Address
  //36. MAC               | 8    | MAC Address
  //37. MAC Lo            | 8    | MAC Address Lo Byte
  //38. BindIp            | 32   | IP of the root device (if part of a larger or modular product)
  //39. BindIndex         | 8    | Order of bound devices (0 or 1 means root device)
  //40. Status2           | 8    | General status register containing various bit fields
  //41. GoodOutputB       | 32   | Array defining output status of the node (additional status)
  //42. Status3           | 8    | General status register containing various bit fields (additional status)
  //43. DefaultRespUID Hi | 8    | RDMnet & LLRP Default Responder UID MSB
  //44. DefaultRespUID    | 8    | RDMnet & LLRP Default Responder UID
  //45. DefaultRespUID    | 8    | RDMnet & LLRP Default Responder UID
  //46. DefaultRespUID    | 8    | RDMnet & LLRP Default Responder UID
  //47. DefaultRespUID Lo | 8    | RDMnet & LLRP Default Responder UID LSB
  //48. UserHi            | 8    | Available for user specific data
  //49. UserLo            | 8    | Available for user specific data
  //50. RefreshRateHi     | 8    | Hi byte of RefreshRate (maximum refresh rate)
  //51. RefreshRateLo     | 8    | Lo byte of RefreshRate (maximum refresh rate)
  //52. Filler            | 88   | Transmit as zero (for future expansion)


  for (int i = 0; i <= 239; i++) {  // Clear
    ArtPollReply[i] = 0x00;
  }
  ArtPollReply[0] = byte('A');  //  ID[8]
  ArtPollReply[1] = byte('r');
  ArtPollReply[2] = byte('t');
  ArtPollReply[3] = byte('-');
  ArtPollReply[4] = byte('N');
  ArtPollReply[5] = byte('e');
  ArtPollReply[6] = byte('t');
  ArtPollReply[7] = 0x00;

  ArtPollReply[8] = 0x00;  // OpCode[0]
  ArtPollReply[9] = 0x21;  // OpCode[1]

  ArtPollReply[10] = Ethernet.localIP()[0];  // IPV4 [0]
  ArtPollReply[11] = Ethernet.localIP()[1];  // IPV4 [1]
  ArtPollReply[12] = Ethernet.localIP()[2];  // IPV4 [2]
  ArtPollReply[13] = Ethernet.localIP()[3];  // IPV4 [3]

  ArtPollReply[14] = 0x36;  // IP Port Low
  ArtPollReply[15] = 0x19;  // IP Port Hi

  ArtPollReply[16] = 0x02;  // High byte of Version
  ArtPollReply[17] = 0x00;  // Low byte of Version

  ArtPollReply[18] = intN;  // NetSwitch

  ArtPollReply[19] = intS;  // Net Sub Switch

  ArtPollReply[20] = 0xFF;  // OEMHi

  ArtPollReply[21] = 0xFF;  // OEMLo

  ArtPollReply[22] = 0x00;  // Ubea Version

  if (EEPROM.read(535) == 2) {  //Status1
    ArtPollReply[23] = 0xE0;    // Status1 normal
    ledNormal();
  }

  else if (EEPROM.read(535) == 3) {
    ArtPollReply[23] = 0xA0;  // Status1 mute
    ledMute();
  }

  else if (EEPROM.read(535) == 4) {
    ArtPollReply[23] = 0x60;  // Status1 locate/identify
    ledIdentify();
  }

  ArtPollReply[24] = 0x00;  // ESTA LO
  ArtPollReply[25] = 0x00;  // ESTA HI

  if (EEPROM.read(537) > 0) {
    for (int i = 0; i < 18; i++) {
      ArtPollReply[26 + i] = EEPROM.read(537 + i);
      if (ArtPollReply[26 + i] == 0) {
        break;
      }
    }
  } else {
    ArtPollReply[26] = byte('L');  //Short Name [18]
    ArtPollReply[27] = byte('E');
    ArtPollReply[28] = byte('D');
    //...
  }

  if (EEPROM.read(555) > 0) {
    for (int i = 0; i < 64; i++) {
      ArtPollReply[44 + i] = EEPROM.read(555 + i);
      if (ArtPollReply[44 + i] == 0) {
        break;
      }
    }
  } else {
    ArtPollReply[44] = byte('A');  //Long Name [64]
    ArtPollReply[45] = byte('r');
    ArtPollReply[46] = byte('t');
    ArtPollReply[47] = byte('G');
    ArtPollReply[48] = byte('a');
    ArtPollReply[49] = byte('t');
    ArtPollReply[50] = byte('e');
    ArtPollReply[51] = byte('O');
    ArtPollReply[52] = byte('n');
    ArtPollReply[53] = byte('e');
    ArtPollReply[54] = byte(' ');
    ArtPollReply[55] = byte('2');
    ArtPollReply[56] = byte(' ');
    ArtPollReply[57] = byte(' ');
    ArtPollReply[58] = byte('L');
    ArtPollReply[59] = byte('E');
    ArtPollReply[60] = byte('D');
    //...
  }

  generateNodeReport(nodeReportCode);  // NodeReport [64]

  //ArtPollReply[108] = 0x00;   // NodeReport [64]
  //...

  ArtPollReply[172] = 0x00;  // NumPorts Hi
  ArtPollReply[173] = 0x01;  // NumPorts Lo

  ArtPollReply[174] = 0x80;  // Port Types [0]
  ArtPollReply[175] = 0x00;  // Port Types [1]
  ArtPollReply[176] = 0x00;  // Port Types [2]
  ArtPollReply[177] = 0x00;  // Port Types [3]

  ArtPollReply[178] = 0x08;  // GoodInput [0]
  ArtPollReply[179] = 0x00;  // GoodInput [1]
  ArtPollReply[180] = 0x00;  // GoodInput [2]
  ArtPollReply[181] = 0x00;  // GoodInput [3]

  ArtPollReply[182] = 0x80;  // GoodOutputA [0]
  ArtPollReply[183] = 0x00;  // GoodOutputA [1]
  ArtPollReply[184] = 0x00;  // GoodOutputA [2]
  ArtPollReply[185] = 0x00;  // GoodOutputA [3]

  ArtPollReply[186] = 0x00;  // SwIn [0]
  ArtPollReply[187] = 0x00;  // SwIn [1]
  ArtPollReply[188] = 0x00;  // SwIn [2]
  ArtPollReply[189] = 0x00;  // SwIn [3]

  ArtPollReply[190] = intU;  // SwOut [0]
  ArtPollReply[191] = 0x00;  // SwOut [1]
  ArtPollReply[192] = 0x00;  // SwOut [2]
  ArtPollReply[193] = 0x00;  // SwOut [3]


  ArtPollReply[194] = 0x00;  // AcnPriority

  ArtPollReply[195] = 0x00;  // SwMacro

  ArtPollReply[196] = 0x00;  // SwRemote

  ArtPollReply[197] = 0x00;  // Spare
  ArtPollReply[198] = 0x00;  // Spare
  ArtPollReply[199] = 0x00;  // Spare

  ArtPollReply[200] = 0x00;  // Style

  // MAC ADDRESS
  ArtPollReply[201] = mac[0];  // MAC HI
  ArtPollReply[202] = mac[1];  // MAC
  ArtPollReply[203] = mac[2];  // MAC
  ArtPollReply[204] = mac[3];  // MAC
  ArtPollReply[205] = mac[4];  // MAC
  ArtPollReply[206] = mac[5];  // MAC LO

  ArtPollReply[207] = Ethernet.localIP()[0];  // BindIp [0]
  ArtPollReply[208] = Ethernet.localIP()[1];  // BindIp [1]
  ArtPollReply[209] = Ethernet.localIP()[2];  // BindIp [2]
  ArtPollReply[210] = Ethernet.localIP()[3];  // BindIp [3]
  ArtPollReply[211] = 0x01;                   // BindIndex

  ArtPollReply[212] = 0x0D;  // Status2
  if (dhcp_status == 1) {
    ArtPollReply[212] = 0x0F;  // DHCP USED
  }

  ArtPollReply[213] = 0x00;  // GoodOutputB [4]
  ArtPollReply[214] = 0x00;  // GoodOutputB [4]
  ArtPollReply[215] = 0x00;  // GoodOutputB [4]
  ArtPollReply[216] = 0x00;  // GoodOutputB [4]

  ArtPollReply[217] = EEPROM.read(536);  // Status3

  ArtPollReply[218] = 0x00;  // DefaulRespUID Hi
  ArtPollReply[219] = 0x00;  // DefaulRespUID
  ArtPollReply[220] = 0x00;  // DefaulRespUID
  ArtPollReply[221] = 0x00;  // DefaulRespUID
  ArtPollReply[222] = 0x00;  // DefaulRespUID
  ArtPollReply[223] = 0x00;  // DefaulRespUID Lo

  ArtPollReply[224] = 0x00;  // UserHi
  ArtPollReply[225] = 0x00;  // UserLo

  ArtPollReply[226] = 0x00;  // RefreshRateHi
  ArtPollReply[227] = 0x00;  // RefreshRateLo

  //Filler 11 x 8 Transmit as zero. For future expansion.

  return;
}

void Factory_reset() {
  EEPROM.update(512, 0);  // DHCP 0=off, 1=on
  EEPROM.update(513, 2);  // IP
  EEPROM.update(514, 3);
  EEPROM.update(515, 4);
  EEPROM.update(516, 5);
  EEPROM.update(517, 255);  // SubNetMask
  EEPROM.update(518, 0);
  EEPROM.update(519, 0);
  EEPROM.update(520, 0);
  EEPROM.update(521, 0);  // gateway
  EEPROM.update(522, 0);
  EEPROM.update(523, 0);
  EEPROM.update(524, 0);
  EEPROM.update(525, mac[0]);  // mac adres
  EEPROM.update(526, mac[1]);  // mac
  EEPROM.update(527, mac[2]);  // mac
  EEPROM.update(528, mac[3]);  // mac
  EEPROM.update(529, mac[4]);  // mac
  EEPROM.update(530, mac[5]);  // mac
  EEPROM.update(531, 0);       // Art-Net Net
  EEPROM.update(532, 0);       // Art-Net Sub
  EEPROM.update(533, 1);       // Art-Net Uni
  EEPROM.update(534, 0);       // boot scene
  EEPROM.update(535, 2);       // Status 1 (normal)
  EEPROM.update(536, 32);      // Status 3 (hold)
  EEPROM.update(537, 0);       // Short Name
  EEPROM.update(555, 0);       // Long Name
  EEPROM.update(1023, 1);      // komórka kontrolna
  if (Serial) {
  }
  oled.println("        RESET");
  return;
}

void Ethernet_reset() {

  if (EEPROM.read(512) == 1) {
    Ethernet.maintain();
    Ethernet.begin(mac);

  } else {
    IPAddress ip(EEPROM.read(513), EEPROM.read(514), EEPROM.read(515), EEPROM.read(516));
    Ethernet.setLocalIP(ip);
    IPAddress subnet(EEPROM.read(517), EEPROM.read(518), EEPROM.read(519), EEPROM.read(520));
    Ethernet.setSubnetMask(subnet);
    IPAddress gateway(EEPROM.read(521), EEPROM.read(522), EEPROM.read(523), EEPROM.read(524));
    Ethernet.setGatewayIP(gateway);
  }
  makeArtPollReply();
  //IPAddress localIP = Ethernet.localIP();
  return;
}

void ArtAddress() {

  //0 - 7      : ID
  //8 - 9      : OpCode
  //10         : ProtVerHi
  //11         : ProtVerLo
  //12         : NetSwitch
  //13         : BindIndex
  //14 - 31    : Short Name
  //32 - 95    : Long Name
  //96 - 99    : SwIn
  //100 - 103  : SwOut
  //104        : SubSwitch
  //105        : AcnPriority
  //106        : Command


  if ((packetBuffer[12] & 0x80) != 0) {  //Net Switch
    // Odczytaj wartość pola "NetSwitch"
    byte netSwitchValue = packetBuffer[12] & 0x7F;  // Wyzeruj bit 7, aby otrzymać właściwą wartość "NetSwitch"
    ArtPollReply[18] = netSwitchValue;              // NetSwitch
    EEPROM.update(531, netSwitchValue);
  }

  if (packetBuffer[32] != 0) {  //long name
    String longName;
    int i;
    for (i = 0; i < 64; i++) {
      char currentChar = packetBuffer[32 + i];
      ArtPollReply[44 + i] = packetBuffer[32 + i];
      EEPROM.update((555 + i), packetBuffer[32 + i]);
      if (currentChar == 0) {
        break;
      }
      longName += currentChar;
    }
    generateNodeReport(0x0007);
  }

  if (packetBuffer[14] != 0) {  //short name
    String shortName;
    int i;
    for (i = 0; i < 18; i++) {
      char currentChar = packetBuffer[14 + i];
      ArtPollReply[26 + i] = packetBuffer[14 + i];
      EEPROM.update((537 + i), packetBuffer[14 + i]);
      if (currentChar == 0) {
        break;
      }
      shortName += currentChar;
    }
    generateNodeReport(0x0006);
  }

  if ((packetBuffer[100] & 0x80) != 0) {  //SwOut 0 Switch
    // Odczytaj wartość pola "SwOut"
    byte out0subSwitchValue = packetBuffer[100] & 0x7F;  // Wyzeruj bit 7, aby otrzymać właściwą wartość ""
    ArtPollReply[190] = out0subSwitchValue;              // SwOut 0
    EEPROM.update(533, out0subSwitchValue);
  }

  if ((packetBuffer[104] & 0x80) != 0) {  //Sub Switch
    // Odczytaj wartość pola "SubSwitch"
    byte subSwitchValue = packetBuffer[104] & 0x7F;  // Wyzeruj bit 7, aby otrzymać właściwą wartość "SubSwitch"
    ArtPollReply[19] = subSwitchValue;               // SubSwitch
    EEPROM.update(532, subSwitchValue);
  }

  if (packetBuffer[106] != 0) {  //Command

    switch (packetBuffer[106]) {
      case 0x00:
        // No action
        break;
      case 0x01:
        // Merge If Node is currently in merge mode, cancel merge mode upon receipt of next ArtDmx packet. See discussion of merge mode operation.
        break;
      case 0x02:
        // The front panel indicators of the Node operate normally.
        ArtPollReply[23] = 0xE0;  // Status1
        EEPROM.update(535, 2);    // Status 1 (normal)
        ledNormal();
        break;
      case 0x03:
        // The front panel indicators of the Node are disabled and switched off.
        ArtPollReply[23] = 0xA0;  // Status1
        EEPROM.update(535, 3);    // Status 1 (mute)
        ledMute();
        break;
      case 0x04:
        // Rapid flashing of the Node’s front panel indicators. It is intended as an outlet identifier for large installations.
        ArtPollReply[23] = 0x60;  // Status1
        EEPROM.update(535, 4);    // Status 1 (identify)
        ledIdentify();
        break;
      case 0x05:
        //Serial.println("AcResetRx Flags");  // Resets the Node’s Sip, Art-Net 4 Protocol Release V1.4 Document Revision 1.4dh 19/7/2023 - 39 - Text, Test and data error flags. If an output short is being flagged, forces the test to re-run.
        break;
      case 0x06:
        //Serial.println("AcAnalysisOn");  // Enable analysis and debugging mode.
        break;
      case 0x07:
        //Serial.println("AcAnalysisOff");  // Disable analysis and debugging mode.
        break;
      // Failsafe configuration commands: These settings should be retained by the node during power cycling
      case 0x08:
        //Serial.println("AcFailHold");  // Set the node to hold last state in the event of loss of network data.
        //EEPROM.update(534, 0);  // disable BootScene
        EEPROM.update(536, 0x20);
        ArtPollReply[217] = 0x20;  //Hold
        break;
      case 0x09:
        //Serial.println("AcFailZero");  // Set the node’s outputs to zero in the event of loss of network data.
        EEPROM.update(536, 0x60);
        ArtPollReply[217] = 0x60;  //zero
        FailZero();
        break;
      case 0x0A:
        //Serial.println("AcFailFull");  // Set the node’s outputs to full in the event of loss of network data.
        EEPROM.update(536, 0xA0);
        ArtPollReply[217] = 0xA0;  //full
        FailFull();
        break;
      case 0x0B:
        //Serial.println("AcFailScene");  // Set the node’s outputs to play the failsafe scene in the event of loss of network data.
        //EEPROM.update(534, 1);  // Enable BootScene
        EEPROM.update(536, 0xE0);
        ArtPollReply[217] = 0xE0;  //Playback
        PlayBootScene();
        break;
      case 0x0C:
        //Serial.println("AcFailRecord");  // Record the current output state as the failsafe scene.
        //EEPROM.update(534, 1);  // Enable BootScene
        RecordBootScene();
        break;
      // Node configuration commands: Note that Ltp / Htp and direction settings should be retained by the node during power cycling.
      case 0x10:
        //Serial.println("AcMergeLtp0");  // Set DMX Port 0 to Merge in LTP mode.
        break;
      case 0x11:
        //Serial.println("AcMergeLtp1");  // Set DMX Port 1 to Merge in LTP mode.
        break;
      case 0x12:
        //Serial.println("AcMergeLtp2");  // Set DMX Port 2 to Merge in LTP mode.
        break;
      case 0x13:
        //Serial.println("AcMergeLtp3");  // Set DMX Port 3 to Merge in LTP mode.
        break;
      case 0x20:
        //Serial.println("AcDirectionTx0");  // Set Port 0 direction to output.
        break;
      case 0x21:
        //Serial.println("AcDirectionTx1");  // Set Port 1 direction to output.
        break;
      case 0x22:
        //Serial.println("AcDirectionTx2");  // Set Port 2 direction to output.
        break;
      case 0x23:
        //Serial.println("AcDirectionTx3");  // Set Port 3 direction to output.
        break;
      case 0x30:
        //Serial.println("AcDirectionRx0");  // Set Port 0 direction to input.
        break;
      case 0x31:
        //Serial.println("AcDirectionRx1");  // Set Port 1 direction to input.
        break;
      case 0x32:
        //Serial.println("AcDirectionRx2");  // Set Port 2 direction to input.
        break;
      case 0x33:
        //Serial.println("AcDirectionRx3");  // Set Port 3 direction to input.
        break;
      case 0x50:
        //Serial.println("AcMergeHtp0");  // Set DMX Port 0 to Merge in HTP (default) mode.
        break;
      case 0x51:
        //Serial.println("AcMergeHtp1");  // Set DMX Port 1 to Merge in HTP (default) mode.
        break;
      case 0x52:
        //Serial.println("AcMergeHtp2");  // Set DMX Port 2 to Merge in HTP (default) mode.
        break;
      case 0x53:
        //Serial.println("AcMergeHtp3");  // Set DMX Port 3 to Merge in HTP (default) mode.
        break;
      case 0x60:
        //Serial.println("AcArtNetSel0");  // Set DMX Port 0 to output both DMX512 and RDM packets from the Art-Net protocol (default).
        break;
      case 0x61:
        //Serial.println("AcArtNetSel1");  // Set DMX Port 1 to output both DMX512 and RDM packets from the Art-Net protocol (default).
        break;
      case 0x62:
        //Serial.println("AcArtNetSel2");  // Set DMX Port 2 to output both DMX512 and RDM packets from the Art-Net protocol (default).
        break;
      case 0x63:
        //Serial.println("AcArtNetSel3");  // Set DMX Port 3 to output both DMX512 and RDM packets from the Art-Net protocol (default).
        break;
      case 0x70:
        //Serial.println("AcAcnSel0");  // Set DMX Port 0 to output DMX512 data from the sACN protocol and RDM data from the Art-Net protocol.
        break;
      case 0x71:
        //Serial.println("AcAcnSel1");  // Set DMX Port 1 to output DMX512 data from the sACN protocol and RDM data from the Art-Net protocol.
        break;
      case 0x72:
        //Serial.println("AcAcnSel2");  // Set DMX Port 2 to output DMX512 data from the sACN protocol and RDM data from the Art-Net protocol.
        break;
      case 0x73:
        //Serial.println("AcAcnSel3");  // Set DMX Port 3 to output DMX512 data from the sACN protocol and RDM data from the Art-Net protocol.
        break;
      case 0x90:
        //Serial.println("AcClearOp0");  // Clear DMX Output buffer for Port 0.
        break;
      case 0x91:
        //Serial.println("AcClearOp1");  // Clear DMX Output buffer for Port 1.
        break;
      case 0x92:
        //Serial.println("AcClearOp2");  // Clear DMX Output buffer for Port 2.
        break;
      case 0x93:
        //Serial.println("AcClearOp3");  // Clear DMX Output buffer for Port 3.
        break;
      case 0xA0:
        //Serial.println("AcStyleDelta0");  // Set output style to delta mode (DMX frame triggered by ArtDmx) for Port 0.
        break;
      case 0xA1:
        //Serial.println("AcStyleDelta1");  // Set output style to delta mode (DMX frame triggered by ArtDmx) for Port 1.
        break;
      case 0xA2:
        //Serial.println("AcStyleDelta2");  // Set output style to delta mode (DMX frame triggered by ArtDmx) for Port 2.
        break;
      case 0xA3:
        //Serial.println("AcStyleDelta3");  // Set output style to delta mode (DMX frame triggered by ArtDmx) for Port 3.
        break;
      case 0xB0:
        //Serial.println("AcStyleConst0");  // Set output style to constant mode (DMX output is continuous) for Port 0.
        break;
      case 0xB1:
        //Serial.println("AcStyleConst1");  // Set output style to constant mode (DMX output is continuous) for Port 1.
        break;
      case 0xB2:
        //Serial.println("AcStyleConst2");  // Set output style to constant mode (DMX output is continuous) for Port 2.
        break;
      case 0xB3:
        //Serial.println("AcStyleConst3");  // Set output style to constant mode (DMX output is continuous) for Port 3.
        break;
      case 0xC0:
        //Serial.println("AcRdmEnable0");  // Enable RDM for Port 0.
        break;
      case 0xC1:
        //Serial.println("AcRdmEnable1");  // Enable RDM for Port 1.
        break;
      case 0xC2:
        //Serial.println("AcRdmEnable2");  // Enable RDM for Port 2.
        break;
      case 0xC3:
        //Serial.println("AcRdmEnable3");  // Enable RDM for Port 3.
        break;
      case 0xD0:
        //Serial.println("AcRdmDisable0");  // Disable RDM for Port 0.
        break;
      case 0xD1:
        //Serial.println("AcRdmDisable1");  // Disable RDM for Port 1.
        break;
      case 0xD2:
        //Serial.println("AcRdmDisable2");  // Disable RDM for Port 2.
        break;
      case 0xD3:
        //Serial.println("AcRdmDisable3");  // Disable RDM for Port 3.
        break;
      default:
        //Serial.print("Unknown CODE ");
        //Serial.println(packetBuffer[106], HEX);
        break;
    }
    displaydata();  //???? i dont know need or not
  }
  updateNodeReportCounter();
  //makeArtPollReply();
  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());  //send ArtPollReply
  Udp.write(ArtPollReply, 239);
  Udp.endPacket();
  return;
}

// Function to turn off the LED completely
void ledMute() {
  cli();                                // Disable interrupts to safely modify timer registers
  TCCR1A = 0;                           // Turn off PWM mode
  TCCR1B = 0;                           // Turn off the timer
  sei();                                // Enable interrupts
  ledMode = 3;                          // Set LED mode to 3 (LED is muted)
  ledState = LOW;                       // Set the LED state to LOW (off)
  digitalWrite(LED_BUILTIN, ledState);  // Turn off the LED
  return;
}

// Function to make the LED identify (blink rapidly)
void ledIdentify() {
  if (ledMode != 4) {                     // Check if LED mode is not already set to 4
    ledMode = 4;                          // Set LED mode to 4 (LED is in identification mode)
    cli();                                // Disable interrupts to safely modify timer registers
    TCCR1A = 0;                           // Turn off PWM mode
    TCCR1B = 0;                           // Turn off the timer
    OCR1A = 0xF424;                       // Set the compare value for a frequency of 1 Hz (for prescaler 256 and 16 MHz clock)
    TCCR1B = (1 << WGM12) | (1 << CS12);  // Set the timer to CTC mode and use prescaler 256
    TIMSK1 = (1 << OCIE1A);               // Enable Compare Match A interrupt
    sei();                                // Enable interrupts
  } else {
    counter = 0;  // Reset the counter if already in identify mode
  }
  return;
}

// Function to make the LED behave normally (blinking slowly)
void ledNormal() {
  if (ledMode != 2) {                     // Check if LED mode is not already set to 2
    ledMode = 2;                          // Set LED mode to 2 (LED is in normal mode)
    cli();                                // Disable interrupts to safely modify timer registers
    TCCR1A = 0;                           // Turn off PWM mode
    TCCR1B = 0;                           // Turn off the timer
    OCR1A = 0xF424;                       // Set the compare value for a frequency of 1 Hz (for prescaler 256 and 16 MHz clock)
    TCCR1B = (1 << WGM12) | (1 << CS12);  // Set the timer to CTC mode and use prescaler 256
    TIMSK1 = (1 << OCIE1A);               // Enable Compare Match A interrupt
    sei();                                // Enable interrupts
  } else {
    counter = 0;  // Reset the counter if already in normal mode
  }
  return;
}

void generateNodeReport(uint16_t reportCode) {
  nodeReportCode = reportCode;
  NodeReportCounter = (NodeReportCounter + 1) % 10000;
  //Serial.print("NodeReportCounter: ");
  //Serial.print(" : ");
  //Serial.println(NodeReportCounter);

  // Szukamy opisu na podstawie kodu raportu w tablicy
  const char* description = "Unknown";  // Domyślny opis - "Unknown" (nieznany)

  for (int i = 0; i < sizeof(nodeReportTable) / sizeof(nodeReportTable[0]); i++) {
    if (nodeReportTable[i].code == reportCode) {
      description = nodeReportTable[i].description;
      break;
    }
  }

  // Formatowanie komunikatu NodeReport z kodem i opisem
  snprintf(nodeReport, nodeReportLength, "#%04X [%04d] %s", reportCode, NodeReportCounter, description);

  // Uzupełnij pozostałe miejsca w polu NodeReport zerami
  int descriptionLength = strlen(description);
  for (int i = 10 + 4 + 1 + descriptionLength; i < nodeReportLength - 1; i++) {
    nodeReport[i] = 0x00;
  }

  // Utworzenie kopii komunikatu NodeReport w formacie szesnastkowym (HEX)
  for (int i = 0; i < nodeReportLength; i++) {
    ArtPollReply[108 + i] = nodeReport[i];
  }

  // ...
  // Wypełnij pozostałe miejsca w polu NodeReport zerami (jeśli to konieczne)
  // ...
  return;
}

void updateNodeReportCounter() {
  NodeReportCounter = (NodeReportCounter + 1) % 10000;
  //Serial.print("Updated NodeReportCounter: ");
  //Serial.print(" : ");
  //Serial.println(NodeReportCounter);

  // Aktualizacja tylko licznika w komunikacie NodeReport (od ArtPollReply[115])
  snprintf(nodeReport, nodeReportLength, "%04d", NodeReportCounter);

  // Utworzenie kopii komunikatu NodeReport w formacie szesnastkowym (HEX)
  for (int i = 0; i < 4; i++) {
    ArtPollReply[115 + i] = nodeReport[i];
  }
  return;
}

// Interrupt Service Routine for Timer 1 Compare Match A
ISR(TIMER1_COMPA_vect) {

  if (ledMode == 4) {                       // If LED is in identification mode (blink rapidly)
    ledState = !ledState;                   // Toggle the LED state
    digitalWrite(LED_BUILTIN, ledState);    // Update the LED state
  } else if (ledMode == 2) {                // If LED is in normal mode (blinking slowly)
    if (counter < 5) {                      // If the counter reaches a specific value (6 in this case)
      ledState = HIGH;                      // Set the LED state to LOW (off)
      digitalWrite(LED_BUILTIN, ledState);  // Turn off the LED
    } else if (counter == 5) {              // If the counter reaches a specific value (6 in this case)
      ledState = LOW;                       // Set the LED state to LOW (off)
      digitalWrite(LED_BUILTIN, ledState);  // Turn off the LED

      //if Hold - do nothing - of play zero or full - send data to dmx buffer
      if (ArtPollReply[217] == 0x60) {  //zero
        FailZero();
      } else if (ArtPollReply[217] == 0xA0) {  //full
        FailFull();
      } else if (ArtPollReply[217] == 0xE0) {  //Play
        PlayBootScene();
      }
    } else if (counter > 5) {
      ledState = LOW;                       // Set the LED state to LOW (off)
      digitalWrite(LED_BUILTIN, ledState);  // Turn off the LED
      counter = 5;
    }
    counter++;  // Increment the counter
  }
}  // end ISR()

void PlayBootScene() {
  for (int i = 0; i < strip.numPixels(); i++) {
    byte r = EEPROM.read(i * 3);
    byte g = EEPROM.read((i * 3) + 1);
    byte b = EEPROM.read((i * 3) + 2);

    strip.setPixelColor(i, r, g, b);
  }
  strip.show();
  return;
}  // end PlayBootScene()

void RecordBootScene() {

  for (int i = 0; i <= (strip.numPixels() - 1); i++) {
    uint32_t color = strip.getPixelColor(i);  // Pobierz kolor jako 32-bitową wartość

    byte r = (color >> 16) & 0xFF;  // Wyodrębnij składową R
    byte g = (color >> 8) & 0xFF;   // Wyodrębnij składową G
    byte b = color & 0xFF;          // Wyodrębnij składową B

    EEPROM.update((i * 3), r);
    EEPROM.update(((i * 3) + 1), g);
    EEPROM.update(((i * 3) + 2), b);
  }

}  // end RecordBootScene()

void FailFull() {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, 255, 255, 255);
  }
  strip.show();
  return;
}  // end FailFull()

void FailZero() {
  for (unsigned int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, 0, 0, 0);
  }
  strip.show();
  return;
}  // emd FailZero()

void datadecode() {
  int j = 0;
  for (unsigned int i = 0; i <= datalen; i++) {
    if (strwww[i] == 61) {  // jeśli znajdzie znak równości
      j++;
      i++;
      if (j == 1) {  // DHCP
        EEPROM.update(512, (strwww[i] - 48));
      }
      if (j == 2) {  // IP ADDRES
        data = dataadd(i);
        EEPROM.update(513, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(514, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(515, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(516, data);
      }
      if (j == 3) {  // SUBNET
        data = dataadd(i);
        EEPROM.update(517, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(518, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(519, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(520, data);
      }
      if (j == 4) {  // GATEWAY
        data = dataadd(i);
        EEPROM.update(521, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(522, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(523, data);
        i = i + 2;
        if (data >= 10) {
          i++;
        }
        if (data >= 100) {
          i++;
        }
        data = dataadd(i);
        EEPROM.update(524, data);
      }
      if (j == 5) {  // MAC
        EEPROM.update(525, datamac(i));
        i = i + 5;
        EEPROM.update(526, datamac(i));
        i = i + 5;
        EEPROM.update(527, datamac(i));
        i = i + 5;
        EEPROM.update(528, datamac(i));
        i = i + 5;
        EEPROM.update(529, datamac(i));
        i = i + 5;
        EEPROM.update(530, datamac(i));
      }
      if (j == 6) {  // NET
        data = dataadd(i);
        EEPROM.update(531, data);
        intN = data;  // NET
      }
      if (j == 7) {  // SUBNET
        data = dataadd(i);
        EEPROM.update(532, data);
        intS = data;  // Subnet
      }
      if (j == 8) {  // UNIVERSE
        data = dataadd(i);
        EEPROM.update(533, data);
        intU = data;  // Universe
        intUniverse = ((intN * 256) + (intS * 16) + intU);
      }
      if (j == 9) {  // SCENE
        int data = (strwww[i] - 48);
        if (data <= 1) {
          EEPROM.update(534, data);
        } else {
          EEPROM.update(534, 1);
          // nagraj data do eprom
          RecordBootScene();
        }
      }
    }
  }
}  // end datadecode()

int dataadd(int i) {
  data = 0;
  while (strwww[i] != 38 && strwww[i] != 46) {
    data = ((data * 10) + (strwww[i] - 48));
    i++;
  }
  return data;
}  // end dataadd()

int datamac(int i) {
  data = strwww[i];
  if (data <= 57) {
    data = data - 48;
  } else if (data >= 65) {
    data = data - 55;
  }

  data = data * 16;

  if (strwww[i + 1] <= 57) {
    data = data + (strwww[i + 1] - 48);
  } else if (strwww[i + 1] >= 65) {
    data = data + (strwww[i + 1] - 55);
  }
  return data;
}  // end datamac()

void displaydata() {
  oled.clear();
  oled.print(" IP : ");
  oled.println(Ethernet.localIP());

  /*
  oled.print(" Mask: ");
  oled.println(Ethernet.subnetMask());
  */

  /*
  oled.print(" Net ");
  oled.print(intN, HEX);
  oled.print("  Sub ");
  oled.print(intS, HEX);
  oled.print("  Uni ");
  oled.print(intU, HEX);
  */

  intN = EEPROM.read(531);  // NET
  intS = EEPROM.read(532);  // Subnet
  intU = EEPROM.read(533);  // Universe
  intUniverse = ((intN * 256) + (intS * 16) + intU);

  oled.print(" Universe ");
  oled.print(intUniverse);
  if (EEPROM.read(534) == 1) {
    oled.println("  BS");
  } else {
    oled.println();
  }

  return;
}  // end displaydata()
