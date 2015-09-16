// Minimal Arducom slave example
// by Leo Meyer <leomeyer@gmx.de>

#include <Arducom.h>
#include <ArducomStream.h>

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
	// Initialize hardware components
	Serial.begin(9600);

	// Setup Arducom system
	arducom.addCommand(new ArducomVersionCommand("MinimalExample"));
}

/*******************************************************
* Main loop
*******************************************************/

void loop()
{
	// handle Arducom commands
	int code = arducom.doWork();
	if (code == ARDUCOM_COMMAND_HANDLED) {
	} else
	if (code != ARDUCOM_OK) {
	}
}
