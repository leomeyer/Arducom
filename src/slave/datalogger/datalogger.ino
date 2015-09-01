// Arducom based datalogger
// by Leo Meyer <leomeyer@gmx.de>

// Data logger using an SD card and a Real Time Clock DS1307
// Recommended hardware: Arduino Uno or similar with a data logging shield, for example:
// https://learn.adafruit.com/adafruit-data-logger-shield 

// The data logger is intended to be connected to a host (e. g. Raspberry Pi) via USB.
// If the host is always on there is no need for a separate power supply.
// If the host is powered on intermittently there must be a dedicated supply for the Arduino.
// If the data logger does not use a serial input (D0) port, data (real time and stored)
// can be read via the serial port. If the serial input is used for sensor data
// communication can take place over I2C.

// This data logger supports:
// - up to four S0 lines (digital pins 4 - 7), counting 64 bit values stored in EEPROM
// - one D0 input (requires the RX pin and one digital output for the optical transistor supply voltage)
// - up to two DHT22 temperature/humidity sensors
// - up to four analog values (analog pins A0 - A3).

// The S0 lines are queried once a ms using software debouncing.
// The timer interrupt is configured for the ATMEGA328 with a clock speed of 16 MHz. Other CPUs might require adjustments.

// The D0 input is a serial input used by electronic power meters. Data is transmitted via an IR LED.
// As the Arduino should still be programmable via USB cable, D0 serial data must be disabled during
// programming. It is easiest to switch on the receiving IR transistor's supply voltage on program start.
// Also, the UART must be configured to support the serial protocol (e. g. for an Easymeter: 9600 baud, 7E1).
// The following circuit can be used to detect D0 data:


// Timestamped sensor readings are stored on the SD card in a configurable interval. It is recommended to
// choose this interval wisely. Internal sensor data that is exposed to the host is also updated with
// respect to this interval to avoid the need for keeping a time reference on the host.
// Sensor data is provided as absolute values as well as deltas within the last completed interval.

// SD card log files should be rolled over when they become large. Generally it is advisable to keep these files
// to around 100 kB. Logging 100 bytes every minute causes a daily data volume of 140 kB;
// this should be considered the upper limit. At a baud rate of 57600 such a file will take about 30 seconds
// to download using an Arducom FTP transfer.

// The intended use case is to connect some kind of automation software (e. g. OPDID) to the data logger
// for querying (and operating on) current values as well as having an automated job download daily history
// using arducom-ftp.

// The S0 EEPROM values can be adjusted ("primed") via arducom to set the current meter readings (make sure to 
// take the impulse factor into consideration).

// The D0 port needs no persistent storage as the meter will transmit total values.



// This example code is in the public domain.

#include <SPI.h>

#include <dht.h>

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

// The chipselect pin depends on the type of SD card shield.
#define SDCARD_CHIPSELECT	10	

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


#define LOG_FILENAME		"/datalog.txt"

#define LOG_INTERVAL_MS		60000

/*******************************************************
* RTC access command implementation (for getting and
* setting of RTC time)
*******************************************************/

// use RTClib from Adafruit
// https://github.com/adafruit/RTClib
#include <Wire.h>
#include <RTClib.h>

RTC_DS1307 RTC;  // define the Real Time Clock object

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
/* Timer2 reload value, globally available */
unsigned int tcnt2;

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

SdFat sdFat;
SdFile logFile;
long lastWriteMs;

// RAM variables to expose via Arducom
volatile int16_t interruptCalls;

/*******************************************************
* Routines
*******************************************************/

// call back for file timestamps
void dateTime(uint16_t* date, uint16_t* time) {
  DateTime now = RTC.now();

  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(now.year(), now.month(), now.day());

  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}

/*
 * Install the Interrupt Service Routine (ISR) for Timer2 overflow.
 * This is normally done by writing the address of the ISR in the
 * interrupt vector table but conveniently done by using ISR()  */
ISR(TIMER2_OVF_vect) {
	/* Reload the timer */
	TCNT2 = tcnt2;
	
	interruptCalls++;
}

/*******************************************************
* Setup
*******************************************************/

void setup()
{
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
	arducom.addCommand(new ArducomVersionCommand("DataLogger"));
/*
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
*/	
	// expose variables
	arducom.addCommand(new ArducomReadInt16(11, (int16_t*)&interruptCalls));
	
	// connect to RTC
	Wire.begin();
	if (!RTC.begin()) {
		DEBUG(println(F("RTC not functional")));
	} else {
		// register example RTC commands
		// Assuming I2C, on Linux you can display the current date using the following command:
		//  date -d @`./arducom -t i2c -d /dev/i2c-1 -a 5 -c 21 -o Int32 -l 10`
		// To set the current date and time, use:
		//  date +"%s" | ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 22 -i Int32 -r -l 10
		arducom.addCommand(new ArducomGetTime(21));
		arducom.addCommand(new ArducomSetTime(22));
		
		// set date time callback function (sets file creation date)
		SdFile::dateTimeCallback(dateTime);
	}

	// initialize SD system
	if (sdFat.begin(SDCARD_CHIPSELECT, SPI_HALF_SPEED)) {
		// initialize FTP system (adds FTP commands)
		arducomFTP.init(&arducom, &sdFat);
	}

	// configure interrupt (once per ms)
	/* First disable the timer overflow interrupt while we're configuring */
	TIMSK2 &= ~(1<<TOIE2);

	/* Configure timer2 in normal mode (pure counting, no PWM etc.) */
	TCCR2A &= ~((1<<WGM21) | (1<<WGM20));
	TCCR2B &= ~(1<<WGM22);

	/* Select clock source: internal I/O clock */
	ASSR &= ~(1<<AS2);

	/* Disable Compare Match A interrupt enable (only want overflow) */
	TIMSK2 &= ~(1<<OCIE2A);

	/* Now configure the prescaler to CPU clock divided by 128 */
	TCCR2B |= (1<<CS22)  | (1<<CS20); // Set bits
	TCCR2B &= ~(1<<CS21);             // Clear bit

	/* We need to calculate a proper value to load the timer counter.
	* The following loads the value 131 into the Timer 2 counter register
	* The math behind this is:
	* (CPU frequency) / (prescaler value) = 125000 Hz = 8us.
	* (desired period) / 8us = 125.
	* MAX(uint8) + 1 - 125 = 131;
	*/
	/* Save value globally for later reload in ISR */
	tcnt2 = 131; 

	/* Finally load end enable the timer */
	TCNT2 = tcnt2;
	TIMSK2 |= (1<<TOIE2);
}

/*******************************************************
* Main loop
*******************************************************/

void loop()
{
	int code = arducom.doWork();
	if (code != ARDUCOM_OK) {
		DEBUG(print(F("Arducom error: ")));
		DEBUG(println(code));
	}
	
	// write to a file every few seconds
	if (millis() - lastWriteMs > LOG_INTERVAL_MS) {
		if (logFile.open(LOG_FILENAME, O_RDWR | O_CREAT | O_AT_END)) {
			logFile.print(millis());
			logFile.print(" ");
			logFile.println(interruptCalls);
			logFile.close();
			lastWriteMs = millis();
			interruptCalls = 0;
		}
	}
}


