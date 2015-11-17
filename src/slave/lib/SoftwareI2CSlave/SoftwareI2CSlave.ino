// Software I2C slave example
// by Leo Meyer <leo@leomeyer.de>

// Demonstrates use of the SoftwareI2CSlave library.
// This file is in the public domain.

// Testing with a Raspberry Pi
// Precondition: I2C has been properly set up, please see for example:
// https://learn.adafruit.com/adafruits-raspberry-pi-lesson-4-gpio-setup/configuring-i2c
// Set the Raspberry Pi's I2C speed to 40 kHz (how to do this depends on your OS).
//
// Connect Raspberry GND to Arduino GND
// Connect Raspberry GPIO1 (SCL) to Arduino pin A0
// Connect Raspberry GPIO0 (SDA) to Arduino pin A1
// No external resistors are necessary.
//
// i2cdetect should show the Arduino slave:
/*
$ i2cdetect -y 1
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- 77
*/

// To test whether sent data is actually being returned, use arducom.
// Allow at least one millisecond delay for processing (-l 1).
/*
$ ./arducom -t i2c -d /dev/i2c-1 -a 119 -c 0 -v -p 018203840586078809A0 -l 1
Sending bytes: 008AA5018203840586078809A0 .............
Receive buffer: 008AA5018203840586078809A0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF ................................
Communication error: Didn't receive a valid reply
*/
// The "Communication error" is normal because arducom expects a valid response
// instead of simply returned bytes.

#include "pins_arduino.h"

// I2C slave address (range: 1 to 119/0x77)
#define I2C_SLAVE_ADDRESS		0x77

// The buffer size in bytes for the send and receive buffer
#define I2C_SLAVE_BUFSIZE		24

// The numbers of the Arduino pins to use (in this example, A0 and A1)
// Pins 0 - 7 are on PIND
// Pins 8 - 13 are on PINB
// Pins 14 - 19 are on PINC
#define I2C_SLAVE_SCL_PIN		14
#define I2C_SLAVE_SDA_PIN		15

// The pin read command (input port register)
// Subsequent definitions mainly depend on this setting.
#define I2C_SLAVE_READ_PINS		PINC

// The pin data direction register
// For PINB, use DDRB
// For PINC, use DDRC
// For PIND, use DDRD
#define I2C_SLAVE_DDR_PINS		DDRC

// The corresponding bits of the pins on the input and data direction registers
#define I2C_SLAVE_SCL_BIT		0
#define I2C_SLAVE_SDA_BIT		1

// The pin change interrupt vector corresponding to the input port.
// For PINB, use PCINT0_vect
// For PINC, use PCINT1_vect
// For PIND, use PCINT2_vect
#define I2C_SLAVE_INTVECTOR		PCINT1_vect

// The interrupt enable flag for the pin change interrupt
// For PINB, use PCIE0
// For PINC, use PCIE1
// For PIND, use PCIE2
#define I2C_SLAVE_INTFLAG		PCIE1

// The clear flag for the pin change interrupt
// For PINB, use PCIF0
// For PINC, use PCIF1
// For PIND, use PCIF2
#define I2C_SLAVE_CLEARFLAG		PCIF1

// The pin mask register for the pin change interrupt
// For PINB, use PCMSK0
// For PINC, use PCMSK1
// For PIND, use PCMSK2
#define I2C_SLAVE_PINMASKREG	PCMSK1

#include "SoftwareI2CSlave.h"

// This program receives data and immediately sends back the same data.

volatile int16_t recv_length;

// called when data has been received
void I2CReceive(uint8_t length) {
  recv_length = length;
}

void setup()
{
  Serial.begin(9600);

  delay(1000);

  // setup I2C pins
  pinMode(I2C_SLAVE_SCL_PIN, INPUT);
  pinMode(I2C_SLAVE_SDA_PIN, INPUT);

  i2c_slave_init(&I2CReceive);

  sei();
}

void loop() {
  if (recv_length > 0) {
  	Serial.print("Received ");
	Serial.print(recv_length);
	Serial.println(" byte(s)");
	i2c_slave_send(i2c_slave_buffer, recv_length);
	recv_length = 0;
  }
}
 
