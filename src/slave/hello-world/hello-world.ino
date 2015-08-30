// Arducom hello world example
// by Leo Meyer <leomeyer@gmx.de>

// Demonstrates use of the Arducom library.
// Supports serial or I2C transport methods.
// Supports the Arducom status inquiry command.
// Implements basic EEPROM access commands.
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

// LED pin; undefine this if you don't want to use the LED as status indicator
// #define LED					13

// Define this macro if you are using an SD card.
// The chipselect pin depends on the type of SD card shield.
#define SDCARD_CHIPSELECT	10	

// Specifies whether the DS1307 Real Time Clock should be used.
// If you don't have a DS1307 connected (via I2C), comment this define.
#define USE_DS1307			1

// Define the Arducom transport method. You can use either serial or I2C
// communication but not both.
// #define SERIAL_STREAM		Serial
#define SERIAL_BAUDRATE		115200

#define I2C_SLAVE_ADDRESS	5

// Specifies a Print object to use for the debug output.
// Undefine this if you don't want to use debugging.
// You cannot use the same Print object for Arducom serial communication
// (for example, Serial).
// #define DEBUG_OUTPUT		Serial

// Macro for debug output
#ifdef DEBUG_OUTPUT
#define DEBUG(x) if (DEBUG_OUTPUT) (DEBUG_OUTPUT).x
#else
#define DEBUG(x) /* x */
#endif

// If this is defined Arducom will output debug messages.
// This will greatly slow down communications, so don't use
// this during normal operation.
// #define USE_ARDUCOM_DEBUG	1

#if defined SERIAL_STREAM && defined I2C_SLAVE_ADDRESS
#error You cannot use serial and I2C communications at the same time.
#endif

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

#if USE_DS1307
// use RTClib from Adafruit
// https://github.com/adafruit/RTClib
#include <Wire.h>
#include <RTClib.h>

RTC_DS1307 RTC;  // define the Real Time Clock object
//------------------------------------------------------------------------------
// call back for file timestamps
void dateTime(uint16_t* date, uint16_t* time) {
  DateTime now = RTC.now();

  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(now.year(), now.month(), now.day());

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}
#endif  // USE_DS1307

// RAM variables to expose via Arducom
uint8_t testByte;
int16_t testInt16;
int32_t testInt32;
int64_t testInt64;
#define TEST_BLOCK_SIZE	10
uint8_t testBlock[TEST_BLOCK_SIZE];

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
	
	DEBUG(print(F("FreeRam: ")));
	DEBUG(println(FreeRam()));	
#endif

	// reserved version command (it's recommended to leave this in
	// except if you really have to save flash/RAM)
	arducom.addCommand(new ArducomVersionCommand("HelloWorld"));

	// EEPROM access command
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
	if (!RTC.begin()) {
		DEBUG(println(F("RTC not functional")));
	} else {
		// set date time callback function (sets file creation date)
		SdFile::dateTimeCallback(dateTime);
	}
#endif  // USE_DS1307

#ifdef SDCARD_CHIPSELECT
	// initialize SD system
	if (sdFat.begin(SDCARD_CHIPSELECT, SPI_HALF_SPEED)) {
		// initialize FTP system (adds FTP commands)
		arducomFTP.init(&arducom, &sdFat);
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
#endif LED
}

void loop()
{
	int code = arducom.doWork();
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
#endif LED
	}
	
	return;

#ifdef SDCARD_CHIPSELECT
	// write to a file every few seconds
	if (millis() - lastWriteMs > 5000) {
		if (logFile.open("/testfil2.txt", O_RDWR | O_CREAT | O_AT_END)) {
			logFile.println(millis());
			logFile.close();
			lastWriteMs = millis();
		}
	}
#endif
}


