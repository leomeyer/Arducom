// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#include "ArducomMaster.h"

#include <errno.h>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <iostream>
#ifndef WIN32
#include <unistd.h>
#include <arpa/inet.h>
#else
#include <Ws2tcpip.h>
#endif
#include <sstream>

#include "../slave/lib/Arducom/Arducom.h"
#ifndef WIN32
#include "ArducomMasterI2C.h"
#endif
#include "ArducomMasterSerial.h"
#ifndef ARDUCOM_DISABLE_TCPIP
#include "ArducomMasterTCPIP.h"
#endif

#ifdef WIN32
//Returns the last Win32 error, in string format. Returns an empty string if there is no error.
// https://stackoverflow.com/a/17387176
std::string GetLastErrorAsString(int errorCode = 0) {
	// Get the error message, if any.
	if (errorCode == 0)
		errorCode = ::GetLastError();
	if (errorCode == 0)
		errorCode = errno;
	if (errorCode == 0)
		return std::string("Unknown error"); // No error message has been recorded

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	// Free the buffer.
	LocalFree(messageBuffer);

	return message;
}
#endif

void throw_system_error(const char* what, const char* info, int code) {
	std::stringstream fullWhatSS;
	fullWhatSS << what << ": " << (info != NULL ? info : "") << (info != NULL ? ": " : "");
	// special treatment for certain errors
#ifdef WIN32
		fullWhatSS << GetLastErrorAsString(code);
#else
	if (errno == EINPROGRESS)
		fullWhatSS << "The operation timed out";
	else
		fullWhatSS << strerror(errno);
#endif
	std::string fullWhat = fullWhatSS.str();
	throw std::runtime_error(fullWhat.c_str());
}

std::string get_what(const std::exception& e) {
	std::stringstream ss;
	ss << e.what();
	try {
		std::rethrow_if_nested(e);
	} catch (const std::exception& nested) {
		ss << ": ";
		ss << get_what(nested);
	}
	return ss.str();
}

void print_what(const std::exception& e, bool printEndl) {
	std::cerr << get_what(e);
	if (printEndl)
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

/** ArducomMasterTransport implementation */

ArducomMasterTransport::~ArducomMasterTransport() {}

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
	if (args.at(*i) == "-vv") {
		this->verbose = true;
		this->debug = true;
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
			} catch (std::exception&) {
				throw std::invalid_argument("Expected numeric address or port after argument -a");
			}
		}
	} else
	if (args.at(*i) == "-u") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected timeout value in milliseconds after argument -u");
		} else {
			try {
				timeoutMs = std::stoi(args.at(*i));
			} catch (std::exception&) {
				throw std::invalid_argument("Expected numeric timeout value in milliseconds after argument -u");
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
			} catch (std::exception&) {
				throw std::invalid_argument("Expected numeric baudrate after argument -b");
			}
		}
	} else
	if (args.at(*i) == "--initDelay") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected initialization delay in milliseconds after argument --initDelay");
		} else {
			try {
				initDelayMs = std::stol(args.at(*i));
				initDelaySetManually = true;
			} catch (std::exception&) {
				throw std::invalid_argument("Expected numeric initialization delay in milliseconds after argument --initDelay");
			}
		}
	} else
	if (args.at(*i) == "-l") {
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected delay in milliseconds after argument -l");
		} else {
			try {
				delayMs = std::stol(args.at(*i));
				delaySetManually = true;
			} catch (std::exception&) {
				throw std::invalid_argument("Expected numeric delay in milliseconds after argument -l");
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
			} catch (std::exception&) {
				throw std::invalid_argument("Expected number after argument -x");
			}
		}
	} else
	if (args.at(*i) == "-k") {
		#if defined(__CYGWIN__) || defined(WIN32)
		throw std::invalid_argument("Sorry, System V semaphore locking is not supported under Windows");
		#else
		(*i)++;
		if (args.size() == *i) {
			throw std::invalid_argument("Expected semaphore key (integer) after argument -k");
		} else {
			try {
				semkey = std::stoi(args.at(*i));
			} catch (std::exception& e) {
				throw std::invalid_argument("Expected integer number after argument -k");
			}
		}
		#endif
	} else
		throw std::invalid_argument("Unknown argument: " + args.at(*i));
}

ArducomMasterTransport* ArducomBaseParameters::validate() {
	if (delayMs < 0)
		throw std::invalid_argument("Delay must not be negative (argument -l)");

	if (initDelayMs < 0)
		throw std::invalid_argument("Init delay must not be negative (argument --initDelay)");

	if (retries < 0)
		throw std::invalid_argument("Number of retries must not be negative (argument -x)");

	if ((transportType == "") && (device != "")) {
		// try to figure out transport type from device name
		if ((device.find("/dev/tty") == 0)
			|| (device.find("/dev/rfcomm") == 0)
			|| (device.find("COM") == 0) || (device.find("\\\\.\\COM") == 0)) {
			transportType = "serial";
		} else
		if (device.find("/dev/i2c") == 0) {
			transportType = "i2c";
		} else {
#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
			char buffer[4];
			if (InetPtonA(AF_INET, device.c_str(), &buffer) == 1)
				transportType = "tcpip";
#elif not defined(__MINGW32__)
			// check whether the device represents a valid IP address
			struct sockaddr_in sa;
			if (inet_pton(AF_INET, device.c_str(), &(sa.sin_addr)) == 1)
				transportType = "tcpip";
#endif
		}
	}

	ArducomMasterTransport* transport;

	if (transportType == "i2c") {
		#if defined(__CYGWIN__) || defined(WIN32)
		throw std::invalid_argument("Sorry, the I2C transport is not supported under Windows");
		#else
		if (device == "")
			throw std::invalid_argument("Expected I2C transport device file name (argument -d)");

		if ((deviceAddress < 1) || (deviceAddress > 127))
			throw std::invalid_argument("Expected I2C slave device address within range 1..127 (argument -a)");

		transport = new ArducomMasterTransportI2C();
		#endif
	} else
	if (transportType == "serial") {
		if (device == "")
			throw std::invalid_argument("Expected serial transport device file name (argument -d)");

		transport = new ArducomMasterTransportSerial();
	} else
	if (transportType == "tcpip") {
#ifdef ARDUCOM_DISABLE_TCPIP
			throw std::invalid_argument("Sorry, TCP/IP is not supported in this build");
#else
		if (device == "")
			throw std::invalid_argument("Expected TCP/IP host name or IP (argument -d)");

		if ((deviceAddress < 0) || (deviceAddress > 65535))
			throw std::invalid_argument("TCP/IP port number must be within 0 (default) and 65535");

		if (deviceAddress == 0)
			deviceAddress = ARDUCOM_TCP_DEFAULT_PORT;

		transport = new ArducomMasterTransportTCPIP();
#endif // ARDUCOM_DISABLE_TCPIP
	} else
	if (transportType != "")
		throw std::invalid_argument("Transport type unsupported (argument -t), use 'i2c', 'serial', or 'tcpip'");
	else {
		if (device != "")
			throw std::invalid_argument("Transport type could not be determined, use 'i2c', 'serial', or 'tcpip' (argument -t)");
		else
			throw std::invalid_argument("Expected a device name (argument -d)");
	}

	try {
		transport->init(this);
	} catch (const std::exception&) {
		std::throw_with_nested(std::runtime_error("Error initializing transport"));
	}

	return transport;
}

std::string ArducomBaseParameters::toString() {

	std::stringstream result;
	result << "Transport: ";
	result << this->transportType;
	result << "; ";
	result << "Device/Host: ";
	result << this->device;
	result << "; ";
	result << "Address/Port: ";
	result << this->deviceAddress;
	result << "; ";
	result << "Baud rate: ";
	result << this->baudrate;
	result << "; ";
	result << "Timeout: ";
	result << this->timeoutMs;
	result << " ms; ";
	result << "Init delay: ";
	result << this->initDelayMs;
	result << " ms; ";
	result << "Retries: ";
	result << this->retries;
	result << "; ";
	result << "Command delay: ";
	result << this->delayMs;
	result << " ms; ";
	result << "Use checksum: ";
	result << (this->useChecksum ? "yes" : "no");

	return result.str();
}

std::string ArducomBaseParameters::getHelp() {
	std::string result;
	result.append("Arducom base parameters:\n");
	result.append("  --version: Display version information and exit.\n");
	result.append("  -h or -?: Display help and exit.\n");
	result.append("  -v: Verbose mode.\n");
	result.append("  -vv: Extra verbose mode.\n");
	result.append("  -d <device>: Specifies the target device. Required.\n");
	result.append("    For serial, the name of a serial device.\n");
	result.append("    For I2C, the name of an I2C bus device.\n");
	result.append("    For TCP/IP, a host name or IP address.\n");
	result.append("  -t <transport>: Specifies the transport type.\n");
	result.append("    One of 'serial', 'i2c', or 'tcpip'.\n");
	result.append("    Only required if it can't be guessed from the device.\n");
	result.append("  -a <address>: Specifies the device address.\n");
	result.append("    For I2C, the slave address number (2 - 127). Required for I2C.\n");
	result.append("    For TCP/IP, the destination port number. Optional; default: " QUOTE(ARDUCOM_TCP_DEFAULT_PORT) ".\n");
	result.append("    Not used for serial transport.\n");
	result.append("  -b <baudrate>: Specifies the baud rate (serial only). Default: " QUOTE(DEFAULT_BAUDRATE) ".\n");
	result.append("  -n: Do not use checksums. Not recommended.\n");
	result.append("  --initDelay <value>: Delay in milliseconds after transport init.\n");
	result.append("    Only relevant for serial transport (e. g. for Arduino resets).\n");
	result.append("    Default: " QUOTE(DEFAULT_INIT_DELAY_MS) ".\n");
	result.append("  -u <value>: Timeout in milliseconds. Optional; default: " QUOTE(DEFAULT_TIMEOUT_MS) ".\n");
	result.append("  -l <value>: Delay in milliseconds between send and receive.\n");
	result.append("    Optional; default: " QUOTE(DEFAULT_DELAY_MS) ". Gives the device time to process.\n");
	result.append("  -x <value>: Number of retries should sending or retrieving fail.\n");
	result.append("    Optional; default: 0. A sensible value would be about 3.\n");
	#ifndef	__CYGWIN__
	result.append("  -k <value>: The semaphore key used to synchronize between different\n");
	result.append("    processes. A value of 0 disables semaphore synchronization.\n");
	#endif

	return result;
}


void ArducomBaseParameters::showVersion(void) {}

void ArducomBaseParameters::showHelp(void) {
	std::cout << getHelp();
}

/** ArducomMaster implementation */

ArducomMaster::ArducomMaster(ArducomMasterTransport* transport) {
	this->transport = transport;
	this->lastCommand = 255;	// set to invalid command
	this->lastError = 0;
	this->semkey = 0;
	this->semid = 0;
	this->hasLock = false;
}

ArducomMaster::~ArducomMaster() {
    close(false);
	// free the transport object if present
	if (transport != nullptr)
		delete transport;
}

std::string ArducomMaster::getExceptionMessage(const std::exception& e) {
    return get_what(e);
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

void ArducomMaster::execute(ArducomBaseParameters& parameters, uint8_t command, uint8_t* buffer, uint8_t* size,
                            uint8_t expected, uint8_t* destBuffer, uint8_t *errorInfo, bool close) {

	// Sends the command to the slave and returns the response if possible.
	// Throws exceptions in case of errors. Exceptions that are handled by the standard
	// always set errorInfo to 0 (if it is not null). For errors of type ARDUCOM_FUNCTION_ERROR
	// errorInfo contains the error code as supplied by the device. This gives the caller
	// the ability to react to device specific function errors.

	if (parameters.debug)
		std::cout << parameters.toString() << std::endl;

	// determine the semaphore key to use
	// If the parameters specify a value < 0 (default), use the transport's semaphore key.
	// A value of 0 disables the semaphore mechanism.
	#if defined(__CYGWIN__) || defined(WIN32)
	// disable semaphores under Windows (not supported)
	this->semkey = 0;
	#else
	if (parameters.semkey < 0)
		this->semkey = transport->getSemkey();
	else
		this->semkey = parameters.semkey;
	#endif

	try {
		this->lock(parameters.debug, parameters.timeoutMs);

		// send the command and payload to the slave
		// The command is sent only once. If the caller requires the command to be re-sent in case
		// of failure, it should handle this case by itself.
		send(command, parameters.useChecksum, buffer, *size, parameters.retries, parameters.verbose);

		// receive response
		uint8_t errInfo;
		int retries = parameters.retries;

		// retry loop
		// The logic tries to retrieve the response several times in case there is
		// a delay of execution on the slave or some other error that can be possibly
		// remedied by trying again.
		while (retries >= 0) {
			// errorInfo may be null if the caller is not interested in error details
			if (errorInfo != nullptr)
				*errorInfo = 0;
#ifdef WIN32
			Sleep(parameters.delayMs);
#else
			// wait for the specified delay
			timespec sleeptime;
			sleeptime.tv_sec = parameters.delayMs / 1000;
			sleeptime.tv_nsec = (parameters.delayMs % 1000) * 1000000L;
			nanosleep(&sleeptime, nullptr);
#endif
			// try to retrieve the result
			*size = 0;
			uint8_t result = receive(expected, parameters.useChecksum, destBuffer, size, &errInfo, parameters.verbose);

			// no error?
			if (result == ARDUCOM_OK) {
				break;
			}

			// special case: if NO_DATA has been received, give the slave more time to react
			if ((result == ARDUCOM_NO_DATA) && (retries > 0)) {
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

			case ARDUCOM_NO_DATA:
				throw std::runtime_error("ARDUCOM_NO_DATA (not enough data sent or command not yet processed, try to increase --initDelay, delay -l or number of retries -x)");

			case ARDUCOM_COMMAND_UNKNOWN:
				throw std::runtime_error((std::string("ARDUCOM_COMMAND_UNKNOWN (") + resultStr + "): " + errInfoStr).c_str());

			case ARDUCOM_TOO_MUCH_DATA:
				throw std::runtime_error((std::string("ARDUCOM_TOO_MUCH_DATA (") + resultStr + "); expected bytes: " + errInfoStr).c_str());

			case ARDUCOM_PARAMETER_MISMATCH:
				// sporadic I2C dropouts cause this error (receiver problems?)
				// seem to be unrelated to baud rate...
				throw std::runtime_error((std::string("ARDUCOM_PARAMETER_MISMATCH (") + resultStr + "); expected bytes: " + errInfoStr).c_str());

			case ARDUCOM_BUFFER_OVERRUN:
				throw std::runtime_error((std::string("ARDUCOM_BUFFER_OVERRUN (") + resultStr + "); buffer size is: " + errInfoStr).c_str());

			case ARDUCOM_CHECKSUM_ERROR:
				throw std::runtime_error((std::string("ARDUCOM_CHECKSUM_ERROR (") + resultStr + "); calculated checksum: " + errInfoStr).c_str());

			case ARDUCOM_LIMIT_EXCEEDED:
				throw std::runtime_error((std::string("ARDUCOM_LIMIT_EXCEEDED (") + resultStr + "); limit is: " + errInfoStr).c_str());

			case ARDUCOM_FUNCTION_ERROR: {
				// set errorInfo and throw an exception to signal the caller that a function error occurred
				if (errorInfo != nullptr)
					*errorInfo = errInfo;
				throw Arducom::FunctionError((std::string("ARDUCOM_FUNCTION_ERROR ") + resultStr + ": info code: " + errInfoStr).c_str());
			} 
			
			case ARDUCOM_NOT_IMPLEMENTED:
				throw std::runtime_error("ARDUCOM_NOT_IMPLEMENTED: This function is not implemented on the slave device");
			
			case ARDUCOM_INVALID_CONFIG:
				throw std::runtime_error("ARDUCOM_INVALID_CONFIG: The configuration of the slave device is not valid for this function");

			default:
				// handle unknown errors
				throw std::runtime_error((std::string("Device error ") + resultStr + "; info code: " + errInfoStr).c_str());
			}
		}	// while (retries)

	} catch (const std::exception&) {
		// cleanup after the transaction
		done(parameters.debug);
		char commandStr[21];
		sprintf(commandStr, "%d", command);
		std::throw_with_nested(std::runtime_error((std::string("Error executing command ") + commandStr).c_str()));
	}

	if (close)
        // cleanup after the transaction
        this->close(parameters.debug);
}

void ArducomMaster::close(bool verbose) {
    done(verbose);
}


/*** ArducomMaster internal functions ***/

void ArducomMaster::lock(bool verbose, long timeoutMs) {
	if (this->semkey == 0)
		return;

#ifndef __NO_LOCK_MECHANISM
	// acquire interprocess semaphore to avoid contention
	if (verbose)
		std::cout << "Acquiring interprocess communication semaphore with key 0x" << std::hex << this->semkey << "..." << std::dec << std::endl;

	// when creating, allow access for processes running under all users
	this->semid = semget(this->semkey, 1, IPC_CREAT | 0666);
	if (this->semid < 0)
		throw_system_error("Unable to create or open semaphore");

	// avoid increasing the semaphore more than once
	if (this->hasLock)
		throw std::runtime_error("Programming error: Trying to increase the resource more than once");

	struct sembuf semops[2];

	// semaphore has been created or opened, wait until it becomes available
	// wait until the semaphore becomes zero
	semops[0].sem_num = 0;
	semops[0].sem_op = 0;
	semops[0].sem_flg = 0;
	// increment the value by one
	semops[1].sem_num = 0;
	semops[1].sem_op = 1;
	semops[1].sem_flg = SEM_UNDO;

#ifdef linux
	// try to acquire resource
	// wait for the specified timeout
	struct timespec timeout;
	timeout.tv_sec = timeoutMs / 1000;
	timeout.tv_nsec = (timeoutMs % 1000) * 1000;

	if (semtimedop(this->semid, semops, 2, &timeout) < 0) {
		// error acquiring semaphore
		throw_system_error("Error acquiring semaphore");
	}
#else
	// POSIX-compliant version
	if (semop(this->semid, semops, 2) < 0) {
		// error acquiring semaphore
		throw_system_error("Error acquiring semaphore");
	}
#endif

#endif

	this->hasLock = true;
}

void ArducomMaster::unlock(bool verbose) {
	if (this->semkey == 0)
		return;
	if (!this->hasLock)
		return;
#ifndef __NO_LOCK_MECHANISM
	if (verbose)
		std::cout << "Releasing interprocess communication semaphore..." << std::endl;

	// decrease the semaphore
	struct sembuf semops;
	semops.sem_num = 0;
	semops.sem_op = -1;
	semops.sem_flg = SEM_UNDO;
	if (semop(this->semid, &semops, 1) < 0)
		perror("Error decreasing semaphore");
#endif

	this->hasLock = false;
}

void ArducomMaster::send(uint8_t command, bool checksum, uint8_t* buffer, uint8_t size, int retries, bool verbose) {
	this->lastError = ARDUCOM_OK;
#if defined(WIN32) && !defined(__MINGW32__)
	uint8_t* data = (uint8_t*)alloca(size + (checksum ? 3 : 2));
#else
	uint8_t data[size + (checksum ? 3 : 2)];
#endif
	data[0] = command;
	data[1] = size | (checksum ? 0x80 : 0);
	for (uint8_t i = 0; i < size; i++) {
		data[i + (checksum ? 3 : 2)] = buffer[i];
	}
	if (checksum)
		data[2] = calculateChecksum(data[0], data[1], &data[3], size);
	if (verbose) {
		std::cout << "Sending bytes: " ;
		this->printBuffer(data, size + (checksum ? 3 : 2));
		std::cout << std::endl;
	}
	try {
		this->transport->sendBytes(data, size + (checksum ? 3 : 2), retries);
	} catch (const std::exception&) {
		this->lastError = ARDUCOM_TRANSPORT_ERROR;
		std::throw_with_nested(std::runtime_error("Error sending data"));
	}
	this->lastCommand = command;
}

uint8_t ArducomMaster::receive(uint8_t expected, bool useChecksum, uint8_t* destBuffer, uint8_t* size, uint8_t *errorInfo, bool verbose) {
	this->lastError = ARDUCOM_OK;

	if (this->lastCommand > 127) {
		this->lastError = ARDUCOM_NO_COMMAND;
		throw std::runtime_error("Cannot receive without sending a command first");
	}

	try {
		this->transport->request(expected);
	} catch (const Arducom::TimeoutException&) {
		this->lastError = ARDUCOM_TIMEOUT;
		return ARDUCOM_NO_DATA;
	} catch (const std::exception&) {
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
	} catch (const std::exception&) {
		this->lastError = ARDUCOM_TRANSPORT_ERROR;
		std::throw_with_nested(std::runtime_error("Error reading data"));
	}

	// error?
	if (resultCode == ARDUCOM_ERROR_CODE) {
		if (verbose)
			std::cout << "Received error code 0xff" << std::endl;
		try	{
			resultCode = this->transport->readByte();
		} catch (const std::exception&) {
			this->lastError = ARDUCOM_TRANSPORT_ERROR;
			std::throw_with_nested(std::runtime_error("Error reading data"));
		}
		if (verbose) {
			std::cout << "Error: ";
			this->printBuffer(&resultCode, 1);
		}
		try	{
			*errorInfo = this->transport->readByte();
		} catch (const std::exception&) {
			this->lastError = ARDUCOM_TRANSPORT_ERROR;
			std::throw_with_nested(std::runtime_error("Error reading data"));
		}
		if (verbose) {
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

	if (verbose) {
		std::cout << "Response command code is ok." << std::endl;
	}

	// read code byte
	uint8_t code;
	try	{
		code = this->transport->readByte();
	} catch (const std::exception&) {
		this->lastError = ARDUCOM_TRANSPORT_ERROR;
		std::throw_with_nested(std::runtime_error("Error reading data"));
	}

	uint8_t length = (code & 0b00111111);
	bool checksum = (code & 0x80) == 0x80;
	if (checksum != useChecksum)
		throw std::runtime_error("Checksum flag mismatch");
	if (verbose) {
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
		} catch (const std::exception&) {
			this->lastError = ARDUCOM_TRANSPORT_ERROR;
			std::throw_with_nested(std::runtime_error("Error reading data"));
		}

	*size = 0;
	// read payload into the buffer; up to expected bytes or returned bytes, whatever is lower
	for (uint8_t i = 0; (i < expected) && (i < length); i++) {
		try {
			destBuffer[i] = this->transport->readByte();
		} catch (const std::exception&) {
			this->lastError = ARDUCOM_TRANSPORT_ERROR;
			std::throw_with_nested(std::runtime_error("Error reading data"));
		}
		*size = i + 1;
	}
	if ((*size > 0) && verbose) {
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

void ArducomMaster::done(bool verbose) {
	this->transport->done();
	this->unlock(verbose);
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

