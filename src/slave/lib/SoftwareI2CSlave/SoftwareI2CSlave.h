// Software I2C slave library for Arduino
// Copyright (c) 2015 Leo Meyer, leo@leomeyer.de. All rights reserved.

// *** License ***
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// *** Documentation ***
//
// This implementation of an I2C slave uses pin change interrupts to monitor
// the I2C bus lines. It does not use the internal I2C (or "Wire") hardware.
//
// This library is for situations where an Arduino is required to operate
// as an I2C master and slave simultaneously and there is only one hardware
// I2C port which is already designed to be the master (for example, to connect
// peripherals like real time clocks), and the other master does not support
// multi-master mode (e. g. a Raspberry Pi). Connecting both masters to the
// same I2C bus can cause data corruption on the peripherals due to a failure
// to properly lose bus arbitration by the other master. In these cases it is
// advisable to separate the two I2C buses. Generally it is preferable to use
// the Arduino hardware I2C slave and connect to the peripherals using a software
// I2C master implementation, but this sometimes can't be done with standard shields
// without hardware alterations as the I2C lines are usually wired to the hardware
// I2C pins. This library allows the Arduino to expose itself as an I2C slave on
// two pins other than the standard I2C hardware pins.
// 
// The following restrictions apply:
// - This implementation may have trouble with repeated start conditions. In
//   most cases this won't be a problem because those conditions are mostly
//   used in multi-master setups and this library is specifically for single
//   master setups.
// - The SDA and SCL pin must be on the same ATMEGA port.
// - Internal pullup resistors are not supported.
// - The usage of pin change interrupts may render this library incompatible
//   with other libraries that also use those interrupts (e. g. SoftwareSerial).
// - Bandwith limitation: 50 kHz seems to work fine (TODO: test greater speeds)
// - Single I2C address supported only. However, multiple slave addresses could be
//   implemented without too much trouble if required.
// - Single buffer for sending and receiving data. Data that is to be sent may
//   be overwritten if the master sends data instead of requesting data. Also,
//   there is no way of knowing whether data has actually been sent.
// - Delays by interrupt servicing routines when sending large amounts of data:
//   At a bus speed of 50 kHz each transferred byte takes about 9 / 50000 = 0.18 ms
//   time. This means that sending five bytes of data will delay other program parts
//   or interrupts by more than a millisecond. As pin change interrupts have higher
//   priority than for example timer interrupts, this will delay timers causing
//   values returned by e. g. millis() to tend to become slightly off over time.
//   In situations where accurate timing is required it is better to send only
//   small amounts of data at a time.
//   Reading data is done faster, and does not cause such problems.
// - Known bug: Writing data from a Raspberry Pi for the first time after startup
//   fails due to unknown reasons. Subsequent writes work ok.
//
// The plus side:
// - This implementation does not enable pullup resistors by default.
//   This means that an Arduino operating on 5 volts can be connected to a 3.3 V
//   master like a Raspberry Pi without danger.
// - Wait loops use a timeout counter. In theory, this should prevent the device
//   to hang in I2C service code in case of bus problems (the Arduino hardware
//   I2C library does not prevent this).
// - Low flash and RAM footprint.

// How to use:
// Include this header file in your sketch. This code is provided as a header file
// rather than a separate library because it requires several defines for
// configuration, thus allowing the interrupt service routine to run faster
// than it would be possible if configuration variables were used.
// Before including, define the configuration settings starting with I2C_SLAVE_
// in your sketch (see the example below).
// You have to define a callback function that is called by the interrupt when data
// has been received (on reception of the stop condition, to be precise).
// This function takes the following form:
// void I2COnReceive(uint8_t length) { ... }
// Keep this function short as it is called in an interrupt context. It is best to
// save the length to a (volatile) variable and process the received data in the main loop.
// To setup the I2C slave, call i2c_slave_init passing the callback function:
// i2c_slave_init(&I2COnReceive);
// If necessary, initialize the SDA and SCL pins to input mode. Do not use internal pullups.
// You have to enable interrupts using sei(); at the end of your setup routine.
// In the main loop, the received data can be accessed via the array i2c_slave_buffer.
// Note that the content of this array may change during processing if the master
// sends new data too fast.
// To send data back to the master, use the following function:
// i2c_slave_send(uint8_t *data, uint8_t length)
// This function copies the specified data to the i2c_slave_buffer array and uses it
// as send buffer when the master requests data from the slave.
// For a complete example see the end of this file, or SoftwareI2CSlave.ino.

/* Example configuration settings (copy these to your sketch and adjust them before
   including this file):

#include "pins_arduino.h"

// I2C slave address (range: 1 to 119)
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
   
*/

#include <util/delay.h>

// callback function that is called when the slave has received I2C data
// this routine must be short as it is run in an interrupt context
typedef void (*I2CSlaveOnReceive)(uint8_t length);

// I2C implementation details
#define I2CSTATE_INIT	0
#define I2CSTATE_RECV	0x80
#define I2CSTATE_SEND	0x40

#define I2C_SLAVE_PIN_MASK	(bit(I2C_SLAVE_SCL_BIT) | bit (I2C_SLAVE_SDA_BIT))

static volatile uint8_t i2c_slave_pins = 0;
static volatile uint8_t i2c_slave_state = I2CSTATE_INIT;
static volatile uint8_t i2c_slave_data = 0;
static volatile uint8_t i2c_slave_index = 0;
static volatile uint8_t i2c_slave_length = 0;
static uint8_t i2c_slave_buffer[I2C_SLAVE_BUFSIZE];
static I2CSlaveOnReceive i2c_onReceive;

// setup the I2C slave with the specified callback function
// the callback function must be short as it is run in an interrupt context
void i2c_slave_init(I2CSlaveOnReceive onReceive) {
	i2c_onReceive = onReceive;
	// enable the pin change interrupt
	PCICR |= bit(I2C_SLAVE_INTFLAG);
	I2C_SLAVE_PINMASKREG = I2C_SLAVE_PIN_MASK;
}

// store data to send in internal buffer
// data will be sent when a read is requested by the master
// if the master writes instead, the data is lost
void i2c_slave_send(uint8_t *data, uint8_t length) {
	uint8_t i = 0;
	while ((i < length) && (i < I2C_SLAVE_BUFSIZE)) {
		i2c_slave_buffer[i] = data[i];
		i++;
	}
	i2c_slave_index = 0;
	i2c_slave_length = i;
	i2c_slave_state = I2CSTATE_SEND;
}

// pin change interrupt routine for SCL and SDA
SIGNAL(I2C_SLAVE_INTVECTOR) {

#define START_COND	(bit(I2C_SLAVE_SCL_BIT))
#define STOP_COND	(bit(I2C_SLAVE_SCL_BIT) | bit (I2C_SLAVE_SDA_BIT))

// the waitcounter logic avoids the interrupt routine hanging in an infinite loop
// the value of MAX_WAIT may have to be increased if the bus speed is very slow (maximum: 32767)
#define MAX_WAIT	10000
int16_t waitcounter;
#define WAIT_SCL_LOW()  waitcounter = MAX_WAIT; while (I2C_SLAVE_READ_PINS & bit(I2C_SLAVE_SCL_BIT)) { waitcounter--; if (waitcounter <= 0) goto timeout; }
#define WAIT_SCL_HIGH() waitcounter = MAX_WAIT; while (!(I2C_SLAVE_READ_PINS & bit(I2C_SLAVE_SCL_BIT))) { waitcounter--; if (waitcounter <= 0) goto timeout; }

// send acknowledge by pulling SDA low for one clock cycle
#define SEND_ACK()	I2C_SLAVE_DDR_PINS |= bit(I2C_SLAVE_SDA_BIT); WAIT_SCL_HIGH(); WAIT_SCL_LOW(); I2C_SLAVE_DDR_PINS &= ~bit(I2C_SLAVE_SDA_BIT)

	// get current state of I2C pins
	uint8_t pins = I2C_SLAVE_READ_PINS;
	uint8_t noclear = 0;
	
	// stop condition (rising SDA, SCL high)?
	if (((pins & I2C_SLAVE_PIN_MASK) == STOP_COND)
		// SDA must have been previously low
		&& !(i2c_slave_pins & bit(I2C_SLAVE_SDA_BIT))) {
		// stop condition after receive?
		if (i2c_slave_state & I2CSTATE_RECV) {
			// trigger callback
			i2c_onReceive(i2c_slave_index);
		}
		// reset I2C state
		i2c_slave_state = I2CSTATE_INIT;
	} else
	// start condition (falling SDA, SCL high)?
	if (((pins & I2C_SLAVE_PIN_MASK) == START_COND)
		// SDA must have been previously high (bit changed)
		&& ((pins ^ i2c_slave_pins) & bit(I2C_SLAVE_SDA_BIT))) {
		// read address
		uint8_t adr = 0;
		for (uint8_t i = 0; i < 7; i++) {
			WAIT_SCL_LOW();
			WAIT_SCL_HIGH();
			adr <<= 1;
			// read address bit
			adr |= (I2C_SLAVE_READ_PINS >> I2C_SLAVE_SDA_BIT) & 1;
		}
		WAIT_SCL_LOW();
		WAIT_SCL_HIGH();
		// read r/w bit
		uint8_t r_w = (I2C_SLAVE_READ_PINS >> I2C_SLAVE_SDA_BIT) & 1;
		WAIT_SCL_LOW();
		// has this slave been addressed?
		if (adr == I2C_SLAVE_ADDRESS) {
			SEND_ACK();
			// master read?
			if (r_w) {
				if (i2c_slave_state == I2CSTATE_SEND) {
					// once a read has been initiated the state is reset
					// what is not read during this read is discarded
					i2c_slave_state = I2CSTATE_INIT;
					while (i2c_slave_index < i2c_slave_length) {
						uint8_t data = i2c_slave_buffer[i2c_slave_index];
						// send data (MSB first)
						for (int i = 0; i < 8; i++) {
							if (!(data & 0x80))
								// pull SDA low
								I2C_SLAVE_DDR_PINS |= bit(I2C_SLAVE_SDA_BIT);
							WAIT_SCL_HIGH();
							WAIT_SCL_LOW();
							if (!(data & 0x80))
								// reset SDA to input
								I2C_SLAVE_DDR_PINS &= ~bit(I2C_SLAVE_SDA_BIT);
							data <<= 1;
						}
						// expect ACK or NACK
						WAIT_SCL_HIGH();
						// NACK?
						if ((I2C_SLAVE_READ_PINS >> I2C_SLAVE_SDA_BIT) & 1)
							break;
						WAIT_SCL_LOW();
						i2c_slave_index++;
					}
				}
			} else {
				// master writes
				// receive data
				i2c_slave_state = I2CSTATE_RECV;
				i2c_slave_data = 0;
				i2c_slave_index = 0;
			}
		}
	} else
	// rising edge of clock?
	if ((pins & bit(I2C_SLAVE_SCL_BIT)) && !(i2c_slave_pins & bit(I2C_SLAVE_SCL_BIT))) {
		// receiving data?
		if (i2c_slave_state & I2CSTATE_RECV) {
			// read next data bit
			i2c_slave_data |= (pins >> I2C_SLAVE_SDA_BIT) & 1;
			// increase counter
			i2c_slave_state++;
			// eight bits received?
			if (i2c_slave_state & 0x08) {
				if (i2c_slave_index < I2C_SLAVE_BUFSIZE) {
					i2c_slave_buffer[i2c_slave_index] = i2c_slave_data;
					i2c_slave_index++;
					// reset counter
					i2c_slave_state &= ~0x07;
					WAIT_SCL_LOW();
					SEND_ACK();
					// stay in receive mode
					i2c_slave_state = I2CSTATE_RECV;
					i2c_slave_data = 0;
				} else {
					// receive buffer overflow, send NACK
					WAIT_SCL_LOW();
					WAIT_SCL_HIGH();
					i2c_slave_state = I2CSTATE_INIT;
				}
			} else
				i2c_slave_data <<= 1;		
		}
	}

timeout:
	// remember last pin state
	i2c_slave_pins = I2C_SLAVE_READ_PINS;
	if (!noclear)
		PCIFR |= bit(I2C_SLAVE_CLEARFLAG);   // clear any outstanding interrupts
	// switch SDA pin to input in case of timeouts
	I2C_SLAVE_DDR_PINS &= ~bit(I2C_SLAVE_SDA_BIT);
}

/* 

// Example for the use of this library in a sketch.
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
 
*/