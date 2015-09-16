// Main program

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <stdio.h>
#include <exception>
#include <stdexcept>
#include <cstdint>
#include <unistd.h>
#include <cstring>

#include "../slave/lib/Arducom/Arducom.h"
#include "ArducomMaster.h"
#include "ArducomMasterI2C.h"
#include "ArducomMasterSerial.h"

enum Format {
	HEX,
	RAW,
	BYTE,
	INT16,
	INT32,
	INT64
};

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

uint8_t char2byte(char input) {
	if ((input >= '0') && (input <= '9'))
		return input - '0';
	if ((input >= 'A') && (input <= 'F'))
		return input - 'A' + 10;
	if ((input >= 'a') && (input <= 'f'))
		return input - 'a' + 10;
	throw std::invalid_argument(std::string("Invalid hex character in byte input string: ") + input);
}

Format parseFormat(std::string arg, std::string argName) {
	if (arg == "Hex")
		return HEX;
	else
	if (arg == "Raw")
		return RAW;
	else
	if (arg == "Byte")
		return BYTE;
	else
	if (arg == "Int16")
		return INT16;
	else
	if (arg == "Int32")
		return INT32;
	else
	if (arg == "Int64")
		return INT64;
	else
		throw std::invalid_argument("Expected one of the following values after argument " + argName + ": Hex, Raw, Byte, Int16, Int32, Int64");
}

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

void parseParameter(std::string arg, Format format, char separator, std::vector<uint8_t> &params) {
	// for all formats but raw: does the string contain the separator?
	if ((format != RAW) && (separator != '\0') && (arg.find(separator) != std::string::npos)) {
		// split along the separators
		std::vector<std::string> parts;
		split(arg, separator, parts);
		// parse each part
		std::vector<std::string>::const_iterator it = parts.begin();
		while (it != parts.end()) {
			// pass empty separator to avoid searching a second time
			parseParameter(*it, format, '\0', params);
			it++;
		}
	}
	else {
		// parse the string
		switch (format) {
			case HEX: {
				if (arg.size() % 2 == 1)
					throw std::invalid_argument("Expected parameter string of even length for input format Hex");
				const char *paramStr = arg.c_str();
				for (size_t p = 0; p < arg.size() - 1; p += 2) {
					params.push_back(char2byte(paramStr[p]) * 16 + char2byte(paramStr[p + 1]));
				}
				break;
			}
			case RAW: {
				for (size_t p = 0; p < arg.size(); p++) {
					params.push_back(arg.at(p));
				}
				break;
			}
			case BYTE: {
				int value;
				try {
					value = std::stoi(arg);
				} catch (std::exception& e) {
					throw std::invalid_argument("Expected numeric value for input format Byte");
				}
				if ((value < 0) || (value > 255))
					throw std::invalid_argument("Input value for format Byte must be in range 0..255");
				params.push_back((uint8_t)value);
				break;
			}
			case INT16: {
				int value;
				try {
					value = std::stoi(arg);
				} catch (std::exception& e) {
					throw std::invalid_argument("Expected numeric value for input format Int16");
				}
				if ((value < -32768) || (value > 32767))
					throw std::invalid_argument("Input value for format Int16 must be in range -32768..32767");
				params.push_back((uint8_t)value);
				params.push_back((uint8_t)(value >> 8));
				break;
			}
			case INT32: {
				long long value;
				try {
					value = std::stoll(arg);
				} catch (std::exception& e) {
					throw std::invalid_argument("Expected numeric value for input format Int32");
				}
				if ((value < -2147483648) || (value > 2147483647))
					throw std::invalid_argument("Input value for format Int32 must be in range -2147483648..2147483647");
				params.push_back((uint8_t)value);
				params.push_back((uint8_t)(value >> 8));
				params.push_back((uint8_t)(value >> 16));
				params.push_back((uint8_t)(value >> 24));
				break;
			}
			case INT64: {
				long long value;
				try {
					value = std::stoll(arg);
				} catch (std::exception& e) {
					throw std::invalid_argument("Expected numeric value for input format Int64");
				}
				params.push_back((uint8_t)value);
				params.push_back((uint8_t)(value >> 8));
				params.push_back((uint8_t)(value >> 16));
				params.push_back((uint8_t)(value >> 24));
				params.push_back((uint8_t)(value >> 32));
				params.push_back((uint8_t)(value >> 40));
				params.push_back((uint8_t)(value >> 48));
				params.push_back((uint8_t)(value >> 56));
				break;
			}
			default:
				throw std::invalid_argument("Parse format not supported");
		}
	}
}

int main(int argc, char *argv[]) {
	
	std::string transportType;
	std::string device;
	int baudrate = 9600;
	int deviceAddress = 0;
	bool verbose = false;
	int command = -1;
	std::vector<uint8_t> params;
	bool paramSpecified = false;
	bool readInputSpecified = false;
	long delayMs = 0;
	int expectedBytes = -1;
	Format inputFormat = HEX;
	Format outputFormat = HEX;
	bool noNewline = false;
	char outputSeparator = ',';
	char inputSeparator = outputSeparator;
	int retries = 0;
	bool tryInterpret = true;
	bool useChecksum = true;
	ArducomMaster* master = NULL;
	
	std::vector<std::string> args;
	args.reserve(argc);
	for (int i = 0; i < argc; i++) {
		char* targ = argv[i];
		std::string arg(targ);
		args.push_back(arg);
	}
	
	try {
		// evaluate arguments
		for (unsigned int i = 1; i < args.size(); i++) {
			if (args.at(i) == "-h" || args.at(i) == "-?") {
				return 0;
			} else
			if (args.at(i) == "--version") {
				return 0;
			} else
			if (args.at(i) == "-v") {
				verbose = true;
			} else
			if (args.at(i) == "--no-interpret") {
				tryInterpret = false;
			} else
			if (args.at(i) == "-n") {
				useChecksum = false;
			} else
			if (args.at(i) == "-p") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected parameter value after argument -p");
				} else {
					paramSpecified = true;
					parseParameter(args.at(i), inputFormat, inputSeparator, params);
				}
			} else		
			if (args.at(i) == "-r") {
				readInputSpecified = true;
			} else
			if (args.at(i) == "-t") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected transport type after argument -t");
				} else {
					transportType = args.at(i);
				}
			} else		
			if (args.at(i) == "-d") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected device file name after argument -d");
				} else {
					device = args.at(i);
				}
			} else
			if (args.at(i) == "-a") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected device address after argument -a");
				} else {
					try {
						deviceAddress = std::stoi(args.at(i));
					} catch (std::exception& e) {
						throw std::invalid_argument("Expected numeric device address after argument -a");
					}
				}
			} else
			if (args.at(i) == "-b") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected baud rate after argument -b");
				} else {
					try {
						baudrate = std::stoi(args.at(i));
					} catch (std::exception& e) {
						throw std::invalid_argument("Expected numeric baudrate after argument -b");
					}
				}
			} else
			if (args.at(i) == "-c") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected command number after argument -c");
				} else {
					try {
						command = std::stoi(args.at(i));
					} catch (std::exception& e) {
						throw std::invalid_argument("Expected numeric command number after argument -c");
					}
				}
			} else
			if (args.at(i) == "-e") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected number of expected bytes after argument -e");
				} else {
					try {
						expectedBytes = std::stoi(args.at(i));
					} catch (std::exception& e) {
						throw std::invalid_argument("Expected number after argument -e");
					}
				}
			} else
			if (args.at(i) == "-l") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected delay in ms after argument -l");
				} else {
					try {
						delayMs = std::stol(args.at(i));
					} catch (std::exception& e) {
						throw std::invalid_argument("Expected numeric delay in ms after argument -l");
					}
				}
			} else
			if (args.at(i) == "-i") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected input format after argument -i");
				} else {
					inputFormat = parseFormat(args.at(i), "-i");
				}
			} else
			if (args.at(i) == "-o") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected output format after argument -o");
				} else {
					outputFormat = parseFormat(args.at(i), "-o");
				}
			} else
			if (args.at(i) == "-s") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected separator character after argument -s");
				} else {
					outputSeparator = args.at(i)[0];
					inputSeparator = args.at(i)[0];
				}
			} else
			if (args.at(i) == "-si") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected input separator character after argument -si");
				} else {
					inputSeparator = args.at(i)[0];
				}
			} else
			if (args.at(i) == "-so") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected output separator character after argument -so");
				} else {
					outputSeparator = args.at(i)[0];
				}
			} else
			if (args.at(i) == "-x") {
				i++;
				if (args.size() == i) {
					throw std::invalid_argument("Expected number of retries after argument -x");
				} else {
					try {
						retries = std::stoi(args.at(i));
					} catch (std::exception& e) {
						throw std::invalid_argument("Expected number after argument -x");
					}
				}
			} else
			if (args.at(i) == "--no-newline") {
				noNewline = true;
			}
		}
		
		ArducomMasterTransport *transport;
		
		if (transportType == "i2c") {
			if (device == "")
				throw std::invalid_argument("Error: missing transport device name (argument -d)");
				
			if ((deviceAddress < 1) || (deviceAddress > 127))
				throw std::invalid_argument("Expected device address within range 1..127 (argument -a)");

			transport = new ArducomMasterTransportI2C(device, deviceAddress);
			try {
				transport->init();
			} catch (const std::exception& e) {
				std::throw_with_nested(std::runtime_error("Error initializing transport"));
			}
		} else
		if (transportType == "serial") {
			if (device == "")
				throw std::invalid_argument("Error: missing transport device name (argument -d)");

			// TODO check baud rate

			transport = new ArducomMasterTransportSerial(device, baudrate, 1000);
			try {
				transport->init();
			} catch (const std::exception& e) {
				std::throw_with_nested(std::runtime_error("Error initializing transport"));
			}
		} else
			throw std::invalid_argument("Error: transport type not supported (argument -t), use 'i2c' or 'serial'");

		if ((command < 0) || (command > 126))
			throw std::invalid_argument("Expected command number within range 0..126 (argument -c)");
			
		if (readInputSpecified && paramSpecified)
			throw std::invalid_argument("You cannot read parameters from input (-r) and specify parameters (-p) at the same time");
		
		if (readInputSpecified) {
			// fill buffer from stdin
			char buffer[transport->getMaximumCommandSize() * 4];	// this should be enough for all input formats
			size_t readBytes = fread(buffer, 1, sizeof buffer - 1, stdin);
			
			buffer[readBytes] = '\0';
			parseParameter(std::string(buffer), inputFormat, inputSeparator, params);
		}
			
		if (params.size() > transport->getMaximumCommandSize()) {
			char numstr[21];
			sprintf(numstr, "%zu", transport->getMaximumCommandSize());
			throw std::invalid_argument((std::string("Command parameter length must not exceed the transport's maximum command size: ") 
				+ numstr).c_str());
		}

		// initialize default number of expected bytes
		if (expectedBytes == -1)
			expectedBytes = transport->getDefaultExpectedBytes();
		
		if ((expectedBytes < 0) || (expectedBytes > 64))
			throw std::invalid_argument("Expected number of bytes must be within range 0..64 (argument -e)");

		if (retries < 0)
			throw std::invalid_argument("Number of retries must not be negative (argument -x)");

		// initialize protocol
		master = new ArducomMaster(transport, verbose);
		
		// send command
		master->send(command, useChecksum, params.data(), params.size());
		
		// receive response
		uint8_t buffer[255];
		uint8_t size;
		uint8_t errorInfo;
		
		// retry loop
		while (retries >= 0) {
			// wait for the specified delay
			usleep(delayMs * 1000);
			size = 0;
			
			uint8_t result = master->receive(expectedBytes, buffer, &size, &errorInfo);

			// no error?
			if (result == ARDUCOM_OK) {
				// let the master cleanup after the transaction
				master->done();
				
				break;
			}

			// special case: if NO_DATA has been received, give the slave more time to react
			// without resending the message
			if (result == ARDUCOM_NO_DATA) {
				retries--;
				if (retries > 0) {
					if (verbose) {
						std::cout << "Received no data, " << retries << " retries left" << std::endl;
					}
					master->done();
					continue;
				}
			}
			
			// let the master cleanup after the transaction
			master->done();
			
			// convert error code to string
			char errstr[21];
			sprintf(errstr, "%d", result);
			
			// convert info code to string
			char numstr[21];
			sprintf(numstr, "%d", errorInfo);
			
			switch (result) {
			case ARDUCOM_NO_DATA: {
				retries--;
				if (retries <= 0)
					throw std::runtime_error((std::string("Device error ") + errstr + ": No data (not enough data sent or command not yet processed, try to increase delay -l or number of retries -x)").c_str());
				if (verbose)
					std::cout << "No data received, retrying..." << std::endl;
				continue;
			}
			case ARDUCOM_COMMAND_UNKNOWN:
				throw std::runtime_error((std::string("Command unknown (") + errstr + "): " + numstr).c_str());
			case ARDUCOM_TOO_MUCH_DATA:
				throw std::runtime_error((std::string("Too much data (") + errstr + "); expected bytes: " + numstr).c_str());
			case ARDUCOM_PARAMETER_MISMATCH: {
				// sporadic I2C dropouts cause this error (receiver problems?)
				// seem to be unrelated to baud rate...
				retries--;
				if (retries < 0)
					throw std::runtime_error((std::string("Parameter mismatch (") + errstr + "); expected bytes: " + numstr).c_str());
				else
					// try again
					continue;
			}
			case ARDUCOM_BUFFER_OVERRUN:
				throw std::runtime_error((std::string("Buffer overrun (") + errstr + "); buffer size is: " + numstr).c_str());
			case ARDUCOM_CHECKSUM_ERROR:
				throw std::runtime_error((std::string("Checksum error (") + errstr + "); calculated checksum: " + numstr).c_str());
			case ARDUCOM_FUNCTION_ERROR:
				throw std::runtime_error((std::string("Function error ") + errstr + ": info code: " + numstr).c_str());
			}
			throw std::runtime_error((std::string("Device error ") + errstr + "; info code: " + numstr).c_str());
		}
		
		// output received?
		if (size > 0) {
			// interpret command 0 (version command)?
			if (tryInterpret && command == 0) {
				struct __attribute__((packed)) VersionInfo {
					uint8_t version;
					uint32_t uptime;
					uint8_t flags;
					uint16_t freeRAM;
					char info[64];
				} versionInfo;
				// clear structure
				memset(&versionInfo, 0, sizeof(versionInfo));
				// copy received data
				memcpy(&versionInfo, buffer, size);
				std::cout << "Arducom slave version: " << (int)versionInfo.version;
				std::cout << "; Uptime: " << versionInfo.uptime << " ms";
				std::cout << "; Flags: " << (int)versionInfo.flags << (versionInfo.flags & 1 ? " (debug on)" : " (debug off)");
				std::cout << "; Free RAM: " << versionInfo.freeRAM << " bytes";
				std::cout << "; Info: " << versionInfo.info;
			} else {
				// cannot or should not interpret
				switch (outputFormat) {
				case HEX: ArducomMaster::printBuffer(buffer, size, false, true); break;
				case RAW: ArducomMaster::printBuffer(buffer, size, true, false); break;
				case BYTE: {
					for (uint8_t i = 0; i < size; i++) {
						std::cout << (int)buffer[i];
						if ((i < size - 1) && (outputSeparator > '\0'))
							std::cout << outputSeparator;
					}
					break;
				}
				case INT16: {
					if (size % 2 != 0)
						throw std::invalid_argument("Output size must fit into two byte blocks for output format Int16");
					for (uint8_t i = 0; i < size; i += 2) {
						std::cout << ((int16_t)buffer[i] + (int16_t)(buffer[i + 1] << 8));
						if ((i < size - 2) && (outputSeparator > '\0'))
							std::cout << outputSeparator;
					}
					break;
				}
				case INT32: {
					if (size % 4 != 0)
						throw std::invalid_argument("Output size must fit into four byte blocks for output format Int32");
					for (uint8_t i = 0; i < size; i += 4) {
						std::cout << ((int)buffer[i] + (int)(buffer[i + 1] << 8) + (int)(buffer[i + 2] << 16) + (int)(buffer[i + 3] << 24));
						if ((i < size - 4) && (outputSeparator > '\0'))
							std::cout << outputSeparator;
					}
					break;
				}
				case INT64: {
					if (size % 8 != 0)
						throw std::invalid_argument("Output size must fit into eight byte blocks for output format Int64");
					for (uint8_t i = 0; i < size; i += 8) {
						std::cout << ((long long)buffer[0] + ((long long)buffer[i + 1] << 8) + ((long long)buffer[i + 2] << 16) + ((long long)buffer[i + 3] << 24) + ((long long)buffer[i + 4] << 32) + ((long long)buffer[i + 5] << 40) + ((long long)buffer[i + 6] << 48) + ((long long)buffer[i + 7] << 56));
						if ((i < size - 8) && (outputSeparator > '\0'))
							std::cout << outputSeparator;
					}
					break;
				}
				default:
					throw std::invalid_argument("Output format not supported");
				}
			}
			if (!noNewline)
				std::cout << std::endl;
		}	// output received
	} catch (const std::exception& e) {
		print_what(e);
		exit(master->lastError);
	}

	return 0;
}
