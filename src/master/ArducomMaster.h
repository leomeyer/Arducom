#ifndef __ARDUCOMMASTER_H
#define __ARDUCOMMASTER_H

#include <inttypes.h>

#define ARDUCOM_GENERAL_ERROR		1
#define ARDUCOM_NO_COMMAND			2
#define ARDUCOM_INVALID_REPLY		3
#define ARDUCOM_INVALID_RESPONSE	4
#define ARDUCOM_PAYLOAD_TOO_LONG	5
#define ARDUCOM_TRANSPORT_ERROR		6


class ArducomMasterTransport {

public:
	/** Initializes the transport. Throws an exception in case of errors. */
	virtual void init(void) = 0;

	/** Sends the specified bytes over the transport. Throws an exception in case of errors. */
	virtual void send(uint8_t* buffer, uint8_t size, int retries = 0) = 0;

	/** Requests up to expectedBytes from the transport. Throws an exception in case of errors. */
	virtual void request(uint8_t expectedBytes) = 0;
	
	/** Reads a byte from the transport. Throws an exception in case of errors. */
	virtual uint8_t readByte(void) = 0;

	/** Returns the maximum command size supported by this transport. */
	virtual size_t getMaximumCommandSize(void) = 0;
	
	/** Returns the default number of expected bytes for this transport. */
	virtual size_t getDefaultExpectedBytes(void) = 0;

	virtual void printBuffer(void) = 0;	
};


class ArducomMaster {

public:

	/** The code of the last error that occurred using this master. 
	* Codes lower than 128 are local. Codes greater than 127 come from the slave. */
	uint8_t lastError;

	ArducomMaster(ArducomMasterTransport *transport, bool verbose) {
		this->transport = transport;
		this->verbose = verbose;
		this->lastCommand = 255;	// set to invalid command
		this->lastError = 0;
	};
	
	/** Prints the buffer content (as hex and RAW) to stdout. */
	static void printBuffer(uint8_t* buffer, uint8_t size, bool noHex = false, bool noRAW = false);

	/** Sends the specified command and the content of the buffer to the slave. */
	virtual void send(uint8_t command, bool checksum, uint8_t* buffer, uint8_t size, int retries = 0);
	
	/** Places up to the number of expected bytes in the destBuffer if expected is >= 0.
	* size indicates the number of received payload bytes. 
	* The return code 0 indicates success. Other values mean that an error occurred. 
	* In these cases, errorInfo contains the info byte as transferred from
	* the slave, if available. May throw exceptions. */
	virtual uint8_t receive(uint8_t expected, uint8_t* destBuffer, uint8_t* size, uint8_t *errorInfo);
	
protected:
	ArducomMasterTransport *transport;
	bool verbose;
	uint8_t lastCommand;
	
	virtual void invalidResponse(uint8_t commandByte);
};

#endif
