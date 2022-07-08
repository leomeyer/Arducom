// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#ifndef __ARDUCOMMASTER_H
#define __ARDUCOMMASTER_H

#include "../slave/lib/Arducom/Arducom.h"

#include <vector>
#include <stdexcept>
#include <inttypes.h>
#include <sstream>
#include <string>

#if defined(__CYGWIN__) || defined(_MSC_VER)
#define ARDUCOM_NO_I2C
#define ARDUCOM__NO_LOCK_MECHANISM 1
#endif

// Default slave reaction delay for processing and sending
// Only relevant for I2C transport (I2C data request fails immediately if there is not data).
#define ARDUCOM_DEFAULT_DELAY_MS		10

// The default timeout for I/O should not be less than 500 because with TCP/IP, if it is less,
// an error message for an unknown host would be "timeout" rather than "no route to host",
// which is slightly confusing. To avoid this confusion, set the default rather high.
// The constant is already defined in the slave library (Arducom.h).
#undef ARDUCOM_DEFAULT_TIMEOUT_MS
#define ARDUCOM_DEFAULT_TIMEOUT_MS		5000

#define ARDUCOM_TRANSPORT_DEFAULT_BAUDRATE		ARDUCOM_DEFAULT_BAUDRATE

// The init delay is only relevant for serial transports in case an Arduino is being reset
// by the serial driver on connection. This constant allows for some startup time.
#define ARDUCOM_DEFAULT_INIT_DELAY_MS	3000

// helper macros
#define ARDUCOM_Q(str)				#str
#define ARDUCOM_QUOTE(str)			ARDUCOM_Q(str)

namespace Arducom {

/* Input and output data formats */
enum Format {
	FMT_HEX,
	FMT_RAW,
	FMT_BIN,
	FMT_BYTE,
	FMT_INT16,
	FMT_INT32,
	FMT_INT64,
	FMT_FLOAT
};


Format parseFormat(const std::string& arg, const std::string& argName);

class TimeoutException: public std::runtime_error {
public:
	TimeoutException(const char* what) : std::runtime_error(what) {}
};

class FunctionError: public std::runtime_error {
public:
	FunctionError(const char* what) : std::runtime_error(what) {}
};

}
/** Helper function that throws an error message with system error information */
void throw_system_error(const char* what, const char* info = NULL, int code = 0);

/** Recursively prints exception whats */
void print_what(const std::exception& e, bool printEndl = true);

class ArducomBaseParameters;

/** This class defines how an Arducom transport mechanism works.
 */
class ArducomMasterTransport {

public:
	/** Destructor */
	virtual ~ArducomMasterTransport();

	/** Initializes the transport. Throws an exception in case of errors. */
	virtual void init(ArducomBaseParameters* parameters) = 0;

	/** Sends the specified bytes over the transport. Throws an exception in case of errors. */
	virtual void sendBytes(uint8_t* buffer, uint8_t size, int retries = 0) = 0;

	/** Requests up to expectedBytes from the transport. Throws an exception in case of errors. */
	virtual void request(uint8_t expectedBytes) = 0;

	/** Is called when the transport should be closed. */
	virtual void done(void) = 0;

	/** Reads a byte from the transport. Throws an exception in case of errors. */
	virtual uint8_t readByte(void) = 0;

	/** Returns the maximum command size supported by this transport. */
	virtual uint8_t getMaximumCommandSize(void) = 0;

	/** Returns the default number of expected bytes for this transport. */
	virtual uint8_t getDefaultExpectedBytes(void) = 0;

	/** For interprocess communication. Returns the semaphore key to use for this
	 * transport. If 0, no sempahore locking is to be used. */
	virtual int getSemkey(void) = 0;

	virtual void printBuffer(void) = 0;
};

/** This class encapsulates parameter validation and basic transport creation.
 * It is used by tools to reduce code duplication. Tools are expected to extend
 * this class by adding their specific requirements.
 */
class ArducomBaseParameters {

public:
	std::string transportType;
	std::string device;
	int baudrate;
	int deviceAddress;
	bool verbose;
	bool debug;
	long initDelayMs;
	bool initDelaySetManually;
	long delayMs;
	bool delaySetManually;
	long timeoutMs;
	int retries;
	bool useChecksum;
	int semkey;		// semaphore key; usually determined from transport but can be specified in case of conflict

	/** Standard constructor. Applies the default values. */
	ArducomBaseParameters() {
		baudrate = ARDUCOM_TRANSPORT_DEFAULT_BAUDRATE;
		deviceAddress = 0;
		verbose = false;
		debug = false;
		initDelayMs = 0;	// set by the transport (only really required for serial)
		initDelaySetManually = false;
		delayMs = ARDUCOM_DEFAULT_DELAY_MS;
		delaySetManually = false;
		timeoutMs = ARDUCOM_DEFAULT_TIMEOUT_MS;
		retries = 0;
		useChecksum = true;
		semkey = -1;
	}

	/** Helper function to convert char* array to vector */
	static void convertCmdLineArgs(int argc, char* argv[], std::vector<std::string>& args) {
		args.reserve(argc);
		for (int i = 0; i < argc; i++) {
			char* targ = argv[i];
			std::string arg(targ);
			args.push_back(arg);
		}
	}

	/** Iterates over the arguments and tries to evaluate them. */
	void setFromArguments(std::vector<std::string>& args);

	/** Tries to evaluate the argument at position i and sets the corresponding
	 * parameter(s) if successful. Throws an exception if the argument is not recognized. */
	virtual void evaluateArgument(std::vector<std::string>& args, size_t* i);

	/** Throws an exception if the parameters cannot be validated.
	 * Returns an initialized ArducomTransport object if everything is ok. */
	virtual ArducomMasterTransport* validate();

	/** Returns a string representation of the parameters. */
	virtual std::string toString(void);

	/** Display version information and exit. */
	virtual void showVersion(void);

	/** Display help text and exit. */
	virtual void showHelp(void);

protected:
	/** Returns the parameter help for this object. */
	virtual std::string getHelp(void);
};

/** This class contains the functions to send and receive data over a transport.
 */
class ArducomMaster {

public:

	/** The code of the last error that occurred using this master.
	* Codes lower than 128 are local. Codes greater than 127 come from the slave. */
	uint8_t lastError;

	/** Initialize the object with the given transport. The object takes ownership of the transport
	* and frees it when it is destroyed. */
	ArducomMaster(ArducomMasterTransport* transport);

	/** Destructor */
	virtual ~ArducomMaster();

	std::string getExceptionMessage(const std::exception& e);

	/** Prints the buffer content (as hex and RAW) to stdout. */
	static void printBuffer(uint8_t* buffer, uint8_t size, bool noHex = false, bool noRAW = false);

	/** Sends the specified command to the slave.
	* Expects the payload in buffer and the payload size in size.
	* The number of expected bytes must be specified.
	* Places up to the number of expected bytes in the destBuffer if expected is >= 0.
	* size is modified to contain the response size.
	* Throws an exception if an error occurred.
	* In case of an ARDUCOM_FUNCTION_ERROR, errorInfo contains the info byte as transferred from
	* the slave, if available (can be null if not required).
	* If the close flag is false the connection is kept open if the transport supports this.  */
	virtual void execute(ArducomBaseParameters& parameters, uint8_t command, uint8_t* buffer, uint8_t* size,
                      uint8_t expected, uint8_t* destBuffer, uint8_t *errorInfo, bool close = true);

    /** If the last command has been executed without closing, the communication must be closed by
    * invoking this method when done. This method is also called when destroying the object. */
    virtual void close(bool verbose);

protected:

	ArducomMasterTransport *transport;
	uint8_t lastCommand;

	// semaphore for mutually exclusive access
	int semkey;
	int semid;
	bool hasLock;

	/** If the transport specifies a semaphore key, attempts to acquire the semaphore. */
	virtual void lock(bool verbose, long timeoutMs);

	/** If the transport specifies a semaphore key, releases the semaphore. */
	virtual void unlock(bool verbose);

	/** Sends the specified command and the content of the buffer to the slave. */
	virtual void send(uint8_t command, bool checksum, uint8_t* buffer, uint8_t size, int retries, bool verbose);

	/** Places up to the number of expected bytes in the destBuffer if expected is >= 0.
	* size indicates the number of received payload bytes.
	* The return code 0 indicates success. Other values mean that an error occurred.
	* In these cases, errorInfo contains the info byte as transferred from
	* the slave, if available. May throw exceptions. */
	virtual uint8_t receive(uint8_t expected, bool useChecksum, uint8_t* destBuffer, uint8_t* size, uint8_t *errorInfo, bool verbose);

	/** Must be called when the transaction is complete. */
	virtual void done(bool verbose);

	/** Helper function. Throws an exception. */
	virtual void invalidResponse(uint8_t commandByte);
};

#endif
