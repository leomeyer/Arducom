// ESP8266Proxy
// by Leo Meyer <leo@leomeyer.de>

// This sketch can be used to provide an Arduino Arducom client with WLAN functionality.
// The ESP8266 is connected to the Arduino in this or a similar fashion:
// http://www.teomaragakis.com/hardware/electronics/how-to-connect-an-esp8266-to-an-arduino-uno/
// Software serial can be used on the Arduino so that hardware serial is still available for debugging.

// This example code is in the public domain.

#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Werror"

#include <ArducomESP8266.h>

// Machine-specific secrets. Can be put in a folder "Secrets" in your library folder.
// Delete this line if you want to put the secrets into this file.
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
  .staticIP = IPAddress(192, 168, 0, 247),
  .gateway = IPAddress (192, 168, 0, 1),
  .subnet = IPAddress (255, 255, 255, 0),
  .dnsServer = IPAddress (192, 168, 0, 1)
};

#define HOSTNAME "ESP8266Proxy"

// set up the transport mechanism
ESP8266WifiTransport wifiTransport(myNetworks, &myAddresses, HOSTNAME);

// set up the proxy transport that transfers data from the ESP8266 to the Serial stream
ArducomTransportProxy proxyTransport(&wifiTransport, &Serial);

// initialize Arducom with the proxy transport
Arducom arducom(&proxyTransport);

void setup() {
  Serial.begin(9600);
}

void loop() {
  // handle Arducom commands
  arducom.doWork();
}
