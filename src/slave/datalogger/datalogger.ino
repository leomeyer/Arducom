// Arducom based data logger
// Copyright (c) 2015-2016 Leo Meyer, leo@leomeyer.de
//
// This code is in the public domain.

// Data logger using an SD card and a Real Time Clock DS1307
// Recommended hardware: Arduino Uno or similar with a data logging shield, for example:
// https://learn.adafruit.com/adafruit-data-logger-shield 
// If using an Ethernet shield with an SD card and an RTC, an Arduino Uno may be too small.
// For such advanced purposes please use an Arduino Mega or similar.
// Tested with an Arduino Uno clone and a "KEYES XD-204 Data Logging Shield Module".
// This is also the default configuration using serial transport at 57600 baud.
// Caution: Remove I2C pullup resistors on 5 V data shields if connecting to a Raspberry Pi
// or another 3.3 V-operated module that uses internal pullups on its I2C pins!
//
// For interoperation with a Raspberry Pi it is recommended to use the software I2C slave
// implementation instead of a multi-master setup if using other peripherals like an RTC.
// Software I2C supports a maximum baud rate of about 40 kHz.
//
// This sketch can also be used without RTC and SD card for testing purposes.
// In this case, the respective Arducom commands will not be present.
// Startup time will also be *much* longer. If using a serial connection you will have to
// set the Arducom --initDelay parameter to about 7000 (ms) to get a response if your driver
// resets the Arduino during the serial connect.
//
// Important: To start logging to timestamped files see the instructions under the "RTC" section.
//
// All Arducom command line examples are given for the I2C transport. If using for example serial
// transport, change "/dev/i2c-1" to your serial device name and remove the "-a 5" parameter.
// 
// This file is best viewed with a monospace font and tab width 4.

// ********* Description **********
//
// This data logger can be used standalone or connected to a host (e. g. Raspberry Pi) via serial port or I2C.
// The intended use case is to connect some kind of automation software (e. g. OPDID) to the data logger
// for querying current values as well as having an automated job download history data.
// 
// This data logger supports:
// - up to four S0 lines, counting impulses as 64 bit values stored in EEPROM
// - one D0 OBIS data input (requires the RX pin and one output for the optical transistor supply voltage)
// - up to two DHT22 temperature/humidity sensors with 0.1 °C resolution (multiplied by 10 and stored as 16-bit integers)
//
// All readings are accessible via Arducom via memory block read (command 20). See "RAM layout" below for details.
// The EEPROM is accessible via Arducom commands 9 (read block) and 10 (write block). See "EEPROM layout" for details.
// 
// The logger can write history data to an SD card; this data can be downloaded using the arducom-ftp tool.

// ********* Arducom command codes *********
// 
// This sketch exposes the following Arducom commands:
//
//  0: Default Arducom version/status command
//  9: Read EEPROM block
// 10: Write EEPROM block
// 20: Read RAM (see RAM layout below)
// 21: Get time from RTC (if RTC is present)
// 22: Set time to RTC and EEPROM (if RTC is present)
// 30: Write RAM (see RAM layout below)
// 60+: FTP commands (if SD card is present)

// ********* S0 **********
//
// An S0 line is an impulse based interface to devices like water, gas or electricity meters.
// An S0 impulse is specified to be at least 30 ms long. This logger counts the impulses as 64 bit values.
// The S0 lines are queried about once a millisecond using software debouncing using the timer 2.
// The timer interrupt is configured for the ATMEGA328 with a clock speed of 16 MHz. Other CPU speeds might require adjustments.
// Each S0 line uses eight bytes of EEPROM. At program start the EEPROM values are read into RAM variables.
// S0 EEPROM range starts at 0 (eight bytes S0_A, eight bytes S0_B, eight bytes S0_C, eight bytes S0_D).
// The S0 EEPROM values can be adjusted ("primed") via Arducom to set the current meter readings (make sure to 
// take the impulse factor of the respective device into consideration).
// Each detected S0 impulse increments its RAM variable by 1. These variables are written to the log file.
// S0 values are stored in EEPROM in a configurable interval. The interval is a compromise between EEPROM cell life
// and the amount of data loss in case of a catastrophic failure.
// If the values are written once per hour, with an expected EEPROM cell life of 100k writes the EEPROM can be expected
// to last at least about 11 years. If the cell life should be exhausted the EEPROM range can also be moved to fresh cells.
// The program attempts to detect whether the last start was due to a watchdog reset. If yes, the S0 values in memory
// are not overwritten from EEPROM to minimize data loss.
// In the event of a requested shutdown (shutdown button pressed) the S0 values are stored in EEPROM, too.
// Also, you can send 0xFFFF as payload to the version command 0 to initiate the shutdown.
//
// Transmitting large data blocks via software I2C may cause S0 timing to become inaccurate. This may affect devices 
// that send impulses very fast, approaching the 30 ms per impulse limit. This is unlikely to occur in practice.

// ********* D0 **********
//
// The D0 interface is a serial interface used by electronic power meters and other measurement devices.
// Data transmitted over this interface conforms to the OBIS specification ("Object Identification System").
// This implementation is for the Easymeter Q3D. It can be extended to support other devices.
// This datalogger uses the hardware serial port to read the serial data (on Uno). This may conflict with the bootloader
// during programming because the bootloader also uses the serial port.
// As the Arduino should still be programmable via USB cable, D0 serial data input must be disabled during
// programming. The simplest solution is to switch on the receiving IR transistor's supply voltage on program start only.
// Consequently the reset before programming disables the power, preventing conflicting serial data from arriving.
// The UART must be configured to support the serial protocol after start (e. g. for an Easymeter Q3D: 9600 baud, 7E1).
// The D0 input does not require persistent storage as the meter will always transmit total values.
// The following circuit can be used to detect D0 data that is transmitted via an IR LED:
//
//
// OBIS_IR_       ___
// POWER_PIN  o--|___|--.
//               100k   |
//                      |          .---o RX
//                 -> |/           |
//       IR LED    -> |  BPW40     |
//                 -> |>           |
//                      |        |/
//                      o--------|  BC548
//                      |        |>
//                     .-.         |
//                     | |         |
//                 100k| |         |
//                     '-'         |
//                      |          |
//            o---------o----------'
//         GND
//
// Parsed D0 records are matched against added variable definitions and stored in the respective variables.
//
// If the data logger does not use a serial input (D0) port for OBIS input, data (real time and stored)
// can be read via Arducom over the serial port. If the serial input is used for D0 Arducom communication 
// can take place over I2C.

// ********* RTC **********
//
// This program exposes a Real Time Clock DS1307 via Arducom. The RTC is also used internally for log file names
// and timestamping records.
// To query and set the RTC use the following:
// Assuming I2C, on Linux you can display the current date using the following command:
// $ date -d @`./arducom -d /dev/i2c-1 -a 5 -c 21 -o Int32`
// To set the current date and time, use:
// $ date +"%s" | ./arducom -d /dev/i2c-1 -a 5 -c 22 -i Int32 -r
//
// Important! You must set the time using this command before data is logged into timestamped files!
// Otherwise data will be logged to the file "/fallback.log".
// It is also advisable to set the RTC time regularly from a known good time source (such as NTP)
// because the RTC might not be very accurate. It also allows the logger to validate the RTC date.
// Setting the RTC too often should be avoided however because the value is stored in EEPROM causing wear.
// Once per hour should probably be ok.
//
// The time interface uses UTC timestamps (for ease of use with the Linux tools).
// Internally the RTC also runs on UTC time. To correctly determine date rollovers for log files
// you need to specify a timezone offset value to convert between UTC and local time.
// If you don't do this the logger won't be able to determine the beginning of a new day correctly
// and the (UTC) timestamps in the file probably won't match the (local) file date.
// The timezone offset is stored in EEPROM as an int16_t at offset 32 (0x20). It needs to be changed if you want
// to move the device to a different timezone, or if you want to adjust for daylight saving time changes.
// You can read or adjust the timezone offset using the Arducom EEPROM read and write block commands.
// To set the timezone offset to 7200 (two hours), use the following command (assume I2C):
// $ ./arducom -d /dev/i2c-1 -a 5 -c 10 -p 2000 -i Int16 -p 7200
//
// It is advised to automate daylight saving changes and RTC corrections (e. g. setting them once per hour).

// ********* Logging **********
//
// Timestamped sensor readings are stored on the SD card in a configurable interval (default: 1 minute).
// SD card log files should be rolled over when they become larger. Generally it is advisable to keep these files
// to around 100 kB. Logging 100 bytes every minute causes a daily data volume of about 140 kB;
// this should be considered the upper limit. At a baud rate of 57600 (or an I2C bus speed of 50 kHz)
// such a file will take about 30 seconds to download using an Arducom FTP transfer.
// Log files are rolled over each day by creating/appending to a file with name /YYYYMMDD.log.
// To correctly determine the date the TIMEZONE_OFFSET_SECONDS is added to the RTC time (which is UTC).
// Data that cannot be reliably timestamped (due to RTC or I2C problems) is appended to the file /fallback.log.
// To facilitate a clean shutdown you can add a shutdown button which, when pressed, writes all relevant
// current values to the EEPROM, closes all files and halts the system. Use this e. g. before changing SD cards.

// ********* Validation **********
//
// The sensor values, except S0 impulse counters, are invalidated after the log interval.
// If you query after this point and the value has not been re-set yet, you will read an invalid value. 
// What exactly this invalid value is depends on the type of sensor.
// For OBIS (D0) data, the invalid value is -1. For DHT22 data, the invalid value is -9999.
// DHT22 temperature data is read as a double, multiplied by 10 and stored as a 16 bit integer.
// If you read an invalid value you should retry after a few seconds (depending on the sensor update interval).
// If the value is still invalid you can assume a defective sensor or broken communication.
// Tools along the chain that process the sensor data should account for temporarily invalid data.

// ********* GPIO pin map **********
//
// Suggested pin map for Uno; check whether this works if using a different board:
//
// 0 (RX): optional D0 OBIS input
// 1 (TX): used for programmer, do not use
// 2: Software Serial RX on Uno (unused if not using software serial)
// 3: Software Serial TX on Uno (unused if not using software serial) 
// 4 - 7: S0 inputs (attention: 4 is SD card chip select for some data logger shields)
// 8: optional data pin for DHT22 A
// 9: optional data pin for DHT22 B
// 10: Chip Select for SD card on Keyes Data Logger Shield
// 11 - 13: MOSI, MISO, SCK for SD card on Keyes Data Logger Shield (13 is also the default LED pin)
// A0: Software I2C SDA (unused if not using software I2C)
// A1: Software I2C SCL (unused if not using software I2C)
// A2: optional power supply for OBIS serial input circuit
// A3: optional shutdown button (active if low)
// A4: I2C SDA (unused if not using I2C)
// A5: I2C SCL (unused if not using I2C)

// ********* Debug output **********
//
// You can use the HardwareSerial instance Serial for OBIS input and debug output at the same time.
// If you want to listen to the output on a Linux system you can listen to the serial output:
// $ cu -s 9600 -e -l /dev/ttyACM0
// Use this only for debugging because it slows down operation. Do not send data using cu while
// the OBIS parser is reading data from the serial input.

// ********* Building **********
//
// Building this sketch on a Raspberry Pi requires recent Arduino library versions. 
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
// To upload the hex file, use "make upload_hex". It may be necessary to build on the Raspberry first
// to create make dependencies. After that, copy the hex file to the Raspberry and upload it.

// ********* Transferring data to a host **********
//
// To query the sensor values you can use the "arducom" tool on Linux. If you are not using the RX pin for
// OBIS data you can use the serial interface for the Arducom data transfer. If you use OBIS data or the RX pin is
// otherwise in use you must use a different transport to connect your host (e. g. a Raspberry Pi).
// You can download data files using the "arducom-ftp" tool.
// If you are using USB serial communication make sure that your serial USB driver does not reset the Arduino
// while connecting. If it does it will interrupt logging and data may be lost.
//
// When doing I2C with a Raspberry Pi you must use the software I2C library to avoid
// bus conflicts with the RTC as the Raspberry Pi does not support multi-master setups. Please see the example
// in lib/SoftwareI2CSlave for information on how to setup and test such a configuration.
// The recommended speed for software I2C is 40 kHz.
// It is possible that the software I2C interrupt messes up the timing for the DHT22 sensor queries.
// This may be the case during extended data downloads.
//
// Sensor data is exposed via command 20. To query a sensor's data you have to know the sensor's memory 
// variable address and length in bytes. For details see section "RAM layout" below. Example:
//
// #define TOTAL_KWH		16		// 0x0010, length 8
// This means that the total kilowatthours of the electric meter are an 8 byte value stored at offset 16.
// To query this value, assuming I2C, use the following command:
// $ ./arducom -d /dev/i2c-1 -a 5 -c 20 -p 100008 -o Int64
// This sends command 20 to the I2C device at address 5 on I2C bus 1.
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
#define DHT22_A_TEMP		24		// 0x0018, length 2 (divide value by 10 to get a resolution of 0.1 °C)
#define DHT22_A_HUMID		26		// 0x001A, length 2
#define DHT22_B_TEMP		28		// 0x001C, length 2 (divide value by 10 to get a resolution of 0.1 °C)
#define DHT22_B_HUMID		30		// 0x001E, length 2

// S0 values
#define S0_A_VALUE			32		// 0x0020, length 8
#define S0_B_VALUE			40		// 0x0028, length 8
#define S0_C_VALUE			48		// 0x0030, length 8
#define S0_D_VALUE			56		// 0x0038, length 8

#define VAR_TOTAL_SIZE		64		// sum of the above lengths

// ********* EEPROM layout *********
// 
// The EEPROM can be accessed using the Arducom commands 9 (read) and 10 (write).
// To read eight bytes at offset 0x08, use the following command (assume I2C):
// ./arducom -d /dev/i2c-1 -a 5 -c 9 -p 080008 -o Int64
// To set the timezone offset at position 0x20 to 7200 (two hours), use the following command (assume I2C):
// ./arducom -d /dev/i2c-1 -a 5 -c 10 -p 2000 -i Int16 -p 7200
// 
#define EEPROM_S0COUNTER_LEN	0x08
// EEPROM map:
// 0 - 7   (0x00), length 8: S0_A counter
#define EEPROM_S0COUNTER_A		0x00
// 8 - 15  (0x08), length 8: S0_B counter
#define EEPROM_S0COUNTER_B		0x08
// 16 - 23 (0x10), length 8: S0_C counter
#define EEPROM_S0COUNTER_C		0x10
// 23 - 31 (0x18), length 8: S0_D counter
#define EEPROM_S0COUNTER_D		0x18
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
// The program will attempt to detect whether a reset was caused by the watchdog. It installs a handler for the
// WDT_vect interrupt handler and sets the WDIE bit in the watchdog controller register WDTCSR. If a
// watchdog timeout occurs the WDT_vect routine is first called which sets a memory token variable to
// a special value. The MCU will then perform a second watchdog timeout which causes the actual reset.
// This means that the effective watchdog timeout (time until reset) is twice the value specified here.
#define ENABLE_WATCHDOG		1
#define WATCHDOG_TIMEOUT	WDTO_4S
//
// Watchdog behavior can be tested by setting bit 6 of mask and flags of the Arducom version command 0:
// ./arducom -d /dev/i2c-1 -a 5 -c 0 -p 4040
// Be careful! If the watchdog is not enabled this will hang the device!
// A software reset using the watchdog can be done by setting bit 7 of mask and flags of the Arducom version command 0: 
// ./arducom -d /dev/i2c-1 -a 5 -c 0 -p 8080
// As this is an Arducom function it will do the watchdog reset regardless of the ENABLE_WATCHDOG define.
// You can test whether this worked using the Arducom version command (which will tell you the uptime):
// ./arducom -d /dev/i2c-1 -a 5 -c 0

// See the section "Configuration" below for further settings and explanations.

#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <Arduino.h>

#include <SPI.h>
#include <SoftwareSerial.h>
#include <WSWire.h>

#include <Arducom.h>
#include <ArducomI2C.h>
#include <ArducomEthernet.h>
#include <ArducomStream.h>
#include <ArducomFTP.h>

/*******************************************************
* Configuration
*******************************************************/

// Define the Arducom transport method. You can use:
// 1. Hardware Serial: Define SERIAL_STREAM and SERIAL_BAUDRATE.
// 2. Software Serial: Initialize a SoftwareSerial instance and assign it to SERIAL_STREAM.
// 3. Hardware I2C: Define I2C_SLAVE_ADDRESS.
// 4. Software I2C: Define I2C_SLAVE_ADDRESS and SOFTWARE_I2C.
// 5. Ethernet: Define ETHERNET_PORT. An Ethernet shield is required.

// 1. Hardware Serial
// Warning! This setting conflicts with the OBIS data parser which also uses the serial port!
// Undefine OBIS_IR_POWER_PIN if you want to test serial communication.
// #define SERIAL_STREAM		Serial
#define SERIAL_BAUDRATE		57600

// 2. Software serial connection (for example with a Bluetooth module)
// #define SOFTSERIAL_RX_PIN	8
// #define SOFTSERIAL_TX_PIN	9
// SoftwareSerial softSerial(SOFTSERIAL_RX_PIN, SOFTSERIAL_TX_PIN);
// #define SERIAL_STREAM		softSerial
// #define SERIAL_BAUDRATE		9600

// 3. Hardware I2C communication: define a slave address
#define I2C_SLAVE_ADDRESS	5

// 4. Software I2C: additionally define SOFTWARE_I2C
#define SOFTWARE_I2C

// 5. Ethernet
// #define ETHERNET_PORT			ARDUCOM_TCP_DEFAULT_PORT
// #define ETHERNET_MAC			0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
// #define ETHERNET_IP			192, 168, 0, 177

// If this macro is defined the internal I2C pullups on SDA and SCL are activated.
// This will cause those lines to have a voltage of 5 V which may damage connected equipment
// that runs on less than 5 V (e. g. a Raspberry Pi).
// Normally it is not necessary to define this macro because external I2C hardware should
// contain hardware pullup resistors. 
// #define	I2C_PULLUPS			1

// If using software I2C specify the configuration here
// (see ../lib/SoftwareI2CSlave/SoftwareI2CSlave.h).
#if defined I2C_SLAVE_ADDRESS && defined SOFTWARE_I2C

	// The buffer size in bytes for the send and receive buffer
	#define I2C_SLAVE_BUFSIZE		ARDUCOM_BUFFERSIZE

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

	#include "../lib/SoftwareI2CSlave/SoftwareI2CSlave.h"
	
#endif	// SOFTWARE_I2C

// If you use software serial output for debugging, specify its pins here.
// Note that you cannot use software serial and software I2C at the same time!
// #define SOFTWARESERIAL_RX	2
// #define SOFTWARESERIAL_TX	3
// #include <SoftwareSerial.h>
// SoftwareSerial softSerial(SOFTWARESERIAL_RX, SOFTWARESERIAL_TX);

// Specifies a Print object to use for the debug output.
// Undefine this if you don't want to use debugging.
// You cannot use the same Print object for Arducom serial communication
// (for example, Serial). Instead, use a SoftwareSerial port or another
// HardwareSerial on Arduinos with more than one UART.
// Note: This define is for this sketch only. To debug Arducom itself,
// use the define USE_ARDUCOM_DEBUG below. Arducom will also use this output.
// #define DEBUG_OUTPUT		Serial

// If USE_ARDUCOM_DEBUG is defined Arducom will output debug messages on DEBUG_OUTPUT.
// This will greatly slow down communication, so don't use this during normal operation.
// Requires Arducom to be compiled with debug support. To enable debug support
// set ARDUCOM_DEBUG_SUPPORT to 1 in Arducom.h.
#if ARDUCOM_DEBUG_SUPPORT == 1
// #define USE_ARDUCOM_DEBUG
#endif

// The chip select pin depends on the type of SD card shield.
// The Keyes Data Logger Shield uses pin 10 for chip select.
// The W5100 Ethernet shield uses pin 4 for chip select.
#define SDCARD_CHIPSELECT	10

// Specifies whether the DS1307 Real Time Clock should be used.
// If you don't have a DS1307 connected (via I2C), comment this define.
// #define USE_DS1307        1

// S0 pin definitions. If you do not use a pin comment it out for performance.
#define S0_A_PIN			5
// #define S0_B_PIN			5
// #define S0_C_PIN			6
// #define S0_D_PIN			7

// DHT22 sensor definitions
#define DHT22_A_PIN					8
#define DHT22_B_PIN					9
#define DHT22_POLL_INTERVAL_MS		3000		// not below 2000 ms (sensor limit)
// invalid value (set if sensor is not used or there is a sensor problem)
#define DHT22_INVALID				-9999

// Define OBIS_IR_POWER_PIN if you want to use the OBIS parser.
// This pin is switched High after program start. It is intended to provide power to a
// serial data detection circuit (optical transistor or similar) that feeds its output into RX (pin 0).
// After reset and during programming (via USB) the pin has high impedance, meaning that no data will 
// arrive from the external circuitry that could interfere with the flash data being uploaded.
// Undefining this macro switches off OBIS parsing and logging.
#define OBIS_IR_POWER_PIN	A2
// serial stream to use for OBIS data
#define OBIS_STREAM		Serial
#define OBIS_BAUDRATE		9600
// older Arduino libraries (< 102) do not yet have serial protocol configuration constants
#ifdef SERIAL_7E1
#define OBIS_PROTOCOL		SERIAL_7E1
#else
// you have to specify the bits for the UCSRC register manually
#define OBIS_PROTOCOL		0x24	// ((1 << UPM1) | (0 < USBS) | (1 << UCSZ1))
#endif

// Define this macro for OBIS debugging. This will generate a lot of output (sent to DEBUG_OUTPUT)
// which may interfere with programming.
// Use it only if you encounter problems parsing OBIS data.
// #define OBIS_DEBUG			1

// file log interval (milliseconds)
#define LOG_INTERVAL_MS		60000

// interval for S0 EEPROM transfer (seconds)
#define EEPROM_INTERVAL_S	3600

// Undefine this if you are not using a shutdown button (not recommended).
#define SHUTDOWN_BUTTON		A3
// LED pin to blink to indicate successful shutdown
#define LED_PIN				13

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
* Dependent macros
*******************************************************/

// Macro for debug output
#ifdef DEBUG_OUTPUT
#define DEBUG(x) if (DEBUG_OUTPUT) (DEBUG_OUTPUT).x
#else
#define DEBUG(x) /* x */
#endif

// The following macros are used to translate S0 pins to ports and bits.
// They work for the Arduino Uno but may need work for other boards.
#define PIN_TO_PORT(P)	\
(((P) >= 0 && (P) <= 7) ? PIND : (((P) >= 8 && (P) <= 13 ? PINB : PINC)))

#define PIN_TO_BIT(P)	\
(((P) >= 0 && (P) <= 7) ? P : (((P) >= 8 && (P) <= 13 ? (P - 8) : (P - 14))))	

/*******************************************************
* Global helper routines
*******************************************************/

void print64(Print* print, int64_t n) {
	// code adapted from: http://www.hlevkin.com/C_progr/long64.c
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
#ifdef USE_DS1307

// use RTClib from Adafruit
// https://github.com/adafruit/RTClib
#include <RTClib.h>

RTC_DS1307 RTC;  // define the Real Time Clock object

// RTC time is externally represented as a UNIX timestamp
// (32 bit integer, UTC). These two command classes implement
// getting and setting of the RTC clock time.

class ArducomGetTime: public ArducomCommand {
public:
	ArducomGetTime(uint8_t commandCode) : ArducomCommand(commandCode, 0) {}		// this command expects zero parameters
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
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
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
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

#endif

/*******************************************************
* OBIS parser for D0
*******************************************************/
#ifdef OBIS_IR_POWER_PIN

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
		uint8_t vartype;
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
		#ifdef OBIS_DEBUG
		DEBUG(println(F("OBIS startValue")));
		#endif
		this->parseVal = 0;
	}
	
	void startRecord(void) {
		#ifdef OBIS_DEBUG
		DEBUG(println(F("OBIS startRecord")));
		#endif
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
	
	void addVariable(uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint8_t E, uint8_t F, uint8_t vartype, void* ptr) {
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
			#ifdef OBIS_DEBUG
			DEBUG(print(F("c: ")));
			DEBUG(println(c));
			#endif
			
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
						#ifdef OBIS_DEBUG
						DEBUG(println(F("OBIS Err A")));
						#endif
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
					#ifdef OBIS_DEBUG
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
					#endif
					
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
					#ifdef OBIS_DEBUG
					DEBUG(println(F("Unexpected EOL")));
					#endif
					this->startRecord();
				} else {
					// ignore all non-digits
				}
			}	// parsing VALPOS
		}	// while (available)
	}
};

#endif // ifdef OBIS_IR_POWER_PIN

/*******************************************************
* Variables
*******************************************************/

// RAM variables to expose via Arducom
// Expose them as a block to save Arducom commands which consume RAM.
// To query values the master must know the memory layout table (see above).

// exposed variables as array
// Do not automatically initialize these values at program start.
// That way, sensor readings can be kept after a watchdog reset.
// The setup code detects this condition and initializes the RAM accordingly.
uint8_t readings[VAR_TOTAL_SIZE] __attribute__ ((section (".noinit")));

// Timer2 reload value for S0 impulse detection
unsigned int tcnt2;

// Arducom system variables
#ifdef SERIAL_STREAM
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
#ifdef SDCARD_CHIPSELECT

// SdFat:
// https://github.com/greiman/SdFat
#include <SdFat.h>
#include <SdFatUtil.h> 

SdFat sdFat;
#endif
uint8_t sdCardOK;
uint32_t lastWriteMs;
uint32_t lastOKDateFromRTC;
bool rtcOK;
bool initiateShutdown;		// set to true by callback to command 0

#if defined DHT22_A_PIN || defined DHT22_B_PIN

// DHTlib:
// https://github.com/RobTillaart/Arduino
#include <dht.h>

// DHT sensor 
dht DHT;
uint32_t lastDHT22poll;
#endif

#ifdef OBIS_IR_POWER_PIN
// OBIS functionality
OBISParser obisParser(&OBIS_STREAM);
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

// this token is set to 0x1234 in the watchdog interrupt
// and cleared after program start, to distinguish a watchdog reset
// from normal powerup
volatile uint16_t wdt_token __attribute__ ((section(".noinit")));

/*******************************************************
* Routines
*******************************************************/
#ifdef USE_DS1307

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

uint32_t utcToLocal(uint32_t utc) {
  // timezone offset is stored in EEPROM
  int16_t offset = eeprom_read_word((const uint16_t *)EEPROM_TIMEZONE);
  // not initialized?
  if (offset == -1)
    return utc;
  uint32_t unixtime = utc + offset;
  return unixtime;
}

#endif

#if defined S0_A_PIN || defined S0_B_PIN || defined S0_C_PIN || defined S0_D_PIN
// Interrupt Service Routine (ISR) for Timer2 overflow (S0 impulse detection)
ISR(TIMER2_OVF_vect) {
	// reload the timer
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
	
	// This code needs to be optimized because every delay interferes with
	// other interrupts (notably causing software I2C errors).

	// Naive version:
/*
  	// get line state
	if (digitalRead(S0_A_PIN)) {
		...
	} else {
		...
	}
*/	
// The following code avoids digitalRead because it imposes a heavy performance penalty.

	#ifdef S0_A_PIN
	// get line state
	if ((PIN_TO_PORT(S0_A_PIN) >> (PIN_TO_BIT(S0_A_PIN))) & 1) {
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
	if ((PIN_TO_PORT(S0_B_PIN) >> (PIN_TO_BIT(S0_B_PIN))) & 1) {
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
	if ((PIN_TO_PORT(S0_C_PIN) >> (PIN_TO_BIT(S0_C_PIN))) & 1) {
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
	if ((PIN_TO_PORT(S0_D_PIN) >> (PIN_TO_BIT(S0_D_PIN))) & 1) {
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
#endif

// this routine is called the first time the watchdog timeout occurs
ISR(WDT_vect) {
	wdt_token = 0x1234;
	// the watchdog interrupt flag is automatically cleared
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
}

// log the message to a file
void log(const __FlashStringHelper* message, bool ln = true, bool timestamp = true) {
	if (ln) {
		DEBUG(println(message));
	} else {
		DEBUG(print(message));
	}
	if (sdCardOK) {
		SdFile f;
		if (f.open("/datalogr.log", O_RDWR | O_CREAT | O_AT_END)) {
			#ifdef USE_DS1307
			if (timestamp) {
				if (rtcOK) {
					// write timestamp to file
					DateTime now(utcToLocal(RTC.now().unixtime()));
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
			#endif
			if (ln)
				f.println(message);
			else
				f.print(message);
			f.close();
		}
	}	
}

void shutdownHook() {
	initiateShutdown = true;
}

/*******************************************************
* Setup
*******************************************************/

void setup() {	
	// disable watchdog in case it's still on
	wdt_disable();

	// configure shutdown button as input, pullup on
	#ifdef SHUTDOWN_BUTTON
	digitalWrite(SHUTDOWN_BUTTON, HIGH);
	#endif

	// initialize debug output (only if the OBIS parser is not used)
	#if defined DEBUG_OUTPUT && !defined OBIS_IR_POWER_PIN
	DEBUG_OUTPUT.begin(SERIAL_BAUDRATE);
	while (!DEBUG_OUTPUT) {}  // Wait for Leonardo.
	#endif

	// initialize OBIS port to get the correct log output (if DEBUG_OUTPUT is used)
	#ifdef OBIS_IR_POWER_PIN
		#if ARDUINO >= 102
		OBIS_STREAM.begin(OBIS_BAUDRATE, OBIS_PROTOCOL);
		#else
		OBIS_STREAM.begin(OBIS_BAUDRATE);
		#warning Trying to set OBIS serial configuration directly; this may work or not depending on your system
		#if defined(__AVR_ATmega8__)
		UCSRC = (1 << URSEL) | OBIS_PROTOCOL;	// select UCSRC (shared with UBRRH)
		#else
		UCSR0C = OBIS_PROTOCOL;
		#endif
		#endif
	#endif
	
	// **** Initialize hardware components ****

	// initialize I2C
	Wire.begin();

	#if defined I2C_PULLUPS
	// activate internal pullups for I2C
	digitalWrite(SDA, 1);
	digitalWrite(SCL, 1);
	#endif

	#ifdef SDCARD_CHIPSELECT
	// initialize SD system
	if (sdFat.begin(SDCARD_CHIPSELECT, SPI_HALF_SPEED)) {
		sdCardOK = 1;
	}
	#endif
	
	#ifdef USE_DS1307
	// connect to RTC (try three times)
	int repeat = 3;
	while (!rtcOK && (repeat > 0))  {
		rtcOK = RTC.isrunning();
		repeat--;
		delay(10);
	}
	if (rtcOK && sdCardOK)
		// set date time callback function (for file modification date)
		SdFile::dateTimeCallback(dateTime);
	#endif

	log(F("DataLogger starting..."));
	log(F("Build: "), false);
	log(F(__DATE__), false, false);
	log(F(" "), false, false);
	log(F(__TIME__), true, false);
	
	if (!rtcOK)
		log(F("RTC not present"));
	else
		log(F("RTC OK"));

	#ifdef SDCARD_CHIPSELECT
	if (!sdCardOK)
		log(F("SD card not present"));
	else
		log(F("SD card OK"));
	#endif
	
	// **** Initialize memory for readings ****

	// reset by watchdog?
	if (wdt_token == 0x1234) {
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
	
	// clear watchdog reset detector token
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
	
	#ifdef ETHERNET_PORT
	// initialize LAN
	// To use different network settings see the Ethernet library documentation:
	// https://www.arduino.cc/en/Reference/Ethernet
	Ethernet.begin(eth_mac, eth_ip);
	#endif

	// Reserved version command (it's recommended to leave this in
	// except if you really have to save flash/RAM).
	// It can also test the watchdog and perform a software reset.
	// Sending 0xffff to this command will cause the shutdownHook to initiate the shutdown
	// which will store current values in EEPROM and halt the system.
	arducom.addCommand(new ArducomVersionCommand("Logger", &shutdownHook));

	// EEPROM access commands
	// due to RAM constraints we have to expose the whole EEPROM as a block
	arducom.addCommand(new ArducomReadEEPROMBlock(9));
	arducom.addCommand(new ArducomWriteEEPROMBlock(10));

	// expose variables
	// due to RAM constraints we have to expose the whole variable RAM as one block
	arducom.addCommand(new ArducomReadBlock(20, &readings[0], VAR_TOTAL_SIZE));
	// the RAM block can be written (S0 initialization access)
	arducom.addCommand(new ArducomWriteBlock(30, &readings[0], VAR_TOTAL_SIZE));

	#ifdef USE_DS1307
	if (rtcOK) {
		// register RTC commands
		// Assuming I2C, on Linux you can display the current date using the following command:
		//  date -d @`./arducom -d /dev/i2c-1 -a 5 -c 21 -o Int32`
		// To set the current date and time, use:
		//  date +"%s" | ./arducom -d /dev/i2c-1 -a 5 -c 22 -i Int32 -r
		arducom.addCommand(new ArducomGetTime(21));
		arducom.addCommand(new ArducomSetTime(22));
	}
	#endif
	
	#ifdef SDCARD_CHIPSELECT
	if (sdCardOK) {
		log(F("Adding FTP commands"));
		// initialize FTP system (adds FTP commands)
		arducomFTP.init(&arducom, &sdFat);
	}
	#endif
	
	// **** S0 polling interrupt setup ****

#if defined S0_A_PIN || defined S0_B_PIN || defined S0_C_PIN || defined S0_D_PIN
	// configure interrupt (once per ms)
	
	// Credits: Adapted from http://popdevelop.com/2010/04/mastering-timer-interrupts-on-the-arduino/
	
	// disable timer overflow interrupt while configuring
	TIMSK2 &= ~(1<<TOIE2);

	// configure timer2 in normal mode (pure counting, no PWM etc.)
	TCCR2A &= ~((1<<WGM21) | (1<<WGM20));
	TCCR2B &= ~(1<<WGM22);

	// select clock source: internal I/O clock
	ASSR &= ~(1<<AS2);

	// disable Compare Match A interrupt enable (only want overflow)
	TIMSK2 &= ~(1<<OCIE2A);

	// configure the prescaler to CPU clock divided by 128
	TCCR2B |= (1<<CS22) | (1<<CS20);	// Set bits
	TCCR2B &= ~(1<<CS21); 				// Clear bit

	/* Calculate a proper value to load the timer counter.
	* The following loads the value 131 into the Timer 2 counter register
	* The math behind this is:
	* (CPU frequency) / (prescaler value) = 125000 Hz = 8us.
	* (desired period) / 8us = 125.
	* MAX(uint8) + 1 - 125 = 131;
	*/
	// save value globally for later reload in ISR
	tcnt2 = 131; 

	// load and enable the timer
	TCNT2 = tcnt2;
	TIMSK2 |= (1<<TOIE2);	
#endif

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

void loop() {
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
	#if defined DHT22_A_PIN || defined DHT22_B_PIN
	// poll interval reached?
	if (millis() - lastDHT22poll > DHT22_POLL_INTERVAL_MS) {
		// read sensor values
		#ifdef DHT22_A_PIN
		{
			int chk = DHT.read22(DHT22_A_PIN);
			if (chk == DHTLIB_OK) {
				*(int16_t*)&readings[DHT22_A_HUMID] = DHT.humidity;
				*(int16_t*)&readings[DHT22_A_TEMP] = DHT.temperature * 10.0;
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
				*(uint16_t*)&readings[DHT22_B_TEMP] = DHT.temperature * 10.0;
			} else {
				*(int16_t*)&readings[DHT22_B_HUMID] = DHT22_INVALID;
				*(int16_t*)&readings[DHT22_B_TEMP] = DHT22_INVALID;
			}
		}
		#endif

		lastDHT22poll = millis();
	}
	#endif
	
	// S0 impulse counters
	#ifdef S0_A_PIN
	{
		cli();	// disable interrupts
		uint8_t incr = s0AIncrement;
		s0AIncrement = 0;
		sei();	// enable interrupts
		
		// add increment to values
		*(uint64_t*)&readings[S0_A_VALUE] += incr;
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
			// Another common error is a date too far in the future. This can results from I2C contention
			// in multi-master setups. It is strongly recommended not to use multi-master I2C except if
			// it is guaranteed that all devices gracefully handle bus arbitration.
			// If we can't determine a current timestamp that looks valid after several attempts
			// we log to a "fallback file" that is called "/fallback.log".
			// Records ending up in this file probably do not have a valid timestamp, so data cannot be easily
			// correlated; however, the presence of such a file (and its size) can indicate problems
			// with the RTC which can then be further investigated.
			bool dateOK = false;

			#ifdef USE_DS1307
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
			#endif	// use RTC
			
			char filename[14];
			SdFile logFile;

			if (!dateOK) {
				// log to the fallback file
				strcpy(filename, "/fallback.log");
				DEBUG(println(F("RTC date implausible")));
			}
			#ifdef USE_DS1307
			else {
				// convert to local time
				now = DateTime(utcToLocal(nowUTC.unixtime()));
				sprintf(filename, "/%04d%02d%02d.log", now.year(), now.month(), now.day());
			}
			#endif

			// reset watchdog timer (file operations may be slow)
			wdt_reset();
			if (logFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
				// write timestamp in UTC
				#ifdef USE_DS1307
				logFile.print(nowUTC.unixtime());
				#endif
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
		
		// Periodically reset readings. This allows to detect sensor or communication failures.
		resetReadings();
	}
	
	wdt_reset();

	// shutdown requested or EEPROM transfer interval reached or shutdown command sent?
	if (
	#ifdef SHUTDOWN_BUTTON
		(digitalRead(SHUTDOWN_BUTTON) == LOW) ||
	#endif
		(millis() / 1000 - lastEEPROMWrite > EEPROM_INTERVAL_S)
		|| initiateShutdown) {
		
		// write S0 values to EEPROM
		eeprom_update_block((const void*)&readings[S0_A_VALUE], (void*)EEPROM_S0COUNTER_A, EEPROM_S0COUNTER_LEN);
		eeprom_update_block((const void*)&readings[S0_B_VALUE], (void*)EEPROM_S0COUNTER_B, EEPROM_S0COUNTER_LEN);
		eeprom_update_block((const void*)&readings[S0_C_VALUE], (void*)EEPROM_S0COUNTER_C, EEPROM_S0COUNTER_LEN);
		eeprom_update_block((const void*)&readings[S0_D_VALUE], (void*)EEPROM_S0COUNTER_D, EEPROM_S0COUNTER_LEN);
		
		lastEEPROMWrite = millis() / 1000;
		
		log(F("S0 values stored"));
	}

	// shutdown requested?
	if (
		#ifdef SHUTDOWN_BUTTON
		(digitalRead(SHUTDOWN_BUTTON) == LOW) || 
		#endif
		initiateShutdown) {
		log(F("Shutdown requested"));
		wdt_disable();
		
		// if an SD card is used, disable the SPI bus
		// because it may interfere with the LED pin
		#ifdef SDCARD_CHIPSELECT
		SPI.end();
		#endif

		// halt the system, blink the LED
		while (1) {
			#ifdef LED_PIN
			pinMode(LED_PIN, OUTPUT);
			digitalWrite(LED_PIN, HIGH);
			delay(500);
			digitalWrite(LED_PIN, LOW);
			delay(500);
			#endif
		}
	}
}
