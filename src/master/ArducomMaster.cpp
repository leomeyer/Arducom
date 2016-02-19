#include <exception>
#include <stdexcept>
#include <iostream>
#include <unistd.h>

#include "../slave/lib/Arducom/Arducom.h"
#include "ArducomMaster.h"
#include "ArducomMasterI2C.h"
#include "ArducomMasterSerial.h"

// recursively print exception whats:
void print_what (const std::exception& e) {
	std::cerr << e.what();
	try {
		std::rethrow_if_nested(e);
	} catch (const std::exception& nested) {
		std::cerr << ": ";
		print_what(nested);
	}
	std::cerr << std::endl;
}

static uint8_t calculateChecksum(uint8_t commandByte, uint8_t code, uint8_t* data, uint8_t dataSize) {
	int16_t sum = commandByte + code;
	// carry overflow?
	if (sum > 255) {
		sum = (sum & 0xFF) + 1;
	}
	for (uint8_t i = 0; i < dataSize; i++) {
		sum += data[i];
		if (sum > 255) {
			sum = (sum & 0xFF) + 1;
		}
	}
	// return two's complement of result
	return ~(uint8_t)sum;
}

ArducomMaster::ArducomMaster(ArducomMasterTransport* transport, bool verbose) {
	this->transport = transport;
	this->verbose = verbose;
	this->lastCommand = 255;	// set to invalid command
	this->lastError = 0;
}

void ArducomMaster::printBuffer(uint8_t* buffer, uint8_t size, bool noHex, bool noRAW) {
	if (size == 0)
		return;
	if (!noHex) {
		char outBuf[4];
		for (uint8_t j = 0; j < size; j++) {
			sprintf(outBuf, "%02X", buffer[j]);
			std::cout << outBuf;
		}
	}
	if (!noHex && !noRAW)
		std::cout << ' ';
	if (!noRAW) {
		for (uint8_t j = 0; j < size; j++) {
			// if no hex, output as raw
			if (noHex || ((buffer[j] >= ' ') && (buffer[j] <= 127)))
				std::cout << buffer[j];
			else
				std::cout << '.';
		}
	}
}

void ArducomMaster::execute(ArducomBaseParameters& parameters, uint8_t command, uint8_t* buffer, uint8_t* size, uint8_t expected, uint8_t* destBuffer, uint8_t *errorInfo) {
	
	// Sends the command to the slave and returns the response if possible.
	// Throws exceptions in case of errors. Exceptions that are handled by the standard
	// always set errorInfo to 0 (if it is not null). For errors of type ARDUCOM_FUNCTION_ERROR
	// errorInfo contains the error code as supplied by the device. This gives the caller
	// the ability to react to device specific function errors.
	
	try {
		// send the command and payload to the slave
		// The command is sent only once. If the caller requires the command to be re-sent in case
		// of failure, it should handle this case by itself.
		send(command, parameters.useChecksum, buffer, *size);

		// receive response
		uint8_t errInfo;
		int retries = parameters.retries;

		// retry loop
		// The logic tries to retrieve the response several times in case there is 
		// a delay of execution on the slave.
		while (retries >= 0) {
			// errorInfo may be null if the caller is not interested in error details
			if (errorInfo != nullptr)
				*errorInfo = 0;

			// wait for the specified delay
			usleep(parameters.delayMs * 1000);
			size = 0;

			// try to retrieve the result
			uint8_t result = receive(expected, buffer, size, &errInfo);

			// no error?
			if (result == ARDUCOM_OK) {
				break;
			}

			// special case: if NO_DATA has been received, give the slave more time to react
			if ((result == ARDUCOM_NO_DATA) && (retries >= 0)) {
				retries--;
				if (parameters.verbose) {
					std::cout << "Retrying to receive data, " << retries << " retries left" << std::endl;
				}
				continue;
			}
			
			// retries exceeded or another error occurred

			// convert result code to string
			char resultStr[21];
			sprintf(resultStr, "%d", result);

			// convert info code to string
			char errInfoStr[21];
			sprintf(errInfoStr, "%d", errInfo);

			switch (result) {
			case ARDUCOM_NO_DATA: {
				throw std::runtime_error((std::string("Device error ") + resultStr + ": No data (not enough data sent or command not yet processed, try to increase delay -l or number of retries -x)").c_str());
			}
			case ARDUCOM_COMMAND_UNKNOWN:
				throw std::runtime_error((std::string("Command unknown (") + resultStr + "): " + errInfoStr).c_str());
			case ARDUCOM_TOO_MUCH_DATA:
				throw std::runtime_error((std::string("Too much data (") + resultStr + "); expected bytes: " + errInfoStr).c_str());
			case ARDUCOM_PARAMETER_MISMATCH: {
				// sporadic I2C dropouts cause this error (receiver problems?)
				// seem to be unrelated to baud rate...
				// can be retried
				retries--;
				std::string msg(std::string("Parameter mismatch (") + resultStr + "); expected bytes: " + errInfoStr);
				if (parameters.verbose) {
					std::cout << msg << "; retrying to receive data, " << retries << " retries left" << std::endl;
				}
				if (retries < 0)
					throw std::runtime_error(msg.c_str());
				else
					// try again
					continue;
			}
			case ARDUCOM_BUFFER_OVERRUN:
				throw std::runtime_error((std::string("Buffer overrun (") + resultStr + "); buffer size is: " + errInfoStr).c_str());
			case ARDUCOM_CHECKSUM_ERROR: {
				// transportation error
				// can be retried
				retries--;
				std::string msg(std::string("Checksum error (") + resultStr + "); calculated checksum: " + errInfoStr);
				if (parameters.verbose) {
					std::cout << msg << "; retrying to receive data, " << retries << " retries left" << std::endl;
				}
				if (retries < 0)
					throw std::runtime_error(msg.c_str());
				else
					// try again
					continue;
			}
			case ARDUCOM_FUNCTION_ERROR:
				// set errorInfo and throw an exception to signal the caller that a function error occurred
				if (errorInfo != nullptr)
					*errorInfo = errInfo;
				throw std::runtime_error((std::string("Function error ") + resultStr + ": info code: " + errInfoStr).c_str());
			}
			// handle unknown errors
			throw std::runtime_error((std::string("Device error ") + resultStr + "; info code: " + errInfoStr).c_str());
		}
	} catch (const std::exception& e) {
		// cleanup after the transaction
		done();
		char commandStr[21];
		sprintf(commandStr, "%d", command);
		std::throw_with_nested(std::runtime_error((std::string("Error sending command ") + commandStr).c_str()));
	}

	// cleanup after the transaction
	done();
}

void ArducomMaster::send(uint8_t command, bool checksum, uint8_t* buffer, uint8_t size, int retries) {
	this->lastError = ARDUCOM_OK;

	uint8_t data[size + (checksum ? 3 : 2)];
	data[0] = command;
	data[1] = size | (checksum ? 0x80 : 0);
	for (uint8_t i = 0; i < size; i++) {
		data[i + (checksum ? 3 : 2)] = buffer[i];
	}
	if (checksum)
		data[2] = calculateChecksum(data[0], data[1], &data[3], size);
	if (this->verbose) {
		std::cout << "Sending bytes: " ;
		this->printBuffer(data, size + (checksum ? 3 : 2));
		std::cout << std::endl;
	}
	try {
		this->transport->send(data, size + (checksum ? 3 : 2), retries);
	} catch (const std::exception &e) {
		this->lastError = ARDUCOM_TRANSPORT_ERROR;
		std::throw_with_nested(std::runtime_error("Error sending data"));
	}
	this->lastCommand = command;
}

uint8_t ArducomMaster::receive(uint8_t expected, uint8_t* destBuffer, uint8_t* size, uint8_t *errorInfo) {
	this->lastError = ARDUCOM_OK;

	if (this->lastCommand > 127) {
		this->lastError = ARDUCOM_NO_COMMAND;
		throw std::runtime_error("Cannot receive without sending a command first");
	}

	try {
		this->transport->request(expected);
	} catch (const TimeoutException &te) {
		this->lastError = ARDUCOM_TIMEOUT;
		return ARDUCOM_NO_DATA;
	} catch (const std::exception &e) {
		this->lastError = ARDUCOM_TRANSPORT_ERROR;
		std::throw_with_nested(std::runtime_error("Error requesting data"));
	}

	if (verbose) {
		std::cout << "Receive buffer: ";
		this->transport->printBuffer();
		std::cout << std::endl;
	}

	// read first byte of the reply
	uint8_t resultCode;
	try	{
		resultCode = this->transport->readByte();
	} catch (const std::exception &e) {
		this->lastError = ARDUCOM_TRANSPORT_ERROR;
		std::throw_with_nested(std::runtime_error("Error reading data"));
	}

	// error?
	if (resultCode == ARDUCOM_ERROR_CODE) {
		if (this->verbose)
			std::cout << "Received error code 0xff" << std::endl;
		try	{
			resultCode = this->transport->readByte();
		} catch (const std::exception &e) {
			this->lastError = ARDUCOM_TRANSPORT_ERROR;
			std::throw_with_nested(std::runtime_error("Error reading data"));
		}
		if (this->verbose) {
			std::cout << "Error: ";
			this->printBuffer(&resultCode, 1);
		}
		try	{
			*errorInfo = this->transport->readByte();
		} catch (const std::exception &e) {
			this->lastError = ARDUCOM_TRANSPORT_ERROR;
			std::throw_with_nested(std::runtime_error("Error reading data"));
		}
		if (this->verbose) {
			std::cout << ", additional info: ";
			this->printBuffer(errorInfo, 1);
			std::cout << std::endl;
		}
		this->lastError = resultCode;
		return resultCode;
	} else
	if (resultCode == 0) {
		this->lastError = ARDUCOM_INVALID_REPLY;
		throw std::runtime_error("Communication error: Didn't receive a valid reply");
	}

	// device reacted to different command (result command code has highest bit set)?
	if (resultCode != (this->lastCommand | 0x80)) {
		this->lastError = ARDUCOM_INVALID_RESPONSE;
		this->invalidResponse(resultCode & ~0x80);
	}

	if (this->verbose) {
		std::cout << "Response command code is ok." << std::endl;
	}

	// read code byte
	uint8_t code;
	try	{
		code = this->transport->readByte();
	} catch (const std::exception &e) {
		this->lastError = ARDUCOM_TRANSPORT_ERROR;
		std::throw_with_nested(std::runtime_error("Error reading data"));
	}

	uint8_t length = (code & 0b00111111);
	bool checksum = (code & 0x80) == 0x80;
	if (this->verbose) {
		std::cout << "Code byte: ";
		this->printBuffer(&code, 1);
		std::cout << " Payload length is " << (int)length << " bytes.";
		if (checksum)
			std::cout << " Verifying data using checksum.";
		std::cout << std::endl;
	}
	if (length > ARDUCOM_BUFFERSIZE) {
		this->lastError = ARDUCOM_PAYLOAD_TOO_LONG;
		throw std::runtime_error("Protocol error: Returned payload length exceeds maximum buffer size");
	}

	// checksum expected?
	uint8_t checkbyte = 0;
	if (checksum)
		try	{
			checkbyte = this->transport->readByte();
		} catch (const std::exception &e) {
			this->lastError = ARDUCOM_TRANSPORT_ERROR;
			std::throw_with_nested(std::runtime_error("Error reading data"));
		}

	*size = 0;
	// read payload into the buffer; up to expected bytes or returned bytes, whatever is lower
	for (uint8_t i = 0; (i < expected) && (i < length); i++) {
		try {
			destBuffer[i] = this->transport->readByte();
		} catch (const std::exception &e) {
			this->lastError = ARDUCOM_TRANSPORT_ERROR;
			std::throw_with_nested(std::runtime_error("Error reading data"));
		}
		*size = i + 1;
	}
	if (this->verbose) {
		std::cout << "Received payload: ";
		this->printBuffer(destBuffer, *size);
		std::cout << std::endl;
	}
	if (checksum) {
		uint8_t ckbyte = calculateChecksum(resultCode, code, destBuffer, *size);
		if (ckbyte != checkbyte) {
			*errorInfo = ckbyte;
			this->lastError = ARDUCOM_CHECKSUM_ERROR;
			return ARDUCOM_CHECKSUM_ERROR;
		}
	}
	return ARDUCOM_OK;
}

void ArducomMaster::done() {
	this->transport->done();
}

void ArducomMaster::invalidResponse(uint8_t commandByte) {
	uint8_t expectedReply = this->lastCommand | 0x80;
	std::cout << "Expected reply to command ";
	this->printBuffer(&this->lastCommand, 1);
	std::cout << " (";
	this->printBuffer(&expectedReply, 1);
	std::cout << ") but received ";
	this->printBuffer(&commandByte, 1);
	std::cout << std::endl;
	throw std::runtime_error("Invalid response");
}

/** ArducomBaseParameters implementation */

void ArducomBaseParameters::setFromArguments(std::vector<std::string>& args) {
	// evaluate arguments
	for (size_t i = 1; i < args.size(); i++) {
		evaluateArgument(args,  &i);
	}
}

void ArducomBaseParameters::evaluateArgument(std::vector<std::string>& args, size_t* i) {
	if (args.at(*i) == "-h" || args.at(*i) == "-?") {
		this->showHelp();
		exit(0);
	} else
	if (args.at(*i) == "--version") {
		this->showVersion();
		exit(0);
	} else
	if (args.at(*i) == "-v") {
		this->verbose = true;
	} else
	if (args.at(*i) == "-n") {
		useChecksum = false;
	} else
	if (args.at(*i) == "-t") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected transport type after argument -t");
		} else {
			transportType = args.at(*i);
		}
	} else
	if (args.at(*i) == "-d") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected device name or IP address after argument -d");
		} else {
			device = args.at(*i);
		}
	} else
	if (args.at(*i) == "-a") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected address or port number after argument -a");
		} else {
			try {
				deviceAddress = std::stoi(args.at(*i));
			} catch (std::exception& e) {
				throw std::invalid_argument("Expected numeric address or port after argument -a");
			}
		}
	} else
	if (args.at(*i) == "-b") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected baud rate after argument -b");
		} else {
			try {
				baudrate = std::stoi(args.at(*i));
			} catch (std::exception& e) {
				throw std::invalid_argument("Expected numeric baudrate after argument -b");
			}
		}
	} else
	if (args.at(*i) == "-l") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected delay in ms after argument -l");
		} else {
			try {
				delayMs = std::stol(args.at(*i));
			} catch (std::exception& e) {
				throw std::invalid_argument("Expected numeric delay in ms after argument -l");
			}
		}
	} else
	if (args.at(*i) == "-x") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected number of retries after argument -x");
		} else {
			try {
				retries = std::stoi(args.at(*i));
			} catch (std::exception& e) {
				throw std::invalid_argument("Expected number after argument -x");
			}
		}
	} else
		throw std::invalid_argument("Unknown argument: " + args.at(*i));
}

ArducomMasterTransport* ArducomBaseParameters::validate() {
	if (delayMs < 0)
		throw std::invalid_argument("Delay must not be negative (argument -l)");

	if (retries < 0)
		throw std::invalid_argument("Number of retries must not be negative (argument -x)");

	ArducomMasterTransport *transport;

	if (transportType == "i2c") {
		if (device == "")
			throw std::invalid_argument("Expected I2C transport device file name (argument -d)");

		if ((deviceAddress < 1) || (deviceAddress > 127))
			throw std::invalid_argument("Expected I2C slave device address within range 1..127 (argument -a)");

		transport = new ArducomMasterTransportI2C(device, deviceAddress);
		try {
			transport->init();
		} catch (const std::exception& e) {
			std::throw_with_nested(std::runtime_error("Error initializing transport"));
		}
	} else
	if (transportType == "serial") {
		if (device == "")
			throw std::invalid_argument("Expected serial transport device file name (argument -d)");

		transport = new ArducomMasterTransportSerial(device, baudrate, 1000);
		try {
			transport->init();
		} catch (const std::exception& e) {
			std::throw_with_nested(std::runtime_error("Error initializing transport"));
		}
	} else
		throw std::invalid_argument("Transport type not supplied or unsupported (argument -t), use 'i2c' or 'serial'");

	return transport;
}
