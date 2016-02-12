#ifndef __ARDUCOMMASTER_H
#define __ARDUCOMMASTER_H

#include <vector>
#include <stdexcept>
#include <inttypes.h>

class TimeoutException: public std::runtime_error {
public:
	TimeoutException(const char *what) : std::runtime_error(what) {}
};

// recursively print exception whats:
void print_what (const std::exception& e);

/** This class defines how an Arducom transport mechanism works.
 */
class ArducomMasterTransport {

public:
	/** Initializes the transport. Throws an exception in case of errors. */
	virtual void init(void) = 0;

	/** Sends the specified bytes over the transport. Throws an exception in case of errors. */
	virtual void send(uint8_t* buffer, uint8_t size, int retries = 0) = 0;

	/** Requests up to expectedBytes from the transport. Throws an exception in case of errors. */
	virtual void request(uint8_t expectedBytes) = 0;
	
	/** Is called when the transaction is complete. */
	virtual void done(void) = 0;
	
	/** Reads a byte from the transport. Throws an exception in case of errors. */
	virtual uint8_t readByte(void) = 0;

	/** Returns the maximum command size supported by this transport. */
	virtual size_t getMaximumCommandSize(void) = 0;
	
	/** Returns the default number of expected bytes for this transport. */
	virtual size_t getDefaultExpectedBytes(void) = 0;

	virtual void printBuffer(void) = 0;	
};

/** This class contains the functions to send and receive data over a transport.
 */
class ArducomMaster {

public:

	/** The code of the last error that occurred using this master. 
	* Codes lower than 128 are local. Codes greater than 127 come from the slave. */
	uint8_t lastError;

	ArducomMaster(ArducomMasterTransport *transport, bool verbose);
		
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
	
	/** Must be called when the transaction is complete. */
	virtual void done(void);
	
protected:
	ArducomMasterTransport *transport;
	bool verbose;
	uint8_t lastCommand;
	
	virtual void invalidResponse(uint8_t commandByte);
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
	long delayMs;
	int retries;
	bool useChecksum;
	
	/** Standard constructor. Applies the default values. */
	inline ArducomBaseParameters() {
		baudrate = 9600;
		deviceAddress = 0;
		verbose = false;
		delayMs = 0;
		retries = 0;
		useChecksum = true;
	}

	/** Helper function to convert char* array to vector */
	static inline void convertCmdLineArgs(int argc, char* argv[], std::vector<std::string>& args) {
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
	
	/** Display version information and exit. */
	virtual void showVersion(void) = 0;
	
	/** Display help text and exit. */
	virtual void showHelp(void) = 0;
};

#endif
