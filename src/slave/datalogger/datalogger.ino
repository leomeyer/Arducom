// Arducom based data logger
// by Leo Meyer <leomeyer@gmx.de>

// Data logger using an SD card and a Real Time Clock DS1307
// Recommended hardware: Arduino Uno or similar with a data logging shield, for example:
// https://learn.adafruit.com/adafruit-data-logger-shield 
// Tested with an Arduino Uno clone and a "KEYES XD-204 Data Logging Shield Module".
// Caution: Remove I2C pullup resistors on 5 V data shields if connecting to a Raspberry Pi
// or another 3.3 V-operated module!
//
// This file is best viewed with a monospace font and tab width 4.

// ********* Description **********
//
// This data logger can be used standalone or connected to a host (e. g. Raspberry Pi) via serial port or I2C.
// It can also be powered over USB.
// The intended use case is to connect some kind of automation software (e. g. OPDID) to the data logger
// for querying current values as well as having an automated job download history data.
// 
// This data logger supports:
// - up to four S0 lines, counting impulses as 64 bit values stored in EEPROM
// - one D0 OBIS data input (requires the RX pin and one output for the optical transistor supply voltage)
// - up to two DHT22 temperature/humidity sensors
// [PLANNED - up to two analog values (analog pins A0 - A1)]
//
// All readings are accessible via Arducom via memory block read (command 20). See "RAM layout" below for details.
// The EEPROM is accessible via Arducom commands 9 (read block) and 10 (write block). See "EEPROM layout" for details.

// ********* S0 **********
//
// The S0 lines are queried once a millisecond using software debouncing using the timer 2.
// The timer interrupt is configured for the ATMEGA328 with a clock speed of 16 MHz. Other CPUs might require adjustments.
// Each S0 line uses eight bytes of EEPROM. At program start the EEPROM values are read into RAM variables.
// S0 EEPROM range starts at 0 (eight bytes S0_A, eight bytes S0_B, eight bytes S0_C, eight bytes S0_D).
// The S0 EEPROM values can be adjusted ("primed") via Arducom to set the current meter readings (make sure to 
// take the impulse factor into consideration).
// Each detected S0 impulse increments its RAM variable by 1. These variables are written to the log file.
// There are also delta RAM variables that are reset after each log interval. These delta variables are of type uint16_t
// which allows a log interval of 65535 imp / 32 imp/s = 2114 seconds maximum before overflow
// at a presumed worst case impulse duration of 30 ms with 1 ms pause.
// After each log interval the delta values are copied to the last values storage. These values represent the
// accumulated impulses during the log interval and can be interpreted as the momentaneous value with respect to the
// log interval.
// S0 values are stored in EEPROM in a configurable interval. The interval is a compromise between EEPROM cell life
// and the amount of data loss in case of a catastrophic failure.
// If the values are written once per hour, with an expected EEPROM cell life of 100k writes the EEPROM can be expected
// to last at least about 11 years. If the cell life should be exhausted the EEPROM range can also be moved to fresh cells.
// The program attempts to detect whether the last start was due to a watchdog reset. If yes, the S0 values in memory
// are not initialized from EEPROM to minimize data loss.
// This may require an updated bootloader version that makes the reset flags available to user programs.

// ********* D0 **********
//
// The D0 input is a serial input used by electronic power meters and other measurement devices.
// As the Arduino should still be programmable via USB cable, D0 serial data input must be disabled during
// programming. The simplest solution is to switch on the receiving IR transistor's supply voltage on program start.
// Also, the UART must be configured to support the serial protocol (e. g. for an Easymeter Q3D: 9600 baud, 7E1).
// The D0 input does not require persistent storage as the meter will always transmit total values.
// The following circuit can be used to detect D0 data that is transmitted via an IR LED:

// [...]

// Parsed D0 records are matched against added variable definitions and stored in the respective variables.
//
// If the data logger does not use a serial input (D0) port for OBIS input, data (real time and stored)
// can be read via Arducom over the serial port. If the serial input is used for D0 Arducom communication 
// can be made over I2C.

// ********* RTC **********
//
// This program exposes a Real Time Clock DS1307 via Arducom. It is also used internally for log file names
// and timestamping records.
// To query and set the RTC use the following:
// Assuming I2C, on Linux you can display the current date using the following command:
//  date -d @`./arducom -t i2c -d /dev/i2c-1 -a 5 -c 21 -o Int32 -l 10`
// To set the current date and time, use:
//  date +"%s" | ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 22 -i Int32 -r -l 30
//
// Important! You must set the time using this command before data is logged into timestamped files!
// Otherwise data will be logged to the file "/fallback.log."
// It is also advisable to set the time once a month to allow the logger to validate the RTC date.
//
// The time interface uses UTC timestamps (for ease of use with the Linux tools).
// Internally the RTC also runs on UTC time. To correcly determine date rollovers for log files
// you need to specify a timezone offset value to convert between UTC and local time.
// If you don't do this the logger won't be able to determine the beginning of a new day correctly
// and the timestamps in the file probably won't match the file date.
// The timezone offset is stored in EEPROM as an int16_t at offset 32 (0x20). It needs to be changed if you want
// to move the device to a different timezone, or if you want to compensate for daylight saving time changes.
// You can read or adjust the timezone offset using the Arducom EEPROM read and write block commands.
// The log files will contain UTC timestamps, however.

// ********* Logging **********
//
// Timestamped sensor readings are stored on the SD card in a configurable interval.
// SD card log files should be rolled over when they become larger. Generally it is advisable to keep these files
// to around 100 kB. Logging 100 bytes every minute causes a daily data volume of 140 kB;
// this should be considered the upper limit. At a baud rate of 57600 such a file will take about 30 seconds
// to download using an Arducom FTP transfer.
// Log files are rolled over each day by creating/appending to a file with name /YYYYMMDD.log.
// To correctly determine the day the TIMEZONE_OFFSET_SECONDS is added to the RTC time (which is UTC).
// Data that cannot be reliably timestamped (due to RTC or I2C problems) is appended to the file /fallback.log.

// ********* Validation **********
//
// The sensor values, except S0 impulse counters, are invalidated after the log interval.
// If you query after this point and the value has not been re-set yet, you will read an invalid value. 
// What exactly this invalid value is depends on the type of sensor.
// For OBIS (D0) data, the invalid value is -1. For DHT22 data, the invalid value is -9999.
// If you read an invalid value you should retry after a few seconds (depending on the sensor update interval).
// If the value is still invalid you can assume a defective sensor or broken communication.

// ********* GPIO pin map **********
//
// Pin map for Uno; check whether this works with your board:
//
// D0 - RX: D0 OBIS input (9600 7E1)
// D1 - TX: used for programmer, do not use
// D2: suggested Software Serial RX on Uno
// D3: suggested Software Serial TX on Uno 
// D4 - D7: suggested S0 inputs (attention: 4 is Chip Select for some data logger shields)
// D8: suggested data pin for DHT22 A
// D9: suggested data pin for DHT22 B
// D10: Chip Select for SD card on Keyes Data Logger Shield
// D11 - D13: MOSI, MISO, CLK for SD card on Keyes Data Logger Shield
// A0 - A1: analog input
// A2: suggested power supply for OBIS serial input circuit
// A3: Reserved
// A4: I2C SDA
// A5: I2C SCL

// ********* Debug output **********
//
// You can use the HardwareSerial object Serial for OBIS input and debug output at the same time.
// If you want to listen to the output on a Linux system you have to configure the serial port:
// $ stty -F /dev/ttyACM0 cs7 parity -parodd
// You can then listen to the serial output:
// $ cu -s 9600 -e -l /dev/ttyACM0
// Use this only for debugging because it slows down operation. Do not send data using cu while
// the OBIS parser is reading data from the serial input.

// ********* Building **********
//
// This sketch probably fails to build on a Raspberry Pi itself due to old Arduino library versions. 
// It is recommended to build and upload it on an Ubuntu machine with the latest Arduino version
// (1.6.5 at the time of writing). You can also compile on Ubuntu, copy the hex file to a Raspberry
// and upload it from there; in this case you will have to install arduino-core and arduino-mk and
// edit /usr/share/arduino/Arduino.mk by adding an upload target that won't perform a build first:

/* < insert below target raw_upload >

upload_hex:     reset raw_upload_hex

raw_upload_hex:
                $(AVRDUDE) $(AVRDUDE_COM_OPTS) $(AVRDUDE_ARD_OPTS) \
                        -U flash:w:$(TARGET_HEX):i

< end of insertion > */

// As usual with makefiles, take special care of the indentation and whitespace.
// A fixed Arduino.mk file is included with this sketch.

// ********* Transferring data to a host **********
//
// To query the sensor values you can use the "arducom" tool on Linux. If you are not using the RX pin for
// OBIS data you can use the serial interface for the Arducom system. If you use OBIS data or the RX pin is
// otherwise in use, you can use I2C to connect your host (e. g. a Raspberry Pi).
// You can download history data or the current log file using the "arducom-ftp" tool.
//
// Sensor data is exposed via command 20. To query a sensor's data you have to know the sensor's memory 
// variable address and length in bytes. For details see section "RAM layout" below. Example:
//
// #define TOTAL_KWH		16		// 0x0010, length 8
// This means that the total kilowatthours of the electric meter are an 8 byte value stored at offset 16.
// To query this value, assuming I2C, use the following command:
// ./arducom -t i2c -d /dev/i2c-1 -a 5 -l 10 -x 5 -c 20 -p 100008 -o Int64
// This sends command 20 to the I2C device at address 5 on I2C bus 1 with a delay of 10 ms and 5 retries.
// Command parameters are three bytes: a two byte offset (lower byte first), and a length byte.
// The output will be formatted as a 64 bit integer. If you want to know what's going on under the hood, use -v.

// ********* RAM layout *********

// OBIS electric readings
#define MOM_PHASE1			0		// 0x0000, length 4, momentary power consumption phase 1
#define MOM_PHASE2			4		// 0x0004, length 4, momentary power consumption phase 2
#define MOM_PHASE3			8		// 0x0008, length 4, momentary power consumption phase 3
#define MOM_TOTAL			12		// 0x000C, length 4, momentary power consumption total
#define TOTAL_KWH			16		// 0x0010, length 8, total energy consumption

// DHT22 sensors
#define DHT22_A_TEMP		24		// 0x0018, length 2
#define DHT22_A_HUMID		26		// 0x001A, length 2
#define DHT22_B_TEMP		28		// 0x001C, length 2
#define DHT22_B_HUMID		30		// 0x001E, length 2

// S0 values
#define S0_A_VALUE			32		// 0x0020, length 8
#define S0_B_VALUE			40		// 0x0028, length 8
#define S0_C_VALUE			48		// 0x0030, length 8
#define S0_D_VALUE			56		// 0x0038, length 8

// S0 deltas since last writing of log file
#define S0_A_DELTA			64		// 0x0040, length 2
#define S0_B_DELTA			66		// 0x0042, length 2
#define S0_C_DELTA			68		// 0x0044, length 2
#define S0_D_DELTA			70		// 0x0046, length 2

// S0 deltas of the last interval
#define S0_A_LAST			72		// 0x0048, length 2
#define S0_B_LAST			74 		// 0x004A, length 2
#define S0_C_LAST			76 		// 0x004C, length 2
#define S0_D_LAST			78		// 0x004E, length 2

#define VAR_TOTAL_SIZE		80		// sum of the above lengths

// ********* EEPROM layout *********
// 
// The EEPROM can be accessed using the Arducom commands 9 (read) and 10 (write).
// To read eight bytes at offset 0x08, use the following command (assume I2C):
// ./arducom -t i2c -d /dev/i2c-1 -a 5 -l 10 -x 5 -c 9 -p 080008 -o Int64
// To set the timezone offset at position 0x20 to 7200 (two hours), use the following command (assume I2C):
// ./arducom -t i2c -d /dev/i2c-1 -a 5 -l 10 -x 5 -c 10 -p 2000 -i Int16 -p 7200
// 
// EEPROM map:
// 0 - 7   (0x00), length 8: S0_A counter
#define EEPROM_S0COUNTER_A		0x00
// 8 - 15  (0x08), length 8: S0_B counter
#define EEPROM_S0COUNTER_B		0x08
// 16 - 23 (0x10), length 8: S0_C counter
#define EEPROM_S0COUNTER_C		0x10
// 23 - 31 (0x18), length 8: S0_D counter
#define EEPROM_S0COUNTER_D		0x18
#define EEPROM_S0COUNTER_LEN	0x08
// 32 - 33 (0x20), length 2: local timezone offset in seconds (positive or negative)
#define EEPROM_TIMEZONE			0x20
// 34 - 37 (0x22), length 4: last RTC date as set by the user
#define EEPROM_RTCDATETIME		0x22

// ********* Watchdog *********
// 
// Execution of this program can be monitored by the Atmega watchdog so that a reset is performed
// when the program appears to hang (due to I2C problems, SD card reading failures etc.)
// However, this can cause an endless reboot loop with older versions of the Arduino Optiboot boot loader.
// You will have to test whether it works with your board. To enable it define the ENABLE_WATCHDOG macro.
// It is recommended to set the watchdog timer rather high, for example 4 or 8 seconds (you must use
// the predefined timer constants WDTO_x for this), to allow for delays interfacing with the SD card.
// The downside of long watchdog delays is the loss of data during the wait period if the program hangs.
// The program will attempt to detect whether a reset was caused by the watchdog. This mechanism works
// by checking the respective bit in the MCUSR register. However, this method is not reliable as the
// register may have been modified by a bootloader. Therefore the program installs a handler for the
// WDT_vect interrupt handler and sets the WDIE bit in the watchdog controller register WDTCSR. If a
// watchdog timeout occurs the WDT_vect routine is first called which sets a memory token variable to
// a special value. The MCU will then perform a second watchdog timeout which causes the actual reset.
// This means that the effective watchdog timeout (time until reset) is twice the value specified here.
//#define ENABLE_WATCHDOG		1
#define WATCHDOG_TIMEOUT	WDTO_4S
//
// Watchdog behavior can be tested by setting bit 6 of mask and flags of the Arducom version command 0:
// ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 0 -p 4040
// Be careful! If the watchdog is not enabled this will hang the device!
// A software reset using the watchdog can be done by setting bit 7 of mask and flags of the Arducom version command 0: 
// ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 0 -p 8080
// As this is an Arducom function it will do the watchdog reset regardless of the ENABLE_WATCHDOG define.
// You can test whether this worked using the Arducom version command (which will tell you the uptime):
// ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 0

// See the section "Configuration" below for further settings and explanations.

// This code is in the public domain.

#include <avr/eeprom.h>
#include <avr/wdt.h>

#include <SPI.h>
#include <SoftwareSerial.h>
#include <WSWire.h>

// use RTClib from Adafruit
// https://github.com/adafruit/RTClib
#include <RTClib.h>

// DHTlib:
// https://github.com/RobTillaart/Arduino
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
// #define SERIAL_STREAM	Serial
#define SERIAL_BAUDRATE		9600

// If you want to use I2C communications, define a slave address.
#define I2C_SLAVE_ADDRESS	5

// If you use software serial output for debugging, specify its pins here.
#define SOFTWARESERIAL_RX	2
#define SOFTWARESERIAL_TX	3
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

#define S0_A_PIN			4
// #define S0_B_PIN			5
// #define S0_C_PIN			6
// #define S0_D_PIN			7

// DHT22 sensor definitions
#define DHT22_A_PIN					8
#define DHT22_B_PIN					9
#define DHT22_POLL_INTERVAL_MS		3000		// not below 2000 ms (sensor limit)
// invalid value (set if sensor is not used or there is a sensor problem)
#define DHT22_INVALID				-9999

// This pin is switched High after program start. It is intended to provide power to a
// serial data detection circuit (optical transistor or similar) that feeds its output into RX (pin 0).
// After reset and during programming (via USB) the pin has high impedance, meaning that no data will 
// arrive from the external circuitry that could interfere with the flash data being uploaded.
// Undefining this macro switches off OBIS functionality.
#define OBIS_IR_POWER_PIN	A2

// file log interval (milliseconds)
#define LOG_INTERVAL_MS		60000

// interval for S0 EEPROM transfer (seconds)
#define EEPROM_INTERVAL_S	3600

/*******************************************************
* Global helper routines
*******************************************************/

void print64(Print* print, int64_t n) {
	// code copied from: http://www.hlevkin.com/C_progr/long64.c
	  int i = 0;
	  int m;
	  int len;
	  char c;
	  char s = '+';
	char str[21];
	char *pStr = &str[0];

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

/*******************************************************
* RTC access command implementation (for getting and
* setting of RTC time)
*******************************************************/

RTC_DS1307 RTC;  // define the Real Time Clock object

// RTC time is externally represented as a UNIX timestamp
// (32 bit integer, UTC). These two command classes implement
// getting and setting of the RTC clock time.

class ArducomGetTime: public ArducomCommand {
public:
	ArducomGetTime(uint8_t commandCode) : ArducomCommand(commandCode, 0) {}		// this command expects zero parameters
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
		// read RTC time
		DateTime now = RTC.now();
		uint32_t unixTS = now.unixtime();
		*((uint32_t*)destBuffer) = unixTS;
		*dataSize = 4;
		return ARDUCOM_OK;
	}
};

class ArducomSetTime: public ArducomCommand {
public:
	ArducomSetTime(uint8_t commandCode) : ArducomCommand(commandCode, 4) {}		// this command expects four bytes as parameters
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
		// get parameter
		uint32_t unixTS = *((uint32_t*)dataBuffer);
		// construct and set RTC time
		DateTime dt(unixTS);
		RTC.adjust(dt);
		// store date in EEPROM (for RTC date validation)
		eeprom_update_dword((uint32_t*)EEPROM_RTCDATETIME, unixTS);
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
						print64(print, *(int64_t*)var->ptr);
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
// to query values the master must know the memory layout table (see above)

// exposed variables as array
// Do not automatically initialize these values at program start.
// That way, sensor readings can be kept after a watchdog reset.
// The setup code detects this condition and initializes the RAM accordingly.
uint8_t readings[VAR_TOTAL_SIZE] __attribute__ ((section (".noinit")));

/* Timer2 reload value, globally available */
// for S0 impulse detection
unsigned int tcnt2;

// Arducom system variables
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

// SD card and logging related variables
SdFat sdFat;
uint8_t sdCardOK;
uint32_t lastWriteMs;
uint32_t lastOKDateFromRTC;

bool rtcOK;

// DHT sensor 
dht DHT;
uint32_t lastDHT22poll;

#ifdef OBIS_IR_POWER_PIN
// OBIS functionality
OBISParser obisParser(&Serial);
#endif

// S0 counters
#ifdef S0_A_PIN
volatile int8_t s0ACounter __attribute__ ((section(".noinit")));
volatile uint8_t s0AIncrement __attribute__ ((section(".noinit")));
#endif
#ifdef S0_B_PIN
volatile int8_t s0BCounter __attribute__ ((section(".noinit")));
volatile uint8_t s0BIncrement __attribute__ ((section(".noinit")));
#endif
#ifdef S0_C_PIN
volatile int8_t s0CCounter __attribute__ ((section(".noinit")));
volatile uint8_t s0CIncrement __attribute__ ((section(".noinit")));
#endif
#ifdef S0_D_PIN
volatile int8_t s0DCounter __attribute__ ((section(".noinit")));
volatile uint8_t s0DIncrement __attribute__ ((section(".noinit")));
#endif

uint32_t lastEEPROMWrite;

// store the reset flags to be able to detect watchdog resets
uint8_t resetFlags __attribute__ ((section(".noinit")));
// fallback if the reset flags are not preserved
// this token is set to 0x1234 in the watchdog interrupt
// and cleared after program start
volatile uint16_t wdt_token __attribute__ ((section(".noinit")));

/*******************************************************
* Routines
*******************************************************/

// taken from: https://code.google.com/p/arduino/issues/attachmentText?id=794&aid=7940002001&name=resetFlags_appCode.cpp
void resetFlagsInit(void) __attribute__ ((naked)) __attribute__ ((section (".init0")));
void resetFlagsInit(void)
{
	// save the reset flags passed from the bootloader
	__asm__ __volatile__ ("mov %0, r2\n" : "=r" (resetFlags) :);
}

// taken from SdFat examples
// call back for file timestamps, used by the SdFat library
void dateTime(uint16_t* date, uint16_t* time) {
	if (RTC.isrunning()) {
		DateTime now = RTC.now();
		// return date using FAT_DATE macro to format fields
		*date = FAT_DATE(now.year(), now.month(), now.day());
		// return time using FAT_TIME macro to format fields
		*time = FAT_TIME(now.hour(), now.minute(), now.second());
	} else {
		// fallback
		*date = FAT_DATE(2001, 1, 1);
		*time = FAT_TIME(0, 0, 0);
	}
}

/*
 * Install the Interrupt Service Routine (ISR) for Timer2 overflow.
 * This is normally done by writing the address of the ISR in the
 * interrupt vector table but conveniently done by using ISR()  */
ISR(TIMER2_OVF_vect) {
	/* Reload the timer */
	TCNT2 = tcnt2;
	
	// query S0 lines (active low)
	// Software debouncing works by counting up a counter variable
	// each iteration while the pin is low (active). If the pin goes high (inactive)
	// the counter is reset. 
	// As per definition of the S0 interface an impulse is at least 30 ms
	// long. We assume that this refers to the debounced impulse, i. e. the
	// bounces before the impulse are not part of the impulse length.
	// This means that at soon as a counter reaches 30 it means that the pin
	// has been active for the specified time and the impulse should be counted.
	// When an impulse should be counted its increment is increased. The main loop
	// must add the increment to the actual value counters. This should be done
	// by disabling interrupts, copying the increment, zeroing the increment,
	// enabling interrupts, and then only adding the increment to the values
	// in order to lose as little interrupt cycles as possible (disabling interrupts
	// can cause lost impulses).
	// The main loop should have a certain minimum speed to avoid overflow of the
	// eight bit increments. Worst case assumptions are: impulse duration 30 ms, 
	// impulse is off for 1 ms. This means that every 31 ms an impulse is counted.
	// It takes 256 * 31 ms = 7936 ms to overflow the counter in this scenario which
	// appears to be enough time for the main loop. In practice, impulses are expected
	// to be much longer anyway.
	
	#ifdef S0_A_PIN
	// get line state
	if (digitalRead(S0_A_PIN)) {
		// pin is inactive, reset counter
		s0ACounter = 0;
	} else {
		// pin is active
		// test most common case first (performance)
		if (s0ACounter > 31) {
			// counter is 32; impulse is counted; do nothing
		} else
		if (s0ACounter == 31) {
			s0AIncrement++;
			s0ACounter++;
		} else {
			s0ACounter++;
		}
	}
	#endif
	#ifdef S0_B_PIN
	// get line state
	if (digitalRead(S0_B_PIN)) {
		// pin is inactive, reset counter
		s0BCounter = 0;
	} else {
		// pin is active
		// test most common case first (performance)
		if (s0BCounter > 31) {
			// counter is 32; impulse is counted; do nothing
		} else
		if (s0BCounter == 31) {
			s0BIncrement++;
			s0BCounter++;
		} else {
			s0BCounter++;
		}
	}
	#endif
	#ifdef S0_C_PIN
	// get line state
	if (digitalRead(S0_C_PIN)) {
		// pin is inactive, reset counter
		s0CCounter = 0;
	} else {
		// pin is active
		// test most common case first (performance)
		if (s0CCounter > 31) {
			// counter is 32; impulse is counted; do nothing
		} else
		if (s0CCounter == 31) {
			s0CIncrement++;
			s0CCounter++;
		} else {
			s0CCounter++;
		}
	}
	#endif
	#ifdef S0_D_PIN
	// get line state
	if (digitalRead(S0_D_PIN)) {
		// pin is inactive, reset counter
		s0DCounter = 0;
	} else {
		// pin is active
		// test most common case first (performance)
		if (s0DCounter > 31) {
			// counter is 32; impulse is counted; do nothing
		} else
		if (s0DCounter == 31) {
			s0DIncrement++;
			s0DCounter++;
		} else {
			s0DCounter++;
		}
	}
	#endif
}

// this routine is called the first time the watchdog timeout occurs
ISR(WDT_vect) {
	wdt_token = 0x1234;
	// go into infinite loop, let the watchdog do the reset
	while (true) ;
}

// Resets the readings to invalid values.
void resetReadings() {
	// invalidate reading buffer for OBIS data
	memset(&readings[0], 0xFF, TOTAL_KWH + 8);
	
	// DHT22 readings have special invalid values (can't use 0xFFFF because -1 may be a valid temperature)
	*(int16_t*)&readings[DHT22_A_TEMP] = DHT22_INVALID;
	*(int16_t*)&readings[DHT22_A_HUMID] = DHT22_INVALID;
	*(int16_t*)&readings[DHT22_B_TEMP] = DHT22_INVALID;
	*(int16_t*)&readings[DHT22_B_HUMID] = DHT22_INVALID;
	
	// do not reset S0 values; these are set from EEPROM at program start

	// reset S0 deltas
	*(uint16_t*)&readings[S0_A_DELTA] = 0;
	*(uint16_t*)&readings[S0_B_DELTA] = 0;
	*(uint16_t*)&readings[S0_C_DELTA] = 0;
	*(uint16_t*)&readings[S0_D_DELTA] = 0;
}

DateTime utcToLocal(DateTime utc) {
	// timezone offset is stored in EEPROM
	int16_t offset = eeprom_read_word((const uint16_t *)EEPROM_TIMEZONE);
	// not initialized?
	if (offset == -1)
		return utc;
	uint32_t unixtime = utc.unixtime();
	unixtime += offset;
	return DateTime(unixtime);
}

// log the message to a file
void log(const __FlashStringHelper* message, bool ln = true, bool timestamp = true) {
	DEBUG(println(message));
	if (sdCardOK) {
		SdFile f;
		if (f.open("/datalogr.log", O_RDWR | O_CREAT | O_AT_END)) {
			if (timestamp) {
				if (rtcOK) {
					// write timestamp to file
					DateTime now = utcToLocal(RTC.now());
					f.print(now.year());
					f.print(F("-"));
					if (now.month() < 10)
						f.print(F("0"));
					f.print(now.month());
					f.print(F("-"));
					if (now.day() < 10)
						f.print(F("0"));
					f.print(now.day());
					f.print(F(" "));
					if (now.hour() < 10)
						f.print(F("0"));
					f.print(now.hour());
					f.print(F(":"));
					if (now.minute() < 10)
						f.print(F("0"));
					f.print(now.minute());
					f.print(F(":"));
					if (now.second() < 10)
						f.print(F("0"));
					f.print(now.second());
					f.print(F(" "));
				} else {
					f.print(F("<time unknown>      "));
				}
			}
			if (ln)
				f.println(message);
			else
				f.print(message);
			f.close();
		}
	}	
}

/*******************************************************
* Setup
*******************************************************/

void setup()
{	
	// disable watchdog in case it's still on
	wdt_disable();

	#ifdef DEBUG_OUTPUT
	// DEBUG_OUTPUT.begin(9600);
	while (!DEBUG_OUTPUT) {}  // Wait for Leonardo.
	#endif
	
	// **** Initialize hardware components ****

	// initialize I2C
	Wire.begin();

	// deactivate internal pullups for twi
	digitalWrite(SDA, 0);
	digitalWrite(SCL, 0);
	
	// initialize SD system
	if (sdFat.begin(SDCARD_CHIPSELECT, SPI_HALF_SPEED)) {
		sdCardOK = 1;
	}

	// connect to RTC (try three times because I2C may be busy sometimes)
	int repeat = 3;
	while (!rtcOK && (repeat > 0))  {
		rtcOK = RTC.isrunning();
		repeat--;
		delay(10);
	}

	if (rtcOK && sdCardOK)
		// set date time callback function (for file modification date)
		SdFile::dateTimeCallback(dateTime);

	log(F("DataLogger starting..."));
	log(F("Build: "), false);
	log(F(__DATE__), false, false);
	log(F(" "), false, false);
	log(F(__TIME__), true, false);
	
	if (!rtcOK)
		log(F("RTC not functional"));
	
	// **** Initialize memory for readings ****

	// reset by watchdog?
	if ((resetFlags & _BV(WDRF)) || (wdt_token == 0x1234)) {
		log(F("Watchdog reset detected"));
		
		// do not initialize the S0 values and counters in order not to lose data after watchdog reset
		
	} else {
		// startup after power failure or manual reset
		log(F("Normal startup detected"));
		
		// read S0 values from EEPROM
		eeprom_read_block(&readings[S0_A_VALUE], (const uint8_t*)EEPROM_S0COUNTER_A, EEPROM_S0COUNTER_LEN);
		eeprom_read_block(&readings[S0_B_VALUE], (const uint8_t*)EEPROM_S0COUNTER_B, EEPROM_S0COUNTER_LEN);
		eeprom_read_block(&readings[S0_C_VALUE], (const uint8_t*)EEPROM_S0COUNTER_C, EEPROM_S0COUNTER_LEN);
		eeprom_read_block(&readings[S0_D_VALUE], (const uint8_t*)EEPROM_S0COUNTER_D, EEPROM_S0COUNTER_LEN);
		
		// clear S0 counters
		#ifdef S0_A_PIN
		s0ACounter = 0;
		s0AIncrement = 0;
		#endif
		#ifdef S0_B_PIN
		s0BCounter = 0;
		s0BIncrement = 0;
		#endif
		#ifdef S0_C_PIN
		s0CCounter = 0;
		s0CIncrement = 0;
		#endif
		#ifdef S0_D_PIN
		s0DCounter = 0;
		s0DIncrement = 0;
		#endif		
	}
	
	resetReadings();
	
	// reset watchdog reset detector token
	wdt_token = 0;

	// **** Initialize S0 lines ****

	#ifdef S0_A_PIN
	pinMode(S0_A_PIN, INPUT);
	digitalWrite(S0_A_PIN, HIGH);	// enable pullup
	#endif
	#ifdef S0_B_PIN
	pinMode(S0_B_PIN, INPUT);
	digitalWrite(S0_B_PIN, HIGH);	// enable pullup
	#endif
	#ifdef S0_C_PIN
	pinMode(S0_C_PIN, INPUT);
	digitalWrite(S0_C_PIN, HIGH);	// enable pullup
	#endif
	#ifdef S0_D_PIN
	pinMode(S0_D_PIN, INPUT);
	digitalWrite(S0_D_PIN, HIGH);	// enable pullup
	#endif

	// **** Initialize OBIS parsing ****
	
	#ifdef OBIS_IR_POWER_PIN
	// initialize serial port for OBIS data
	Serial.begin(9600, SERIAL_7E1);
	// switch on OBIS power for serial IR circuitry
	pinMode(OBIS_IR_POWER_PIN, OUTPUT);
	digitalWrite(OBIS_IR_POWER_PIN, HIGH);

	// initialize OBIS parser variables
	// the parser will log this data in reverse order!
	// Easymeter Q3D OBIS records
	obisParser.addVariable(1, 0, 1, 8, 0, 255, OBISParser::VARTYPE_INT64, &readings[TOTAL_KWH]);
	obisParser.addVariable(1, 0, 1, 7, 0, 255, OBISParser::VARTYPE_INT32, &readings[MOM_TOTAL]);
	obisParser.addVariable(1, 0, 61, 7, 0, 255, OBISParser::VARTYPE_INT32, &readings[MOM_PHASE3]);
	obisParser.addVariable(1, 0, 41, 7, 0, 255, OBISParser::VARTYPE_INT32, &readings[MOM_PHASE2]);
	obisParser.addVariable(1, 0, 21, 7, 0, 255, OBISParser::VARTYPE_INT32, &readings[MOM_PHASE1]);
	#endif

	// **** Initialize Arducom ****
	
	#ifdef SERIAL_STREAM
	SERIAL_STREAM.begin(SERIAL_BAUDRATE);
	#endif
	
	// reserved version command (it's recommended to leave this in
	// except if you really have to save flash/RAM)
	// it can also test the watchdog and perform a software reset
	arducom.addCommand(new ArducomVersionCommand("Logger"));

	// EEPROM access commands
	// due to RAM constraints we have to expose the whole EEPROM as a block
	arducom.addCommand(new ArducomReadEEPROMBlock(9));
	arducom.addCommand(new ArducomWriteEEPROMBlock(10));

	// expose variables
	// due to RAM constraints we have to expose the whole variable RAM as one read-only block
	arducom.addCommand(new ArducomReadBlock(20, &readings[0]));

	if (rtcOK) {
		// register RTC commands
		// Assuming I2C, on Linux you can display the current date using the following command:
		//  date -d @`./arducom -t i2c -d /dev/i2c-1 -a 5 -c 21 -o Int32 -l 10`
		// To set the current date and time, use:
		//  date +"%s" | ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 22 -i Int32 -r -l 10
		arducom.addCommand(new ArducomGetTime(21));
		// EXPERIMENTALLY DISABLED
		// to check whether RTC corruption occurs due to runaway code or I2C bus glitches
		// arducom.addCommand(new ArducomSetTime(22));
	}
	
	if (sdCardOK) {
		log(F("Adding FTP commands"));
		// initialize FTP system (adds FTP commands)
		arducomFTP.init(&arducom, &sdFat);
	}
	
	// **** S0 polling interrupt setup ****

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

	// **** Watchdog setup ****
	
	#ifdef ENABLE_WATCHDOG
	wdt_enable(WATCHDOG_TIMEOUT);
	WDTCSR |= (1 << WDIE);
	#endif

	log(F("DataLogger started."));
}

/*******************************************************
* Main loop
*******************************************************/

void loop()
{
	// reset watchdog timer
	wdt_reset();
	
	// handle Arducom commands
	int code = arducom.doWork();
	if (code == ARDUCOM_COMMAND_HANDLED) {
		DEBUG(println(F("Arducom command handled")));
	} else
	if (code != ARDUCOM_OK) {
		DEBUG(print(F("Arducom error: ")));
		DEBUG(println(code));
	}
	
	#ifdef OBIS_IR_POWER_PIN
	// let the OBIS parser handle incoming data
	obisParser.doWork();
	#endif
	
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
	
	// S0 impulse counters
	#ifdef S0_A_PIN
	{
		cli();	// disable interrupts
		uint8_t incr = s0AIncrement;
		s0AIncrement = 0;
		sei();	// enable interrupts
		
		// add increment to values
		*(uint64_t*)&readings[S0_A_VALUE] += incr;
		*(uint16_t*)&readings[S0_A_DELTA] += incr;
	}
	#endif	
	#ifdef S0_B_PIN
	{
		cli();	// disable interrupts
		uint8_t incr = s0BIncrement;
		s0BIncrement = 0;
		sei();	// enable interrupts
		
		// add increment to values
		*(uint64_t*)&readings[S0_B_VALUE] += incr;
		*(uint16_t*)&readings[S0_B_DELTA] += incr;
	}
	#endif	
	#ifdef S0_C_PIN
	{
		cli();	// disable interrupts
		uint8_t incr = s0CIncrement;
		s0CIncrement = 0;
		sei();	// enable interrupts
		
		// add increment to values
		*(uint64_t*)&readings[S0_C_VALUE] += incr;
		*(uint16_t*)&readings[S0_C_DELTA] += incr;
	}
	#endif	
	#ifdef S0_D_PIN
	{
		cli();	// disable interrupts
		uint8_t incr = s0DIncrement;
		s0DIncrement = 0;
		sei();	// enable interrupts
		
		// add increment to values
		*(uint64_t*)&readings[S0_D_VALUE] += incr;
		*(uint16_t*)&readings[S0_D_DELTA] += incr;
	}
	#endif	

	// log interval reached?
	if (millis() - lastWriteMs > LOG_INTERVAL_MS) {
		// can write to SD card?
		if (sdCardOK) {		
			// determine log file name
			// The log file should roll over at the start of every day. So we create a file name that consists
			// of the current RTC date. The file should always be created in the root directory (prefix /).
			
			// The date from the RTC must be validated quite strictly.
			// I2C communication is not always reliable. The RTC may be off or not set.
			// Writing to a wrong file because of RTC errors can cause data loss.
			// Common errors are year, month or day values being 0 or out of range.
			// Another common error is a date too far in the future. This probably results from I2C problems
			// when querying values at the same time as the RTC is being read. The Wire and RTClib libraries
			// do not contain any mechanism to detect or avoid such problems, i. e. when an external master
			// speaks to the Arduino slave, and the Arduino simultaneously tries to access the RTC as 
			// a master itself.
			// If we can't determine a current timestamp that looks valid after several attempts
			// we log to a "fallback file" that is called "/fallback.log".
			// Records ending up in this file probably do not have a valid timestamp, so data cannot be easily
			// correlated; however, the presence of such a file (and its size) can indicate problems
			// with the RTC which can then be further investigated.
			
			bool dateOK = false;
			// Each try will take about 3 ms. The worst case duration of a running I2C transfer, which
			// might interfere with the RTC request, can be calculated like: baud rate / byte size * max length.
			// With baud rate assumed to be 100 kHz, byte size = 10 bits and 
			// max length = 32 we get a worst case transfer length of 3.2 ms. 
			int8_t getDateRetries = 10;
			DateTime nowUTC;
			uint32_t nowUnixtime;
			uint32_t lastUnixtime = 0;
			int8_t goodCounter = 0;
			
			while (!dateOK && (getDateRetries > 0)) {
				// try to get the time
				// condition: RTC must be running
				if (RTC.isrunning()) {
					nowUTC = RTC.now();
					nowUnixtime = nowUTC.unixtime();

					// check plausibility
					// RTC year must not be lower than the creation year of this program
					// month and day values must be valid (it's possible to have invalid values which RTClib will not complain about)
					if ((nowUTC.year() >= 2015) && (nowUTC.month() >= 1) && (nowUTC.month() <= 12) && (nowUTC.day() >= 1) && (nowUTC.day() <= 31)) {
					
						// check: if there is a valid last known ok date, and the new date is not too far off, assume it's ok
						// allow some delta which is three times the log interval (convert to seconds)
						if ((lastOKDateFromRTC > 0) && (nowUnixtime > lastOKDateFromRTC) && (nowUnixtime < lastOKDateFromRTC + LOG_INTERVAL_MS / 1000 * 3)) {
							dateOK = true;
							break;
						}
						
						// there is no last known ok date (possibly first run after startup)
						// check whether there's a date stored in EEPROM
						// this is set when the user initializes the date using Arducom
						uint32_t eepromRTC = eeprom_read_dword((const uint32_t*)EEPROM_RTCDATETIME);
						// EEPROM date set?
						if (eepromRTC != 0xffffffff) {
							// check: current time is greater than the stored date and not too far off (allow a larger delta for this check)
							if ((nowUnixtime > eepromRTC) && (nowUnixtime < eepromRTC + 60L * 60L * 24L * 30L))	{	// may be about one month off
								dateOK = true;
								break;
							} else {
								// if we end up here we check whether the RTC time is consistent over a few reads
								// if it is, we simply assume that the date is correct
								// last time set?
								if (lastUnixtime > 0) {
									// may not be more than five seconds off (in practice it will be much less)
									if (nowUnixtime - lastUnixtime < 5) {
										goodCounter++;
									} else {
										// time is way off from what we read before; assume the RTC or I2C is flaky
										goodCounter = 0;
									}
									if (goodCounter > 5) {
										// we have read about the same time more than five times
										// should be ok then
										dateOK = true;
										break;
									}
								}
								lastUnixtime = lastUnixtime;
							}
						}

						// there is no valid EEPROM date
						// maybe the user has never set the RTC
						// assume that the date is not valid
					}
				}
				getDateRetries--;
				// wait for a short while
				delay(3);
			}

			if (dateOK) {
					// remember last known or assumed good date
					lastOKDateFromRTC = nowUnixtime;
			}
			
			DateTime now;
			char filename[14];
			SdFile logFile;

			if (!dateOK) {
				// log to the fallback file
				strcpy(filename, "/fallback.log");
				DEBUG(println(F("RTC date implausible")));
			} else {
				// convert to local time
				now = utcToLocal(nowUTC);
				sprintf(filename, "/%04d%02d%02d.log", now.year(), now.month(), now.day());
			}

			// reset watchdog timer (file operations may be slow)
			wdt_reset();
			if (logFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
				// write timestamp in UTC
				logFile.print(nowUTC.unixtime());
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
		
				#ifdef OBIS_IR_POWER_PIN
				// log OBIS data
				obisParser.logData(&logFile, ';');
				#endif
				
				// log S0 counters
				#ifdef S0_A_PIN
				print64(&logFile, *(int64_t*)&readings[S0_A_VALUE]);
				logFile.print(";");
				#endif
				#ifdef S0_B_PIN
				print64(&logFile, *(int64_t*)&readings[S0_B_VALUE]);
				logFile.print(";");
				#endif
				#ifdef S0_C_PIN
				print64(&logFile, *(int64_t*)&readings[S0_C_VALUE]);
				logFile.print(";");
				#endif
				#ifdef S0_D_PIN
				print64(&logFile, *(int64_t*)&readings[S0_D_VALUE]);
				logFile.print(";");
				#endif
				logFile.println();
				
				// reset watchdog timer (file operations may be slow)
				wdt_reset();
				logFile.close();
				lastWriteMs = millis();
			}
		}	// if (dateOK)
		
		// transfer D0 delta values to last interval values
		*(uint16_t*)&readings[S0_A_LAST] = *(uint16_t*)&readings[S0_A_DELTA];
		*(uint16_t*)&readings[S0_B_LAST] = *(uint16_t*)&readings[S0_B_DELTA];
		*(uint16_t*)&readings[S0_C_LAST] = *(uint16_t*)&readings[S0_C_DELTA];
		*(uint16_t*)&readings[S0_D_LAST] = *(uint16_t*)&readings[S0_D_DELTA];
	
		// Periodically reset readings. This allows to detect sensor or communication failures.
		resetReadings();
	}
	
	wdt_reset();

	// EEPROM transfer interval reached?
	if (millis() / 1000 - lastEEPROMWrite > EEPROM_INTERVAL_S) {
		
		// write S0 values to EEPROM
		eeprom_update_block((const void*)&readings[S0_A_VALUE], (void*)EEPROM_S0COUNTER_A, EEPROM_S0COUNTER_LEN);
		eeprom_update_block((const void*)&readings[S0_B_VALUE], (void*)EEPROM_S0COUNTER_B, EEPROM_S0COUNTER_LEN);
		eeprom_update_block((const void*)&readings[S0_C_VALUE], (void*)EEPROM_S0COUNTER_C, EEPROM_S0COUNTER_LEN);
		eeprom_update_block((const void*)&readings[S0_D_VALUE], (void*)EEPROM_S0COUNTER_D, EEPROM_S0COUNTER_LEN);
		
		lastEEPROMWrite = millis() / 1000;
		
		log(F("S0 values stored"));
	}
}
