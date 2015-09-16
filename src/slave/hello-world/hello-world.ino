// Arducom hello world example
// by Leo Meyer <leomeyer@gmx.de>

// Demonstrates use of the Arducom library.
// Supports serial or I2C transport methods.
// Supports the Arducom status inquiry command.
// Implements basic EEPROM access commands.
// Implements basic RAM access commands (to expose variables).
// If an SD card is present, implements basic FTP support
// using the arducom-ftp master.
// If a DS1307 RTC is connected, supports getting and
// setting of the time.

// This example code is in the public domain.

#include <SPI.h>

// use SdFat:
// https://github.com/greiman/SdFat
#include <SdFat.h>
#include <SdFatUtil.h> 

#include <Arducom.h>
#include <ArducomI2C.h>
#include <ArducomStream.h>
#include <ArducomFTP.h>

/*******************************************************
* Configuration
*******************************************************/

// Recommended hardware: Arduino Uno or similar with a data logging shield, for example:
// https://learn.adafruit.com/adafruit-data-logger-shield 

// Feel free to play with different settings (see comments for the defines).


// LED pin; define this if you want to use the LED as a status indicator.
// Note that using the LED will greatly slow down operations like FTP which use
// lots of commands. Also, if you are using an SD card you won't probably see the
// blinking of the status LED due to its inference with the SPI ports.
// For these reasons it's recommended to use the LED for debugging purposes only.
// #define LED					13

// Define this macro if you are using an SD card.
// The chipselect pin depends on the type of SD card shield.
// Requires the SdFat library:
// https://github.com/greiman/SdFat<a
#define SDCARD_CHIPSELECT	10	

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
#define USE_DS1307			1

// Define the Arducom transport method. You can use either serial or I2C
// communication but not both.
#define SERIAL_STREAM		Serial
#define SERIAL_BAUDRATE		57600

// If you want to use I2C communications, define a slave address.
// #define I2C_SLAVE_ADDRESS	5

// Specifies a Print object to use for the debug output.
// Undefine this if you don't want to use debugging.
// You cannot use the same Print object for Arducom serial communication
// (for example, Serial). Instead, use a SoftwareSerial port or another
// HardwareSerial on Arduinos with more than one UART.
// Note: This define is for the hello-world test sketch. To debug Arducom,
// use the define USE_ARDUCOM_DEBUG below. Arducom will also use this output.
// #define DEBUG_OUTPUT		Serial

// Macro for debug output
#ifdef DEBUG_OUTPUT
#define DEBUG(x) if (DEBUG_OUTPUT) (DEBUG_OUTPUT).x
#else
#define DEBUG(x) /* x */
#endif

// If this is defined Arducom will output debug messages on DEBUG_OUTPUT.
// This will greatly slow down communication, so don't use
// this during normal operation.
// #define USE_ARDUCOM_DEBUG	1

#if defined SERIAL_STREAM && defined I2C_SLAVE_ADDRESS
#error You cannot use serial and I2C communication at the same time.
#endif

/*******************************************************
* RTC access command implementation (for getting and
* setting of RTC time)
*******************************************************/

#if USE_DS1307
// use RTClib from Adafruit
// https://github.com/adafruit/RTClib
#include <WSWire.h>
#include <RTClib.h>

RTC_DS1307 RTC;  // define the Real Time Clock object
#endif  // USE_DS1307

// RTC time is externally represented as a UNIX timestamp
// (32 bit integer). These two command classes implement
// getting and setting of the RTC clock time.

class ArducomGetTime: public ArducomCommand {
public:
	ArducomGetTime(uint8_t commandCode) : ArducomCommand(commandCode, 0) {}		// this command expects zero parameters
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
		// read RTC time
		DateTime now = RTC.now();
		long unixTS = now.unixtime();
		*((int32_t*)destBuffer) = unixTS;
		*dataSize = 4;
		return ARDUCOM_OK;
	}
};

class ArducomSetTime: public ArducomCommand {
public:
	ArducomSetTime(uint8_t commandCode) : ArducomCommand(commandCode, 4) {}		// this command expects four bytes as parameters
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
		// get parameter
		long unixTS = *((int32_t*)dataBuffer);
		// construct and set RTC time
		DateTime dt(unixTS);
		RTC.adjust(dt);
		*dataSize = 0;		// returns nothing
		return ARDUCOM_OK;
	}
};

/*******************************************************
* Variables
*******************************************************/

#ifdef SERIAL_STREAM
ArducomTransportStream arducomTransport(&SERIAL_STREAM);
#elif defined I2C_SLAVE_ADDRESS
ArducomTransportI2C arducomTransport(I2C_SLAVE_ADDRESS);
#else
#error You have to define a transport method (SERIAL_STREAM or I2C_SLAVE_ADDRESS).
#endif

Arducom arducom(&arducomTransport
#if defined DEBUG_OUTPUT && defined USE_ARDUCOM_DEBUG
, &DEBUG_OUTPUT
#endif
);
ArducomFTP arducomFTP;

#ifdef SDCARD_CHIPSELECT
SdFat sdFat;
SdFile logFile;
long lastWriteMs;
#endif

// RAM variables to expose via Arducom
uint8_t testByte;
int16_t testInt16;
int32_t testInt32;
int64_t testInt64;
#define TEST_BLOCK_SIZE	10
uint8_t testBlock[TEST_BLOCK_SIZE];

/*******************************************************
* Routines
*******************************************************/

#if defined SDCARD_CHIPSELECT && defined USE_DS1307
// call back for file timestamps
void dateTime(uint16_t* date, uint16_t* time) {
  DateTime now = RTC.now();

  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(now.year(), now.month(), now.day());

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}
#endif

/*******************************************************
* Setup
*******************************************************/

void setup()
{
#ifdef LED
	pinMode(LED, OUTPUT); 
#endif

#ifdef SERIAL_STREAM
	SERIAL_STREAM.begin(SERIAL_BAUDRATE);
#endif
	
#ifdef DEBUG_OUTPUT
	DEBUG_OUTPUT.begin(9600);
	while (!DEBUG_OUTPUT) {}  // Wait for Leonardo.
	
#endif
	DEBUG(print(F("HelloWorld starting...")));

	// reserved version command (it's recommended to leave this in
	// except if you really have to save flash/RAM)
	arducom.addCommand(new ArducomVersionCommand("HelloWorld"));

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
	arducom.addCommand(new ArducomReadBlock(19, (uint8_t*)&testBlock));
	arducom.addCommand(new ArducomWriteBlock(20, (uint8_t*)&testBlock, TEST_BLOCK_SIZE));
	
	#if USE_DS1307
	// connect to RTC
	Wire.begin();
	if (!RTC.isrunning()) {
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
	// initialize SD system
	if (sdFat.begin(SDCARD_CHIPSELECT, SPI_HALF_SPEED)) {
		// initialize FTP system (adds FTP commands)
		arducomFTP.init(&arducom, &sdFat);
		DEBUG(println(F("SD card setup ok.")));
	} else {
		DEBUG(println(F("SD card setup failure!")));
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


