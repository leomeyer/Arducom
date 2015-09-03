// Arducom based data logger
// by Leo Meyer <leomeyer@gmx.de>

// Example data logger using an SD card and a Real Time Clock DS1307
// Recommended hardware: Arduino Uno or similar with a data logging shield, for example:
// https://learn.adafruit.com/adafruit-data-logger-shield 
// Tested with an Arduino Uno clone and a "KEYES XD-204 Data Logging Shield Module".

// The data logger is intended to be connected to a host (e. g. Raspberry Pi) via serial port or I2C.
// It can also be powered over USB.
// The intended use case is to connect some kind of automation software (e. g. OPDID) to the data logger
// for querying current values as well as having an automated job download daily history using arducom-ftp.

// If the data logger does not use a serial input (D0) port for OBIS input, data (real time and stored)
// can be read via the serial port. If the serial input is used for sensor data communication can be made over I2C.

// This data logger supports:
// [PLANNED - up to four S0 lines (digital pins 4 - 7), counting 64 bit values stored in EEPROM]
// - one D0 input (requires the RX pin and one digital output for the optical transistor supply voltage)
// - up to two DHT22 temperature/humidity sensors (suggested data pins are 2 and 3)
// [PLANNED - up to two analog values (analog pins A0 - A1)]

// [ PLANNED
// The S0 lines are queried once a ms using software debouncing.
// The timer interrupt is configured for the ATMEGA328 with a clock speed of 16 MHz. Other CPUs might require adjustments.
// Each S0 line uses eight bytes of EEPROM. At program start the EEPROM values are read into RAM variables.
// Each detected S0 impulse increments its RAM variable by 1. 
// The S0 EEPROM values can be adjusted ("primed") via arducom to set the current meter readings (make sure to 
// take the impulse factor into consideration).
// ]

// The D0 input is a serial input used by electronic power meters. Data is transmitted via an IR LED.
// As the Arduino should still be programmable via USB cable, D0 serial data must be disabled during
// programming. The simplest solution is to switch on the receiving IR transistor's supply voltage on program start.
// Also, the UART must be configured to support the serial protocol (e. g. for an Easymeter Q3D: 9600 baud, 7E1).
// The D0 port needs no persistent storage as the meter will always transmit total values.
// The following circuit can be used to detect D0 data:

// [...]

// Parsed D0 records are matched against added variable definitions and stored in the respective variables.

// To query and set the RTC use the following:
// Assuming I2C, on Linux you can display the current date using the following command:
//  date -d @`./arducom -t i2c -d /dev/i2c-1 -a 5 -c 21 -o Int32 -l 10`
// To set the current date and time, use:
//  date +"%s" | ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 22 -i Int32 -r -l 10

// Timestamped sensor readings are stored on the SD card in a configurable interval.
// SD card log files should be rolled over when they become large. Generally it is advisable to keep these files
// to around 100 kB. Logging 100 bytes every minute causes a daily data volume of 140 kB;
// this should be considered the upper limit. At a baud rate of 57600 such a file will take about 30 seconds
// to download using an Arducom FTP transfer.
// Log files are rolled over each day by creating/appending to a file with name /YYYYMMDD.log.

// The sensor values are invalidated after the log interval. If you query after this point and the value has not been
// set yet, you will read an invalid value. What exactly this invalid value is depends on the type of sensor.
// For OBIS data, the invalid value is -1. For DHT22 data, the invalid value is -9999.
// If you read an invalid value you should retry after a few seconds (depending on the sensor update interval).
// If the value is still invalid you can assume a defective sensor or broken communication.

// Pin map (for Uno; check whether this works with your board):

// D0 - RX: D0 OBIS input (9600 7E1)
// D1 - TX: used for programmer, do not use
// D2: suggested Software Serial RX on Uno (debug)
// D3: suggested Software Serial TX on Uno (debug) 
// D4 - D7: S0 inputs (attention: 4 is Chip Select for some data logger shields)
// D8: suggested data pin for DHT22 A
// D9: suggested data pin for DHT22 B
// D10: Chip Select for SD card on Keyes Data Logger Shield
// D11 - D13: MOSI, MISO, CLK for SD card on Keyes Data Logger Shield
// A0 - A1: analog input
// A2: suggested power supply for OBIS serial input circuit
// A3: Reserved
// A4: I2C SDA
// A5: I2C SCL

// You can also use the HardwareSerial object Serial for OBIS input and debug output at the same time.
// If you want to listen to the output on a Linux system you have to configure the serial port:
// $ stty -F /dev/ttyACM0 cs7 parity -parodd
// You can then listen to the serial output:
// $ cu -s 9600 -e -l /dev/ttyACM0
// Use this only for debugging because it slows down operation.

// This sketch probably fails to build on a Raspberry Pi itself due to old Arduino library versions. 
// It is recommended to build and upload it on an Ubuntu machine with the latest Arduino version. You can also
// compile on Ubuntu, copy the hex file to a Raspberry and upload it from there; in this case you will have
// to fix the /usr/share/arduino/Arduino.mk file by adding an upload target that won't perform a build first:

// < insert below target raw_upload >
//
// upload_hex:     reset raw_upload_hex
//
// raw_upload_hex:
//                $(AVRDUDE) $(AVRDUDE_COM_OPTS) $(AVRDUDE_ARD_OPTS) \
//                        -U flash:w:$(TARGET_HEX):
// < end of insertion >

// As usual with makefiles, take special care of the indentation and whitespace.
// A fixed Arduino.mk file is included with this sketch.

// To query the sensor values you can use the arducom tool. Sensor data is exposed via command 20. To query
// a sensor's data you have to know the sensor's memory address and the number of bytes. 
// For details see section "Variables". Example:
// #define TOTAL_KWH		16		// 0x0010, length 8
// This define specifies that the total kilowatthours of the electric meter are an 8 byte value stored at offset 16.
// To query this value, assuming I2C, use the following command:
// ./arducom -t i2c -d /dev/i2c-1 -a 5 -l 10 -x 5 -c 20 -p 100008 -o Int64
// This sends command 20 to the I2C device at address 5 on I2c bus 1 with a delay of 10 ms and 5 retries.
// Command parameters are three bytes: a two byte offset (lower byte first), and a length byte.
// The output will be formatted as a 64 bit integer. If you want to know what's going on under the hood, use -v.

// This example code is in the public domain.

#include <SPI.h>
#include <SoftwareSerial.h>

#include <dht.h>

// SdFat:
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
// #define SERIAL_STREAM		Serial
#define SERIAL_BAUDRATE		57600

// If you want to use I2C communications, define a slave address.
#define I2C_SLAVE_ADDRESS	5

// If you use software serial output for debugging, specify its pins here.
#define SOFTWARESERIAL_RX		2
#define SOFTWARESERIAL_TX		3
// SoftwareSerial softSerial(SOFTWARESERIAL_RX, SOFTWARESERIAL_TX);

// Specifies a Print object to use for the debug output.
// Undefine this if you don't want to use debugging.
// You cannot use the same Print object for Arducom serial communication
// (for example, Serial). Instead, use a SoftwareSerial port or another
// HardwareSerial on Arduinos with more than one UART.
// Note: This define is for this sketch only. To debug Arducom itself,
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

// DHT22 sensor definitions
#define DHT22_A_PIN			8
#define DHT22_B_PIN			9
#define DHT22_POLL_INTERVAL_MS	3000		// not below 2000 ms (sensor limit)
// invalid value (set if sensor is not used or there is a sensor problem)
#define DHT22_INVALID			-9999

// This pin is switched High after program start. It is intended to provide power to a
// serial data detection circuit (optical transistor or similar) that feeds its output into RX (pin 0).
// During programming (via USB) the pin has high impedance, meaning that no data will arrive from the
// external circuitry that could interfere with the flash data being uploaded.
#define OBIS_IR_POWER_PIN		A2

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
* OBIS parser for D0
*******************************************************/
/* This class parses OBIS values from incoming stream data. According to the registered patterns A-F
* the parsed values are placed in target variables. This class can also write data to a log file. 
* May not yet work for all OBIS data. */
class OBISParser {
public:
	enum { VARTYPE_BYTE = 0, VARTYPE_INT16, VARTYPE_INT32, VARTYPE_INT64 };

private:
	enum { APOS = 0, BPOS, CPOS, DPOS, EPOS, FPOS, VALPOS };
	
	// internal structure for registered variables
	struct OBISVariable {
		uint8_t A;
		uint8_t B;
		uint8_t C;
		uint8_t D;
		uint8_t E;
		uint8_t F;
		int vartype;
		void* ptr;
		OBISVariable* next;
	};
	
	static const uint8_t UNDEF = 0xff;
	Stream* inputStream;
	// current parser state
	uint8_t parsePos;
	uint64_t parseVal;
	uint8_t A;
	uint8_t B;
	uint8_t C;
	uint8_t D;
	uint8_t E;
	uint8_t F;
	// linked list of variables to match
	OBISVariable* varHead;
	static const uint8_t FILEBUFFER_SIZE = 2;
	uint8_t fileBuffer[FILEBUFFER_SIZE];
	uint8_t bufPos;
	
	void startValue() {
		DEBUG(println(F("OBIS startValue")));
		this->parseVal = 0;
	}
	
	void startRecord(void) {
		DEBUG(println(F("OBIS startRecord")));
		this->A = this->B = this->C = this->D = this->E = this->F = UNDEF;
		this->parsePos = APOS;
		this->startValue();
	}
	
public:
	OBISParser(Stream* inputStream) {
		this->inputStream = inputStream;
		// start with unknown parse position
		this->parsePos = UNDEF;
		this->varHead = 0;
		this->bufPos = 0;
	}
	
	void addVariable(uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint8_t E, uint8_t F, int vartype, void* ptr) {
		OBISVariable* var = new OBISVariable();
		var->A = A;
		var->B = B;
		var->C = C;
		var->D = D;
		var->E = E;
		var->F = F;
		var->vartype = vartype;
		var->ptr = ptr;
		// make linked list of variables
		var->next = this->varHead;
		this->varHead = var;
	}
	
	void print64(Print* print, int64_t n) {
		// code copied from: http://www.hlevkin.com/C_progr/long64.c
		  int i = 0;
		  int m;
		  int len;
		  char c;
		  char s = '+';
		char str[21];
		char *pStr = &str[0];

		 // if(n == LONG_LONG_MIN) // _I64_MIN  for Windows Microsoft compiler
		  if(n < -9223372036854775807)
		  {
		    print->print(F("-9223372036854775808"));
		    return;
		  }

		  if( n < 0 )
		  {
		    s = '-';
		    n = - n;
		    pStr[0]='-';
		    i++;
		  }

		  do
		  {
		    m = n % (int64_t)10;
		    pStr[i] = '0'+ m;
		    n = n / (int64_t)10;
		    i++;
		  }
		  while(n != 0);

		  if(s == '+')
		  {
		    len = i;
		  }
		  else /* s=='-' */
		  {
		    len = i-1;
		    pStr++;
		  }

		  for(i=0; i<len/2; i++)
		  {
		    c = pStr[i];
		    pStr[i]       = pStr[len-1-i];
		    pStr[len-1-i] = c;
		  }
		  pStr[len] = 0;

		  if(s == '-')
		  {
		    pStr--;
		  }
		print->print(pStr);
	}
	
	void logData(Print* print, char separator) {
		OBISVariable* var = this->varHead;
		while (var != 0) {
			switch (var->vartype) {
				// do not print invalid values (those < 0)
				case VARTYPE_BYTE: print->print((int)*(uint8_t*)var->ptr); break;
				case VARTYPE_INT16: {
					if (*(int16_t*)var->ptr >= 0) 
						print->print(*(int16_t*)var->ptr);
					break;
				}
				case VARTYPE_INT32: {
					if (*(int32_t*)var->ptr >= 0)
						print->print(*(int32_t*)var->ptr);
					break;
				}
				case VARTYPE_INT64: {
					if (*(int64_t*)var->ptr >= 0)
						this->print64(print, *(int64_t*)var->ptr);
					break;
				}
			}
			print->print(separator);
			var = var->next;
		}
	}

	void doWork(void) {
		// process all available input
		while (this->inputStream->available()) {
			uint8_t c = this->inputStream->read();
			DEBUG(print(F("c: ")));
			DEBUG(println(c));
			
			if (parsePos == UNDEF) {
				// ignore everything until line break
				if (c == 10)
					this->startRecord();
			} else
			if ((c >= '0') && (c <= '9')) {
				parseVal = parseVal * 10 + (c - '0');
			} else
			if (this->parsePos != VALPOS) {
				// manufacturer ID?
				if (c == '/') {
					// ignore until end
					parsePos = UNDEF;
				} else
				// end of field A?
				if (c == '-') {
					if (this->parsePos != APOS) {
						DEBUG(println(F("OBIS Err A")));
						parsePos = UNDEF;
					} else {
						this->A = parseVal;
						this->startValue();
						this->parsePos = BPOS;
					}
				} else
				// end of field B?
				if (c == ':') {
					this->B = parseVal;
					this->startValue();
					this->parsePos = CPOS;
				} else
				// end of field D?
				if ((this->parsePos == DPOS) && (c == '.')) {
					this->D = parseVal;
					this->startValue();
					this->parsePos = EPOS;
				} else
				// end of field C?
				if (c == '.') {
					this->C = parseVal;
					this->startValue();
					this->parsePos = DPOS;
				} else
				if ((this->parsePos == EPOS) && ((c == '*') || (c == '&'))) {
					this->E = parseVal;
					this->startValue();
					this->parsePos = FPOS;
				} else
				if (c == '(') {
					this->startValue();
					this->parsePos = VALPOS;
				}
			} else {
				// parsing VALPOS
				if (c == ')') {
					// end of value
					DEBUG(print(F("A: ")));
					DEBUG(print((int)this->A));
					DEBUG(print(F(" B: ")));
					DEBUG(print((int)this->B));
					DEBUG(print(F(" C: ")));
					DEBUG(print((int)this->C));
					DEBUG(print(F(" D: ")));
					DEBUG(print((int)this->D));
					DEBUG(print(F(" E: ")));
					DEBUG(print((int)this->E));
					DEBUG(print(F(" F: ")));
					DEBUG(print((int)this->F));
					DEBUG(print(F(" Value: H: ")));
					DEBUG(print((uint32_t)(this->parseVal >> 32)));
					DEBUG(print(F(" L: ")));
					DEBUG(println((uint32_t)this->parseVal));
					
					// try to match variables
					OBISVariable* var = this->varHead;
					while (var != 0) {
						if ((var->A == this->A)
							&& (var->B == this->B)
							&& (var->C == this->C)
							&& (var->D == this->D)
							&& (var->E == this->E)
							&& (var->F == this->F)) {
							// match found
							switch (var->vartype) {
								case VARTYPE_BYTE: *(uint8_t*)var->ptr= (uint8_t)this->parseVal; break;
								case VARTYPE_INT16: *(int16_t*)var->ptr = (int16_t)this->parseVal; break;
								case VARTYPE_INT32: *(int32_t*)var->ptr = (int32_t)this->parseVal; break;
								case VARTYPE_INT64: *(int64_t*)var->ptr = (int64_t)this->parseVal; break;
								default: DEBUG(println(F("vartype error")));
							}
						}
						var = var->next;
					}
					// parse up to line terminator
					this->parsePos = UNDEF;
				} else
				// unexpected EOL?
				if (c == 10) {
					DEBUG(println(F("Unexpected EOL")));
					this->startRecord();
				} else {
					// ignore all non-digits
				}
			}	// parsing VALPOS
		}	// while (available)
	}
};

/*******************************************************
* Variables
*******************************************************/

// RAM variables to expose via Arducom
// expose them as a block to save Arducom commands which consume RAM
// to query values the master must know this memory layout table!

// electric readings
#define MOM_PHASE1		0		// 0x0000, length 4
#define MOM_PHASE2		4		// 0x0004, length 4
#define MOM_PHASE3		8		// 0x0008, length 4
#define MOM_TOTAL		12		// 0x000C, length 4
#define TOTAL_KWH		16		// 0x0010, length 8

// DHT22 sensors
#define DHT22_A_TEMP		24		// 0x0018, length 2
#define DHT22_A_HUMID		26		// 0x001A, length 2
#define DHT22_B_TEMP		28		// 0x001C, length 2
#define DHT22_B_HUMID		30		// 0x001E, length 2

#define VAR_TOTAL_SIZE		32		// sum of the above

// use new rather than static allocation to avoid warnings about strict aliasing
// uint8_t* readings = new uint8_t[VAR_TOTAL_SIZE];
uint8_t readings[VAR_TOTAL_SIZE];

// for calculation of free RAM
extern int __heap_start, *__brkval; 
  
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
uint32_t lastWriteMs;

dht DHT;
uint32_t lastDHT22poll;

OBISParser obisParser(&Serial);

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
	
	// TODO query S0 lines
}

// Resets the readings to invalid values.
void resetReadings() {
	// invalidate reading buffer
	memset(&readings[0], 0xFF, VAR_TOTAL_SIZE);
	
	// DHT22 readings have special invalid values (can't use 0xFFFF because -1 may be a valid temperature)
	*(int16_t*)&readings[DHT22_A_TEMP] = DHT22_INVALID;
	*(int16_t*)&readings[DHT22_A_HUMID] = DHT22_INVALID;
	*(int16_t*)&readings[DHT22_B_TEMP] = DHT22_INVALID;
	*(int16_t*)&readings[DHT22_B_HUMID] = DHT22_INVALID;
}

/*******************************************************
* Setup
*******************************************************/

void setup()
{	
	int v; 	
	int16_t freeRam = (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
	
	// initialize serial port for OBIS data
	Serial.begin(9600, SERIAL_7E1);
	
	// switch on OBIS power for serial IR circuitry
#ifdef OBIS_IR_POWER_PIN
	pinMode(OBIS_IR_POWER_PIN, OUTPUT);
	digitalWrite(OBIS_IR_POWER_PIN, HIGH);
#endif

#ifdef SERIAL_STREAM
	SERIAL_STREAM.begin(SERIAL_BAUDRATE);
#endif
	
#ifdef DEBUG_OUTPUT
	// DEBUG_OUTPUT.begin(9600);
	while (!DEBUG_OUTPUT) {}  // Wait for Leonardo.
#endif
	
	resetReadings();
	
	DEBUG(print(F("Free RAM: ")));
	DEBUG(println(freeRam));

	// initialize OBIS parser variables
	// the parser will log this data in reverse order!
	obisParser.addVariable(1, 0, 1, 8, 0, 255, OBISParser::VARTYPE_INT64, &readings[TOTAL_KWH]);
	obisParser.addVariable(1, 0, 1, 7, 0, 255, OBISParser::VARTYPE_INT32, &readings[MOM_TOTAL]);
	obisParser.addVariable(1, 0, 61, 7, 0, 255, OBISParser::VARTYPE_INT32, &readings[MOM_PHASE3]);
	obisParser.addVariable(1, 0, 41, 7, 0, 255, OBISParser::VARTYPE_INT32, &readings[MOM_PHASE2]);
	obisParser.addVariable(1, 0, 21, 7, 0, 255, OBISParser::VARTYPE_INT32, &readings[MOM_PHASE1]);

	// reserved version command (it's recommended to leave this in
	// except if you really have to save flash/RAM)
	arducom.addCommand(new ArducomVersionCommand(freeRam, "Logger"));

	// EEPROM access commands
	// due to RAM constraints we have to expose the whole EEPROM as a block
	arducom.addCommand(new ArducomReadEEPROMBlock(9));
	arducom.addCommand(new ArducomWriteEEPROMBlock(10));

	// expose variables
	// due to RAM constraints we have to expose the whole variable RAM as a block
	// RAM is read-only
	arducom.addCommand(new ArducomReadBlock(20, &readings[0]));

	// connect to RTC
	Wire.begin();
	if (!RTC.begin()) {
		DEBUG(println(F("RTC not functional")));
	} else {
		// register RTC commands
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

	// S0 polling interrupt setup

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
	if (code == ARDUCOM_COMMAND_HANDLED) {
		DEBUG(println(F("Arducom command handled")));
	} else
	if (code != ARDUCOM_OK) {
		DEBUG(print(F("Arducom error: ")));
		DEBUG(println(code));
	}
	
	// let the OBIS parser handle incoming data from the electricity meter
	obisParser.doWork();

	// DHT22
	// poll interval reached?
	if (millis() - lastDHT22poll > DHT22_POLL_INTERVAL_MS) {
		// read sensor values
		#ifdef DHT22_A_PIN
		{
			int chk = DHT.read22(DHT22_A_PIN);
			if (chk == DHTLIB_OK) {
				*(int16_t*)&readings[DHT22_A_HUMID] = DHT.humidity;
				*(int16_t*)&readings[DHT22_A_TEMP] = DHT.temperature;
			} else {
				*(int16_t*)&readings[DHT22_A_HUMID] = DHT22_INVALID;
				*(int16_t*)&readings[DHT22_A_TEMP] = DHT22_INVALID;
			}
		}
		#endif
		#ifdef DHT22_B_PIN
		{
			int chk = DHT.read22(DHT22_B_PIN);
			if (chk == DHTLIB_OK) {
				*(uint16_t*)&readings[DHT22_B_HUMID] = DHT.humidity;
				*(uint16_t*)&readings[DHT22_B_TEMP] = DHT.temperature;
			} else {
				*(int16_t*)&readings[DHT22_B_HUMID] = DHT22_INVALID;
				*(int16_t*)&readings[DHT22_B_TEMP] = DHT22_INVALID;
			}
		}
		#endif

		lastDHT22poll = millis();
	}

	// log interval reached?
	if (millis() - lastWriteMs > LOG_INTERVAL_MS) {
		// determine filename
		// The log file should roll over every new day. So we create a file name that consists
		// of the current date. The file should always be created in the root directory (prefix /).
		char filename[14];
		DateTime now = RTC.now();
		sprintf(filename, "/%04d%02d%02d.log", now.year(), now.month(), now.day());
		
		if (logFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
			// write timestamp
			DateTime now = RTC.now();
			logFile.print(now.unixtime());
			logFile.print(";");
			
			// print DHT22 values (invalid readings are left empty)
			#ifdef DHT22_A_PIN
			if (*(int16_t*)&readings[DHT22_A_TEMP] != DHT22_INVALID)
				logFile.print(*(int16_t*)&readings[DHT22_A_TEMP]);
			logFile.print(";");
			if (*(int16_t*)&readings[DHT22_A_HUMID] != DHT22_INVALID)
				logFile.print(*(int16_t*)&readings[DHT22_A_HUMID]);
			logFile.print(";");
			#endif
			#ifdef DHT22_B_PIN
			if (*(int16_t*)&readings[DHT22_B_TEMP] != DHT22_INVALID)
				logFile.print(*(int16_t*)&readings[DHT22_B_TEMP]);
			logFile.print(";");
			if (*(int16_t*)&readings[DHT22_B_HUMID] != DHT22_INVALID)
				logFile.print(*(int16_t*)&readings[DHT22_B_HUMID]);
			logFile.print(";");
			#endif
			obisParser.logData(&logFile, ';');
			logFile.println();

			logFile.close();
			lastWriteMs = millis();
		}

		// Periodically reset readings. This allows to detect sensor or communication failures.
		resetReadings();
	}
}


