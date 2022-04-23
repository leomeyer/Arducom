// Arducom hello world example
// by Leo Meyer <leo@leomeyer.de>

// Demonstrates the use of the Arducom library.
// Supports serial (default) or I2C transport methods, either
// being implemented in hardware or software, and Ethernet.
// Supports the Arducom status inquiry command.
// Implements basic EEPROM access commands.
// Implements basic RAM access commands (to expose variables).
// Exposes the upper six pins of port D for reading and writing.
// Exposes the analog inputs for reading.
// If an SD card is present, implements basic FTP support
// for the arducom-ftp master. Uncomment the define SDCARD_CHIPSELECT below.
// If a DS1307 RTC is connected (via I2C), supports getting and
// setting of the time. Uncomment the define USE_DS1307 below.
//
// Pay attention to flash ROM usage as some libraries are rather heavy.

// This example code is in the public domain.

#include <SPI.h>
// #include <SoftwareSerial.h>

// Required library: 
// - SdFat (by Bill Greiman)
// Install via library manager
#include <SdFat.h>

#include <Arducom.h>
#include <ArducomI2C.h>
#include <ArducomEthernet.h>
#include <ArducomFTP.h>

/*******************************************************
* Configuration
*******************************************************/

// Recommended hardware: Arduino Uno or similar with a data logging shield, for example:
// https://learn.adafruit.com/adafruit-data-logger-shield 

// Feel free to play with different settings (see comments for the defines).

// Define the Arducom transport method. You can use:
// 1. Hardware Serial: Define SERIAL_STREAM and SERIAL_BAUDRATE.
// 2. Software Serial: Initialize a SoftwareSerial instance and assign it to SERIAL_STREAM.
// 3. Hardware I2C: Define I2C_SLAVE_ADDRESS.
// 4. Software I2C: Define I2C_SLAVE_ADDRESS and SOFTWARE_I2C.
// 5. Ethernet: Define ETHERNET_PORT. An Ethernet shield is required.

// 1. Hardware Serial
// #define SERIAL_STREAM		  Serial
// #define SERIAL_BAUDRATE		ARDUCOM_DEFAULT_BAUDRATE

// 2. Software serial connection (for example with a Bluetooth module)
// #define SOFTSERIAL_RX_PIN	6
// #define SOFTSERIAL_TX_PIN	7
// SoftwareSerial softSerial(SOFTSERIAL_RX_PIN, SOFTSERIAL_TX_PIN);
// #define SERIAL_STREAM		softSerial
// #define SERIAL_BAUDRATE		9600

// 3. Hardware I2C communication: define a slave address
// #define I2C_SLAVE_ADDRESS	5

// 4. For Software I2C, additionally define SOFTWARE_I2C (configuration see below)
// #define SOFTWARE_I2C

// 5. Ethernet
#define ETHERNET_PORT			ARDUCOM_TCP_DEFAULT_PORT
#define ETHERNET_MAC			0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
#define ETHERNET_IP			  192, 168, 0, 201

// If using software I2C specify the configuration here
// (see ../lib/SoftwareI2CSlave/SoftwareI2CSlave.h).
#if defined I2C_SLAVE_ADDRESS && defined SOFTWARE_I2C

	// The buffer size in bytes for the send and receive buffer
	#define I2C_SLAVE_BUFSIZE		ARDUCOM_BUFFERSIZE

// The numbers of the Arduino pins to use (in this example, A0 and A1)
// Pins 0 - 7 are on PIND
// Pins 8 - 13 are on PINB
// Pins 14 - 19 are on PINC
#define I2C_SLAVE_SCL_PIN   14
#define I2C_SLAVE_SDA_PIN   15

// The pin read command (input port register)
// Subsequent definitions mainly depend on this setting.
#define I2C_SLAVE_READ_PINS   PINC

// The pin data direction register
// For PINB, use DDRB
// For PINC, use DDRC
// For PIND, use DDRD
#define I2C_SLAVE_DDR_PINS    DDRC

// The corresponding bits of the pins on the input and data direction registers
#define I2C_SLAVE_SCL_BIT   0
#define I2C_SLAVE_SDA_BIT   1

// The pin change interrupt vector corresponding to the input port.
// For PINB, use PCINT0_vect
// For PINC, use PCINT1_vect
// For PIND, use PCINT2_vect
#define I2C_SLAVE_INTVECTOR   PCINT1_vect

// The interrupt enable flag for the pin change interrupt
// For PINB, use PCIE0
// For PINC, use PCIE1
// For PIND, use PCIE2
#define I2C_SLAVE_INTFLAG   PCIE1

// The clear flag for the pin change interrupt
// For PINB, use PCIF0
// For PINC, use PCIF1
// For PIND, use PCIF2
#define I2C_SLAVE_CLEARFLAG   PCIF1

// The pin mask register for the pin change interrupt
// For PINB, use PCMSK0
// For PINC, use PCMSK1
// For PIND, use PCMSK2
#define I2C_SLAVE_PINMASKREG  PCMSK1

#include "SoftwareI2CSlave.h"

#endif	// SOFTWARE_I2C

// LED pin; define this if you want to use the LED as a status indicator.
// Note that using the LED will greatly slow down operations like FTP which use
// lots of commands. Also, if you are using an SD card you won't probably see the
// blinking of the status LED due to its inference with the SPI ports.
// For these reasons it's recommended to use the LED for debugging purposes only.
// #define LED				13

// Define this macro if you are using an SD card.
// The chip select pin depends on the type of SD card shield.
// Requires the SdFat library.
// The Keyes Data Logger Shield uses pin 10 for chip select.
// The W5100 Ethernet shield uses pin 4 for chip select.
#define SDCARD_CHIPSELECT		4

// If an SD card is present, periodically appends simulated log data to the file
// specified in this macro.
// It is important to specify the full path for this file. Otherwise, if you connect 
// using the FTP master and change the working directory, the file would be created
// in this directory, so you'd possibly end up with multiple files across the SD card.
// Do not use this if you leave the Arduino running for a longer time because of the
// wear imposed on your SD card.
// #define GENERATE_LOGFILE	"/TESTFILE.TXT"

// Specifies whether the DS1307 Real Time Clock should be used.
// If you don't have a DS1307 connected (via I2C), comment this define.
// #define USE_DS1307

// Specifies a Print object to use for the debug output.
// You cannot use the same Print object for Arducom serial communication
// (for example, Serial). Instead, use a SoftwareSerial port or another
// HardwareSerial on Arduinos with more than one UART.
// Note: This define is for the hello-world test sketch only. To debug Arducom,
// use the define USE_ARDUCOM_DEBUG below. Arducom debug will also use this output.
// Debug output may not work with all versions of the Arduino compiler.
// Using debug output may also cause instability when many commands are defined
// due to low space for local variables. Use less commands in this case.
#define DEBUG_OUTPUT		Serial
#define DEBUG_BAUDRATE		ARDUCOM_DEFAULT_BAUDRATE

// Macro for debug output
#ifdef DEBUG_OUTPUT
#define DEBUG(x) if (DEBUG_OUTPUT) (DEBUG_OUTPUT).x
#else
#define DEBUG(x) /* x */
#endif

// If USE_ARDUCOM_DEBUG is defined Arducom will output debug messages on DEBUG_OUTPUT.
// This will greatly slow down communication, so don't use this during normal operation.
// Requires Arducom to be compiled with debug support. To enable Arducom debug support
// set ARDUCOM_DEBUG_SUPPORT to 1 in Arducom.h.
#if ARDUCOM_DEBUG_SUPPORT == 1
 #define USE_ARDUCOM_DEBUG
#endif

// validate setup
#if defined ETHERNET_PORT && defined SERIAL_STREAM
#error You cannot use Ethernet and serial communication at the same time.
#endif

#if defined ETHERNET_PORT && defined I2C_SLAVE_ADDRESS
#error You cannot use Ethernet and I2C communication at the same time.
#endif

#if defined SERIAL_STREAM && defined I2C_SLAVE_ADDRESS
#error You cannot use serial and I2C communication at the same time.
#endif

/*******************************************************
* RTC access command implementation (for getting and
* setting of RTC time)
*******************************************************/

#ifdef USE_DS1307

// Required libraries:
// - DS1307RTC (by Michael Margolis)
// - Time (by Michael Margolis)
// Install via library manager.
#include <DS1307RTC.h>
//DS1307RTC RTC;  // defined by library

// RTC time is externally represented as a UNIX timestamp
// (32 bit integer). These two command classes implement
// getting and setting of the RTC clock time.

class ArducomGetTime: public ArducomCommand {
public:
	ArducomGetTime(uint8_t commandCode) : ArducomCommand(commandCode, 0) {}		// this command expects zero parameters
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
		// read RTC time
		long unixTS = RTC.get();
		*((int32_t*)destBuffer) = unixTS;
		*dataSize = 4;
		return ARDUCOM_OK;
	}
};

class ArducomSetTime: public ArducomCommand {
public:
	ArducomSetTime(uint8_t commandCode) : ArducomCommand(commandCode, 4) {}		// this command expects four bytes as parameters
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
		// get parameter
		long unixTS = *((int32_t*)dataBuffer);
		// construct and set RTC time
		RTC.set(unixTS);
		*dataSize = 0;		// returns nothing
		return ARDUCOM_OK;
	}
};

#endif		// USE_DS1307

/*******************************************************
* Variables
*******************************************************/

#ifdef SERIAL_STREAM
	// plain serial connection
	ArducomTransportStream arducomTransport(&SERIAL_STREAM);
#elif defined I2C_SLAVE_ADDRESS
	// I2C may be either software or hardware
	#ifdef SOFTWARE_I2C
	ArducomSoftwareI2C arducomTransport(&i2c_slave_init, &i2c_slave_send, &i2c_slave_buffer[0]);
	#else
	ArducomHardwareI2C arducomTransport(I2C_SLAVE_ADDRESS);
	#endif
#elif defined ETHERNET_PORT
	// Ethernet settings
	byte eth_mac[] = {ETHERNET_MAC};
	IPAddress eth_ip(ETHERNET_IP);
	ArducomTransportEthernet arducomTransport(ETHERNET_PORT);

	// command to initialize LAN
	// To use different network settings see the Ethernet library documentation:
	// https://www.arduino.cc/en/Reference/Ethernet
	#define INITIALIZE_ETHERNET() 		Ethernet.begin(eth_mac, eth_ip)
#else
#error You have to define a transport method (SERIAL_STREAM, I2C_SLAVE_ADDRESS or ETHERNET_PORT).
#endif

Arducom arducom(&arducomTransport
#if defined DEBUG_OUTPUT && defined USE_ARDUCOM_DEBUG
, &DEBUG_OUTPUT
#endif
);

#ifdef SDCARD_CHIPSELECT
// FTP works only if there is an SD card
ArducomFTP arducomFTP;

SdFat sdFat;
SdFile logFile;
long lastWriteMs;
#endif

// RAM variables to expose via Arducom
#define TEST_BLOCK_SIZE	10
uint8_t testBlock[TEST_BLOCK_SIZE];

/*******************************************************
* Routines
*******************************************************/

#if defined SDCARD_CHIPSELECT && defined USE_DS1307
// call back for file timestamps
void dateTime(uint16_t* date, uint16_t* time) {
  time_t now = RTC.get();
  
  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(year(now), month(now), day(now));

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(hour(now), minute(now), second(now));
}
#endif

/*******************************************************
* Setup
*******************************************************/

void setup()
{
  //Ethernet.init(10);
#ifdef LED
	pinMode(LED, OUTPUT); 
#endif

#ifdef SERIAL_STREAM
	SERIAL_STREAM.begin(SERIAL_BAUDRATE);
#endif
	
#ifdef DEBUG_OUTPUT
	DEBUG_OUTPUT.begin(DEBUG_BAUDRATE);
	while (!DEBUG_OUTPUT) {}  // Wait for Leonardo.
#endif

	DEBUG(println(F("HelloWorld starting...")));
	
#ifdef ETHERNET_PORT
		INITIALIZE_ETHERNET();
#endif

	// reserved version command (it's recommended to leave this in
	// except if you really have to save flash/RAM)
	arducom.addCommand(new ArducomVersionCommand("HelloWorld"));
/*
	// EEPROM access commands
	arducom.addCommand(new ArducomReadEEPROMBlock(9));
	arducom.addCommand(new ArducomWriteEEPROMBlock(10));
	
	// expose RAM test variables
	arducom.addCommand(new ArducomReadBlock(19, (uint8_t*)&testBlock, TEST_BLOCK_SIZE));
	arducom.addCommand(new ArducomWriteBlock(20, (uint8_t*)&testBlock, TEST_BLOCK_SIZE));
	
	// expose all of port D's pins through pin commands
	// except the two lower ones; these are used by RX and TX
	arducom.addCommand(new ArducomGetPortDirection(30, &DDRD, ~3));
	arducom.addCommand(new ArducomSetPortDirection(31, &DDRD, ~3));
	arducom.addCommand(new ArducomGetPortState(32, &PIND, ~3));
	arducom.addCommand(new ArducomSetPortState(33, &PORTD, &PIND, ~3));
	
	// expose the analog ports
	arducom.addCommand(new ArducomGetAnalogPin(35));
*/
	#ifdef USE_DS1307
  DEBUG(println(F("Setup RTC...")));
	// connect to RTC
	if (!RTC.isRunning()) {
		DEBUG(println(F("RTC setup failure!")));
	} else {
		// register example RTC commands
		// Assuming I2C, on Linux you can display the current date using the following command:
		//  date -d @`./arducom -t i2c -d /dev/i2c-1 -a 5 -c 21 -o Int32 -l 10`
		// To set the current date and time, use:
		//  date +"%s" | ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 22 -i Int32 -r -l 10
		arducom.addCommand(new ArducomGetTime(21));
		arducom.addCommand(new ArducomSetTime(22));
		
#ifdef SDCARD_CHIPSELECT
		// set date time callback function (sets file creation date)
		SdFile::dateTimeCallback(dateTime);
#endif
		DEBUG(println(F("RTC setup ok.")));
	}
#endif  // USE_DS1307

#ifdef SDCARD_CHIPSELECT
  DEBUG(println(F("Setup SD card...")));
	// initialize SD system
	if (!sdFat.begin(SDCARD_CHIPSELECT, SPI_HALF_SPEED)) {
		DEBUG(println(F("SD card setup failure!")));
	} else {
		// initialize FTP system (adds FTP commands)
		arducomFTP.init(&arducom, &sdFat);
		DEBUG(println(F("SD card setup ok.")));
	}		
#endif

#ifdef LED
	// signal ready on LED
	for (int i = 0; i < 5; i++) {
		digitalWrite(LED, HIGH);
		delay(200);
		digitalWrite(LED, LOW);
		delay(200);
	}
#endif
	DEBUG(println(F("HelloWorld setup done.")));
}

/*******************************************************
* Main loop
*******************************************************/

void loop()
{
	// handle Arducom commands
	int code = arducom.doWork();
	if (code == ARDUCOM_COMMAND_HANDLED) {
		DEBUG(println(F("Arducom command handled")));
	} else
	if (code != ARDUCOM_OK) {
		DEBUG(print(F("Arducom error: ")));
		DEBUG(println(code));
		
#ifdef LED
		// signal error on LED
		for (int i = 0; i < code; i++) {
			digitalWrite(LED, HIGH);
			delay(200);
			digitalWrite(LED, LOW);
			delay(200);
		}
#endif
	}
	
#if defined SDCARD_CHIPSELECT && defined GENERATE_LOGFILE
	// write to a file every few seconds
	if (millis() - lastWriteMs > 5000) {
		if (logFile.open(GENERATE_LOGFILE, O_RDWR | O_CREAT | O_AT_END)) {
			logFile.println(millis());
			logFile.close();
			lastWriteMs = millis();
		}
	}
#endif
}
