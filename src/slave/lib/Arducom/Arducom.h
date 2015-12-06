// Copyright (c) 2015 Leo Meyer, leo@leomeyer.de

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

#ifndef __ARDUCOM_H
#define __ARDUCOM_H

#include <stdint.h>

// Arducom status codes that are used internally
#define ARDUCOM_OK						0
#define ARDUCOM_COMMAND_HANDLED			1
#define ARDUCOM_COMMAND_ALREADY_EXISTS	2
#define ARDUCOM_COMMANDCODE_INVALID		3
#define ARDUCOM_TIMEOUT					4
#define ARDUCOM_ERROR					5
#define ARDUCOM_OVERFLOW				6
#define ARDUCOM_COMMAND_ERROR			7
#define ARDUCOM_GENERAL_ERROR			8
#define ARDUCOM_NO_COMMAND				9
#define ARDUCOM_INVALID_REPLY			10
#define ARDUCOM_INVALID_RESPONSE		11
#define ARDUCOM_PAYLOAD_TOO_LONG		12
#define ARDUCOM_TRANSPORT_ERROR			13

// Arducom error codes that are being sent back to the master
#define ARDUCOM_NO_DATA					128
#define ARDUCOM_COMMAND_UNKNOWN			129
#define ARDUCOM_TOO_MUCH_DATA			130
#define ARDUCOM_PARAMETER_MISMATCH		131
#define ARDUCOM_BUFFER_OVERRUN			132
#define ARDUCOM_CHECKSUM_ERROR			133
#define ARDUCOM_FUNCTION_ERROR			134

#define ARDUCOM_ERROR_CODE				255

#define ARDUCOM_BUFFERSIZE	32
#if (ARDUCOM_BUFFERSIZE > 64)
#error "Maximum ARDUCOM_BUFFERSIZE is 64"
#endif

// Configuration bit flag constants
#define ARDUCOM_FLAG_ENABLEDEBUG		0x01
#define ARDUCOM_FLAG_INFINITELOOP		0x40
#define ARDUCOM_FLAG_SOFTRESET			0x80

#ifdef ARDUINO

/******************************************************************************************	
* Arducom transport base class definition
******************************************************************************************/

/** This class encapsulates the transport mechanism for Arducom commands.
*   Messages from the master always start with a one-byte command code, optionally followed by
*   payload bytes whose number depends on the command. Command codes of 0 - 126 are allowed.
*   Messages that are being sent back consist of a two byte header plus an optional payload.
*   The first byte is the command code with its highest bit set. It signals the master that the
*   correct command has been processed. In this case the second byte's lower six bits specify
*   the length of the returned payload. The highest two bits can signal command specific codes.
*   If the first byte is 255 it signals an unsupported command or another error. In this case
*   the second byte is an error code and the third byte contains error specific information.
*/
class ArducomTransport {

public:
	enum Status {
		NO_DATA
		, TOO_MUCH_DATA
		, HAS_DATA
		, READY_TO_SEND
		, SENT
	};

	volatile Status status;
	
	// the buffer for data received and data to send
	uint8_t data[ARDUCOM_BUFFERSIZE];
	// number of valid bytes in the buffer
	volatile uint8_t size;

	ArducomTransport() {
		this->reset();
	};
	
	/** Resets the transport (clears input buffers). */
	virtual void reset(void);
	
	/** If valid data has been received, returns the number of valid bytes, else -1. */
	virtual int8_t hasData(void);
	
	/** Prepares the transport to send count bytes from the buffer; returns -1 in case of errors. */
	virtual int8_t send(uint8_t* buffer, uint8_t count) = 0;
	
	/** Performs regular housekeeping; called from the Arducom main class; returns -1 in case of errors. */
	virtual int8_t doWork(void) = 0;
};

class Arducom;

/** This class is the base class for command handlers. If the command code and the number of expected bytes
* matches its definition its handle() method is called. This method can inspect the supplied data and send
* data back to the master. The number of expected bytes is optional. If it is > -1 the Arducom system will
* let the class handle all received data. If a value is specified, too much data for a command counts as an error. */
class ArducomCommand {

protected:
	/** Initializes an ArducomCommand for a variable number of expected data bytes. */
	ArducomCommand(const uint8_t commandCode) {
		this->commandCode = commandCode;
		this->expectedBytes = -1;
		this->next = 0;
	};

	/** Initializes an ArducomCommand for a known number of expected data bytes. */
	ArducomCommand(const uint8_t commandCode, int8_t expectedBytes) {
		this->commandCode = commandCode;
		this->expectedBytes = expectedBytes;
		this->next = 0;
	};

	uint8_t commandCode;
	int8_t expectedBytes;	// number of expected bytes
	
	/** Is called when the command code of this command has been received and the number of expected bytes match. 
	* This method should evaluate the data in the dataBuffer. Return data should be placed in the destBuffer,
	* up to a length of maxBufferSize. The length of the returned data should be placed in dataSize. 
	* The result data is sent back to the master if this method returns a code of 0 (ARDUCOM_OK). 
	* Any other return code is interpreted as an error. Additional error information can be returned in errorInfo.
	*/
	virtual int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) = 0;

	// forms a linked list of supported commands (internal data structure)
	ArducomCommand* next;

friend class Arducom;	
};

/******************************************************************************************	
* Arducom main class definition
******************************************************************************************/

/** This class is the main class that handles the communication. */
class Arducom {
public:
	static const uint8_t VERSION = 1;
	
	/** The timezone offset maintained by this instance. It can be queried or set with the version command (0). */
	int16_t timezoneOffsetSeconds;
	
	/** Debug Print instance as supplied in the constructor. If this value is != 0 it means that debugging
	* is enabled for this Ardudom instance. */
	Print* debug;

	/** Initializes the Arducom system with the specified transport.
	* Default receive timeout is zero (wait forever). If specified, a complete command must be received
	* within this time window. Otherwise, the transport is being reset which will discard the incomplete command. */
	Arducom(ArducomTransport* transport, Print* debugPrint = 0, uint16_t receiveTimeout = 0);

	/** Adds the specified command to the internal list. When the command is
	* received from the master, its handle() method is executed. 
	* Returns ARDUCOM_OK if the command could be added. */
	uint8_t addCommand(ArducomCommand* command);
	
	/** This method must be regularly called within the main loop of your program.
	* It allows the Arducom class to accept and process commands. 
	* Returns ARDUCOM_OK if everything is ok, or an error code otherwise. */
	virtual uint8_t doWork(void);
	
	virtual void setFlags(uint8_t mask, uint8_t flags);
	
	virtual uint8_t getFlags(void);
	
protected:
	ArducomTransport* transport;
	
	// linked list of commands
	ArducomCommand* list;
	
	// performance optimization: store data size of last check
	int8_t lastDataSize;
	
	uint16_t receiveTimeout;
	long lastReceiveTime;
	
	// backup of Print instance for re-enabling debug
	Print* origDebug;
};

/******************************************************************************************	
* Arducom command definitions
******************************************************************************************/

/** This class implements a test command with code 0. It accepts the following optional parameter bytes:
*   Byte 0: flag mask
*   Byte 1: flags to set (only those flags are set whose bit in flag mask is 1)
*   Bit 0 of the flags enables/disables debug output (if an Arducom debugPrint has been specified).
*   Bit 6 of the flags causes an infinite loop (to test a watchdog).
*   Bit 7 of the flags causes a software restart (using the watchdog).
*   These two bytes have to be specified together.
*   It returns the following info:
*	Byte 0: Arducom version number
*   Bytes 1 - 4: result of the millis() function, LSB first; roughly speaking, the uptime of the slave
*   Byte 5: flag byte
*   Byte 6 - 7: current amount of free RAM
*   Bytes 8 - n: character data (for example, the slave name)
*/
class ArducomVersionCommand: public ArducomCommand {
public:
	/** Initialize the command with a null-terminated data string. */
	ArducomVersionCommand(const char* data) : ArducomCommand(0) {
		this->data = data;
	}
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
private:
	const char *data;
};

/***************************************
* Predefined EEPROM access commands
****************************************/

/** This class implements a command to write a byte value to a specified EEPROM address.
*   It expects a two-byte EEPROM address and the byte value to write. All values are LSB first.
*/
class ArducomWriteEEPROMByte: public ArducomCommand {
public:
	ArducomWriteEEPROMByte(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to read a byte value from a specified EEPROM address.
*   It expects a two-byte EEPROM address, LSB first.
*   It returns the following info:
*   Byte 0: result of the EEPROM read
*/
class ArducomReadEEPROMByte: public ArducomCommand {
public:
	ArducomReadEEPROMByte(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to write a two-byte integer value to a specified EEPROM address.
*   It expects a two-byte EEPROM address and the two-byte value to write. All values are LSB first.
*/
class ArducomWriteEEPROMInt16: public ArducomCommand {
public:
	ArducomWriteEEPROMInt16(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to read a two-byte integer value from a specified EEPROM address.
*   It expects a two-byte EEPROM address, LSB first.
*   It returns the following info:
*   Bytes 0 - 1: result of the EEPROM read (LSB first)
*/
class ArducomReadEEPROMInt16: public ArducomCommand {
public:
	ArducomReadEEPROMInt16(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to write a four-byte integer value to a specified EEPROM address.
*   It expects a two-byte EEPROM address and the four-byte value to write. All values are LSB first.
*/
class ArducomWriteEEPROMInt32: public ArducomCommand {
public:
	ArducomWriteEEPROMInt32(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to read a four-byte integer value from a specified EEPROM address.
*   It expects a two-byte EEPROM address, LSB first.
*   It returns the following info:
*   Bytes 0 - 3: result of the EEPROM read (LSB first)
*/
class ArducomReadEEPROMInt32: public ArducomCommand {
public:
	ArducomReadEEPROMInt32(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to write an eight-byte integer value to a specified EEPROM address.
*   It expects a two-byte EEPROM address and the eight-byte value to write. All values are LSB first.
*/
class ArducomWriteEEPROMInt64: public ArducomCommand {
public:
	ArducomWriteEEPROMInt64(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to read an eight-byte integer value from a specified EEPROM address.
*   It expects a two-byte EEPROM address, LSB first.
*   It returns the following info:
*   Bytes 0 - 7: result of the EEPROM read (LSB first)
*/
class ArducomReadEEPROMInt64: public ArducomCommand {
public:
	ArducomReadEEPROMInt64(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to write an arbitrary data block to a specified EEPROM address.
*   It expects a two-byte EEPROM address and the data to write. All values are LSB first.
*/
class ArducomWriteEEPROMBlock: public ArducomCommand {
public:
	ArducomWriteEEPROMBlock(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to read an arbitrary data block from a specified EEPROM address.
*   It expects a two-byte EEPROM address, LSB first, plus one byte containing the number of bytes to read.
*   It returns the following info:
*   Bytes 0 - n: the data read from the ERPROM
*/
class ArducomReadEEPROMBlock: public ArducomCommand {
public:
	ArducomReadEEPROMBlock(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/***************************************
* Predefined RAM access commands
****************************************/

/** This class implements a command to write a byte value to a specified RAM address.
*   The address must be specified at creation time.
*   It expects the byte value to write. All values are LSB first.
*/
class ArducomWriteByte: public ArducomCommand {
public:
	ArducomWriteByte(uint8_t commandCode, uint8_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	uint8_t* address;
};

/** This class implements a command to read a byte value from a specified RAM address.
*   The address must be specified at creation time.
*   It returns the following info:
*   Byte 0: result
*/
class ArducomReadByte: public ArducomCommand {
public:
	ArducomReadByte(uint8_t commandCode, uint8_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	uint8_t* address;
};

/** This class implements a command to write a two-byte integer value to a specified RAM address.
*   The address must be specified at creation time.
*   It expects the two-byte value to write. All values are LSB first.
*/
class ArducomWriteInt16: public ArducomCommand {
public:
	ArducomWriteInt16(uint8_t commandCode, int16_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	int16_t* address;
};

/** This class implements a command to read a two-byte integer value from a specified RAM address.
*   The address must be specified at creation time.
*   It returns the following info:
*   Bytes 0 - 1: result (LSB first)
*/
class ArducomReadInt16: public ArducomCommand {
public:
	ArducomReadInt16(uint8_t commandCode, int16_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	int16_t* address;
};

/** This class implements a command to write a four-byte integer value to a specified RAM address.
*   The address must be specified at creation time.
*   It expects the four-byte value to write. All values are LSB first.
*/
class ArducomWriteInt32: public ArducomCommand {
public:
	ArducomWriteInt32(uint8_t commandCode, int32_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	int32_t* address;
};

/** This class implements a command to read a four-byte integer value from a specified RAM address.
*   The address must be specified at creation time.
*   It returns the following info:
*   Bytes 0 - 3: result (LSB first)
*/
class ArducomReadInt32: public ArducomCommand {
public:
	ArducomReadInt32(uint8_t commandCode, int32_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	int32_t* address;
};

/** This class implements a command to write an eight-byte integer value to a specified RAM address.
*   The address must be specified at creation time.
*   It expects the eight-byte value to write. All values are LSB first.
*/
class ArducomWriteInt64: public ArducomCommand {
public:
	ArducomWriteInt64(uint8_t commandCode, int64_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	int64_t* address;
};

/** This class implements a command to read an eight-byte integer value from a specified EEPROM address.
*   The address must be specified at creation time.
*   It returns the following info:
*   Bytes 0 - 7: result (LSB first)
*/
class ArducomReadInt64: public ArducomCommand {
public:
	ArducomReadInt64(uint8_t commandCode, int64_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	int64_t* address;
};

/** This class implements a command to write an arbitrary data block to a specified RAM address.
*   The address must be specified at creation time.
*   It expects a two-byte offset and the data to write. All values are LSB first.
*/
class ArducomWriteBlock: public ArducomCommand {
public:
	ArducomWriteBlock(uint8_t commandCode, uint8_t* address, uint16_t maxBlockSize);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	uint8_t* address;
	uint16_t maxBlockSize;
};

/** This class implements a command to read an arbitrary data block from a specified RAM address.
*   The address must be specified at creation time.
*   It expects a two-byte offset, LSB first, plus one byte containing the number of bytes to read.
*   It returns the following info:
*   Bytes 0 - n: the data read from the block
*/
class ArducomReadBlock: public ArducomCommand {
public:
	ArducomReadBlock(uint8_t commandCode, uint8_t* address);
	
	int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
protected:
	uint8_t* address;
};

#endif

#endif