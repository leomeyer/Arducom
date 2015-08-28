#ifndef __ARDUCOM_H
#define __ARDUCOM_H

#include <stdint.h>

// Arducom status codes that are used internally
#define ARDUCOM_OK						0
#define ARDUCOM_COMMAND_ERROR			1
#define ARDUCOM_COMMAND_ALREADY_EXISTS	2
#define ARDUCOM_COMMANDCODE_INVALID		3
#define ARDUCOM_TIMEOUT					4
#define ARDUCOM_ERROR					5
#define ARDUCOM_OVERFLOW				6

// Arducom error codes that are being sent back to the master
#define ARDUCOM_NO_DATA					128
#define ARDUCOM_COMMAND_UNKNOWN			129
#define ARDUCOM_TOO_MUCH_DATA			130
#define ARDUCOM_PARAMETER_MISMATCH		131
#define ARDUCOM_BUFFER_OVERRUN			132
#define ARDUCOM_FUNCTION_ERROR			133

#define ARDUCOM_ERROR_CODE				255

#define ARDUCOM_BUFFERSIZE	32
#if (ARDUCOM_BUFFERSIZE > 64)
#error "Maximum ARDUCOM_BUFFERSIZE is 64"
#endif

// Configuration bit flag constants
#define ARDUCOM_FLAG_ENABLEDEBUG		1

#ifdef ARDUINO

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
	volatile uint8_t data[ARDUCOM_BUFFERSIZE];
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
	
	/** Sends the specified error code to the master. */
	virtual int8_t sendError(uint8_t errorCode, uint8_t info);
	
	/** Performs regular housekeeping; called from the Arducom main class; returns -1 in case of errors. */
	virtual int8_t doWork(void) = 0;
};

class Arducom;

/** This class is the base class for command handlers. If the command code and the number of expected bytes
* matches its definition its handle() method is called. This method can inspect the supplied data and send
* data back to the master. The number of expected bytes is optional. If it is > -1 the Arducom system will
* wait for enough data to arrive until the command is handled. Too much data for the command counts as an error. */
class ArducomCommand {

public:
	uint8_t commandCode;
	int8_t expectedBytes;	// number of expected bytes
	
	// forms a linked list of supported commands
	ArducomCommand* next;
	
	/** Is called when the command code of this command has been received and the number of expected bytes match. 
	* This method should evaluate the data in the dataBuffer. Return data should be placed in the destBuffer,
	* up to a length of maxBufferSize. The length of the returned data should be placed in dataSize. 
	* The result data is sent back to the master if this method returns a code of 0 (ARDUCOM_OK). 
	* Any other return code is interpreted as an error. Additional error information can be returned in errorInfo.
	*/
	virtual int8_t handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) = 0;

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
};

/** This class is the main class that handles the communication. */
class Arducom {
public:
	const uint8_t VERSION = 1;
	
	Print* debug;

	/** Initializes the Arducom system with the specified transport.
	* Default receive timeout is one second. A complete command must be received
	* within this time window. Otherwise, the transport is being reset which will 
	* discard the incomplete command. */
	Arducom(ArducomTransport* transport, Print* debugPrint = 0, uint16_t receiveTimeout = 0);

	/** Adds the specified command to the internal list. When the command is
	* received from the master, its handle() method is executed. 
	* Returns ARDUCOM_OK if the command could be added. */
	uint8_t addCommand(ArducomCommand* command);
	
	/** This method must be regularly called within the main loop of your program.
	* It allows the Arducom class to accept and process commands. 
	* Returns ARDUCOM_OK if everything is ok, or an error code otherwise. */
	virtual uint8_t doWork(void);
	
	virtual void setFlags(uint8_t flags);
	
	virtual uint8_t getFlags(void);
	
protected:
	ArducomTransport* transport;
	
	// linked list of commands
	ArducomCommand* list;
	
	ArducomCommand* currentCommand;
	// performance optimization: store data size of last check
	int8_t lastDataSize;
	
	uint16_t receiveTimeout;
	long lastReceiveTime;
	
	Print* origDebug;
};

/** This class implements a test command with code 0 and one optional flag byte
*   returning the following info:
*	Byte 0: Arducom version number
*   Bytes 1 - 4: result of the millis() function, LSB first; roughly speaking, the uptime of the slave
*   Bytes 5 - n: character data (for example, the slave name)
*   Bit 0 of the flags enables/disables debug output (if an Arducom debugPrint has been specified).
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

#endif

#endif
