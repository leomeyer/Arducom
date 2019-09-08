// ESP8266HelloWorld
// by Leo Meyer <leo@leomeyer.de>

// This sketch can be used to test Arducom on an ESP8266.
// It exposes 512 bytes of EEPROM and a few RAM variables.

// This example code is in the public domain.

#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Werror"

#include <ArducomESP8266.h>
#include <EEPROM.h>

// Machine-specific secrets. Can be put in a folder "Secrets" in your library folder.
// Delete this line if you don't want to keep the secrets local.
#include <secrets.h>

// Networks to connect to (in the specified order).
WiFiNetwork myNetworks[] = {
// the WLAN definitions are from the secrets.h file on the local machine
    { WLAN1 },
    { WLAN2 },
// if there is no secrets.h file define them directly:    
//  { "mySSID", "myPassword" }    
    {nullptr, nullptr}  // mandatory
};

// Static IP address configuration
WiFiAddresses myAddresses {
  .staticIP = IPAddress(192, 168, 0, 248),
  .gateway = IPAddress (192, 168, 0, 1),
  .subnet = IPAddress (255, 255, 255, 0),
  .dnsServer = IPAddress (192, 168, 0, 1)
};

#define HOSTNAME "HelloESP"

// set up the transport mechanism
ESP8266WifiTransport wifiTransport(myNetworks, &myAddresses, HOSTNAME);

Arducom arducom(&wifiTransport, &Serial);

// RAM variables to expose via Arducom
uint8_t testByte;
int16_t testInt16;
int32_t testInt32;
int64_t testInt64;
#define TEST_BLOCK_SIZE  10
uint8_t testBlock[TEST_BLOCK_SIZE];

void setup() {

  EEPROM.begin(512);

  Serial.begin(115200);

  // give serial console time to connect
  #if ARDUCOM_DEBUG_SUPPORT == 1
    delay(10000);
  #endif
  
  Serial.println();

  wifiTransport.setHostname(HOSTNAME);

  arducom.addCommand(new ArducomVersionCommand(HOSTNAME));

  // EEPROM access commands
  arducom.addCommand(new ArducomReadEEPROMByte(1));
  arducom.addCommand(new ArducomWriteEEPROMByte(2));
  arducom.addCommand(new ArducomReadEEPROMInt16(3));
  arducom.addCommand(new ArducomWriteEEPROMInt16(4));
  arducom.addCommand(new ArducomReadEEPROMInt32(5));
  arducom.addCommand(new ArducomWriteEEPROMInt32(6));
  arducom.addCommand(new ArducomReadEEPROMInt64(7));
  arducom.addCommand(new ArducomWriteEEPROMInt64(8));
  arducom.addCommand(new ArducomReadEEPROMBlock(9));
  arducom.addCommand(new ArducomWriteEEPROMBlock(10));
  
  // expose RAM test variables
  arducom.addCommand(new ArducomReadByte(11, &testByte));
  arducom.addCommand(new ArducomWriteByte(12, &testByte));
  arducom.addCommand(new ArducomReadInt16(13, &testInt16));
  arducom.addCommand(new ArducomWriteInt16(14, &testInt16));
  arducom.addCommand(new ArducomReadInt32(15, &testInt32));
  arducom.addCommand(new ArducomWriteInt32(16, &testInt32));
  arducom.addCommand(new ArducomReadInt64(17, &testInt64));
  arducom.addCommand(new ArducomWriteInt64(18, &testInt64));
  arducom.addCommand(new ArducomReadBlock(19, (uint8_t*)&testBlock, TEST_BLOCK_SIZE));
  arducom.addCommand(new ArducomWriteBlock(20, (uint8_t*)&testBlock, TEST_BLOCK_SIZE));

  // expose the analog pin
  arducom.addCommand(new ArducomGetAnalogPin(A0));

  arducom.addCommand(new ArducomTimedToggle(51, 2));
}

void loop() {
  // handle Arducom commands
  int code = arducom.doWork();
  if (code == ARDUCOM_COMMAND_HANDLED) {
    Serial.println("Arducom command handled");
  } else
  if (code != ARDUCOM_OK) {
    Serial.print("Arducom error: ");
    Serial.println(code);
  }
}
