// Minimal Arducom slave example
// by Leo Meyer <leo@leomeyer.de>

// Receives Arducom commands via serial stream.
// Understands the Arducom version command (code 0).

// This code is in the public domain.

#include <Arducom.h>

/*******************************************************
* Variables
*******************************************************/

// Arducom system variables
ArducomTransportStream arducomTransport(&Serial);
Arducom arducom(&arducomTransport);

/*******************************************************
* Setup
*******************************************************/

void setup()
{	
	// initialize hardware
	Serial.begin(ARDUCOM_DEFAULT_BAUDRATE);

	// setup Arducom system
	arducom.addCommand(new ArducomVersionCommand("MinimalExample"));
}

/*******************************************************
* Main loop
*******************************************************/

void loop()
{
	// handle Arducom commands
	arducom.doWork();
}
