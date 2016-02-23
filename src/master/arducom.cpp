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

/* Input and output data formats */
enum Format {
	HEX,
	RAW,
	BYTE,
	INT16,
	INT32,
	INT64
};

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

/* Split string into parts at specified delimiter */
std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

/* Parse parameter and add to payload; convert depending on specified format */
void parsePayload(std::string arg, Format format, char separator, std::vector<uint8_t> &params) {
	// for all formats but raw: does the string contain the separator?
	if ((format != RAW) && (separator != '\0') && (arg.find(separator) != std::string::npos)) {
		// split along the separators
		std::vector<std::string> parts;
		split(arg, separator, parts);
		// parse each part
		std::vector<std::string>::const_iterator it = parts.begin();
		while (it != parts.end()) {
			// pass empty separator to avoid searching a second time
			parsePayload(*it, format, '\0', params);
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

/* Specialized parameters class */
class ArducomParameters : public ArducomBaseParameters {

public:
	std::vector<uint8_t> payload;
	int command;
	bool paramSpecified;
	bool readInputSpecified;
	int expectedBytes;
	Format inputFormat;
	Format outputFormat;
	bool noNewline;
	char outputSeparator;
	char inputSeparator;
	bool tryInterpret;

	ArducomParameters() : ArducomBaseParameters() {
		command = -1;
		paramSpecified = false;
		readInputSpecified = false;
		expectedBytes = -1;
		inputFormat = HEX;
		outputFormat = HEX;
		noNewline = false;
		outputSeparator = ',';
		inputSeparator = outputSeparator;
		tryInterpret = true;
	}

	void evaluateArgument(std::vector<std::string>& args, size_t* i) override {
		if (args.at(*i) == "-c") {
			(*i)++;
			if (args.size() == *i) {
				throw std::invalid_argument("Expected command number after argument -c");
			} else {
				try {
					command = std::stoi(args.at(*i));
				} catch (std::exception& e) {
					throw std::invalid_argument("Expected numeric command number after argument -c");
				}
			}
		} else
		if (args.at(*i) == "-e") {
			(*i)++;
			if (args.size() == *i) {
				throw std::invalid_argument("Expected number of expected bytes after argument -e");
			} else {
				try {
					expectedBytes = std::stoi(args.at(*i));
				} catch (std::exception& e) {
					throw std::invalid_argument("Expected number after argument -e");
				}
			}
		} else
		if (args.at(*i) == "-i") {
			(*i)++;
			if (args.size() == *i) {
				throw std::invalid_argument("Expected input format after argument -i");
			} else {
				inputFormat = parseFormat(args.at(*i), "-i");
			}
		} else
		if (args.at(*i) == "-o") {
			(*i)++;
			if (args.size() == *i) {
				throw std::invalid_argument("Expected output format after argument -o");
			} else {
				outputFormat = parseFormat(args.at(*i), "-o");
			}
		} else
		if (args.at(*i) == "-s") {
			(*i)++;
			if (args.size() == *i) {
				throw std::invalid_argument("Expected separator character after argument -s");
			} else {
				outputSeparator = args.at(*i)[0];
				inputSeparator = args.at(*i)[0];
			}
		} else
		if (args.at(*i) == "-si") {
			(*i)++;
			if (args.size() == *i) {
				throw std::invalid_argument("Expected input separator character after argument -si");
			} else {
				inputSeparator = args.at(*i)[0];
			}
		} else
		if (args.at(*i) == "-so") {
			(*i)++;
			if (args.size() == *i) {
				throw std::invalid_argument("Expected output separator character after argument -so");
			} else {
				outputSeparator = args.at(*i)[0];
			}
		} else
		if (args.at(*i) == "--no-newline") {
			noNewline = true;
		} else
		if (args.at(*i) == "--no-interpret") {
			tryInterpret = false;
		} else
		if (args.at(*i) == "-p") {
			(*i)++;
			if (args.size() == *i) {
				throw std::invalid_argument("Expected payload value after argument -p");
			} else {
				paramSpecified = true;
				parsePayload(args.at(*i), inputFormat, inputSeparator, payload);
			}
		} else
		if (args.at(*i) == "-r") {
			readInputSpecified = true;
		} else
			ArducomBaseParameters::evaluateArgument(args, i);
	};

	ArducomMasterTransport* validate() {
		ArducomMasterTransport* transport = ArducomBaseParameters::validate();

		if ((command < 0) || (command > 126))
			throw std::invalid_argument("Expected command number within range 0..126 (argument -c)");

		if (readInputSpecified && paramSpecified)
			throw std::invalid_argument("You cannot read parameters from input (-r) and specify parameters (-p) at the same time");

		if (readInputSpecified) {
			// fill buffer from stdin
			char buffer[transport->getMaximumCommandSize() * 4];	// this should be enough for all input formats
			size_t readBytes = fread(buffer, 1, sizeof buffer - 1, stdin);

			buffer[readBytes] = '\0';
			parsePayload(std::string(buffer), inputFormat, inputSeparator, payload);
		}

		if (payload.size() > transport->getMaximumCommandSize()) {
			char errorInfoStr[21];
			sprintf(errorInfoStr, "%zu", transport->getMaximumCommandSize());
			throw std::invalid_argument((std::string("Command payload length must not exceed the transport's maximum command size: ") 
				+ errorInfoStr).c_str());
		}

		// initialize default number of expected bytes
		if (expectedBytes == -1)
			expectedBytes = transport->getDefaultExpectedBytes();

		if ((expectedBytes < 0) || (expectedBytes > 64))
			throw std::invalid_argument("Expected number of bytes must be within range 0..64 (argument -e)");

		return transport;
	};

	void showVersion(void) override {
		std::cout << this->getVersion();
		exit(0);
	};

	 void showHelp(void) override {
		std::cout << this->getHelp();
		exit(0);
	};

protected:
	/** Returns the parameter help for this object. */
	virtual std::string getHelp(void) override {
		std::string result;
		result.append(this->getVersion());

		result.append("\n");
		result.append(ArducomBaseParameters::getHelp());
		
		result.append("\n");
		result.append("Command tool parameters:\n");
		result.append("  -c: Arducom command number between 0 and 127. Required.\n");
		result.append("  -e: Number of expected response payload bytes. Default depends on transport.\n");
		result.append("  -i: Input format of command payload. Default: Hex.\n");
		result.append("    One of: Hex, Raw, Byte, Int16, Int32, Int64.\n");
		result.append("  -o: Output format of response payload. Default: Hex.\n");
		result.append("    One of: Hex, Raw, Byte, Int16, Int32, Int64.\n");
		result.append("  -s: Input and output separator character.\n");
		result.append("  -si: Input separator character.\n");
		result.append("  -so: Output separator character.\n");
		result.append("  -p <payload>: Specifies the command payload.\n");
		result.append("  -r: Read command payload from standard input.\n");
		result.append("    Must be in the specified input format.\n");
 		result.append("  --no-newline: No newline after output.\n");
 		result.append("  --no-interpret: No standard interpretation of command 0.\n");
		result.append("\n");
		result.append("Examples:\n");
		result.append("\n");
 		result.append("./arducom -t serial -d /dev/ttyUSB0 -b 115200 -c 0\n");
 		result.append("  Send command 0 (status inquiry) to the Arduino at /dev/ttyUSB0.\n");
 		result.append("  If this command fails you perhaps need to add --initDelay 3000\n");
 		result.append("  to give the Arduino time to start up after the serial connect.\n");
		result.append("\n");
 		result.append("./arducom -t i2c -d /dev/i2c-1 -a 5 -c 0\n");
 		result.append("  Send command 0 (version inquiry) to the Arduino over I2C bus 1.\n");
		result.append("\n");
 		result.append("./arducom -t i2c -d /dev/i2c-1 -a 5 -c 9 -p 000008 -o Int64\n");
 		result.append("  Send command 9 (read EEPROM) to the Arduino over I2C bus 1.\n");
 		result.append("  Retrieves 8 bytes from EEPROM offset 0000 and displays them\n");
 		result.append("  as a 64 bit integer value. Requires the hello-world sketch.\n");
		
		return result;
	}
	
	virtual std::string getVersion(void) {
		std::string result;
		result.append("Arducom command line tool version 1.0\n");
		result.append("Copyright (c) Leo Meyer 2015-16\n");
		result.append("Build: " __DATE__ " " __TIME__ "\n");
		return result;
	}
};

//********************************************************************************
// Main program
//********************************************************************************

int main(int argc, char* argv[]) {

	ArducomMaster* master = NULL;

	std::vector<std::string> args;
	ArducomParameters::convertCmdLineArgs(argc, argv, args);

	uint8_t errorInfo = 0;
	
	try {
		// create and initialize parameter object
		ArducomParameters parameters;
		parameters.setFromArguments(args);

		ArducomMasterTransport* transport = parameters.validate();

		// initialize protocol
		master = new ArducomMaster(transport, parameters.verbose);
		
		uint8_t buffer[255];
		uint8_t size = parameters.payload.size();

		master->execute(parameters, parameters.command, parameters.payload.data(), &size, parameters.expectedBytes, buffer, &errorInfo);

		// output received?
		if (size > 0) {
			// interpret command 0 (version command)?
			if (parameters.tryInterpret && (parameters.command == 0)) {
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
				switch (parameters.outputFormat) {
				case HEX: ArducomMaster::printBuffer(buffer, size, false, true); break;
				case RAW: ArducomMaster::printBuffer(buffer, size, true, false); break;
				case BYTE: {
					for (uint8_t i = 0; i < size; i++) {
						std::cout << (int)buffer[i];
						if ((i < size - 1) && (parameters.outputSeparator > '\0'))
							std::cout << parameters.outputSeparator;
					}
					break;
				}
				case INT16: {
					if (size % 2 != 0)
						throw std::invalid_argument("Output size must fit into two byte blocks for output format Int16");
					for (uint8_t i = 0; i < size; i += 2) {
						std::cout << ((int16_t)buffer[i] + (int16_t)(buffer[i + 1] << 8));
						if ((i < size - 2) && (parameters.outputSeparator > '\0'))
							std::cout << parameters.outputSeparator;
					}
					break;
				}
				case INT32: {
					if (size % 4 != 0)
						throw std::invalid_argument("Output size must fit into four byte blocks for output format Int32");
					for (uint8_t i = 0; i < size; i += 4) {
						std::cout << ((int)buffer[i] + (int)(buffer[i + 1] << 8) + (int)(buffer[i + 2] << 16) + (int)(buffer[i + 3] << 24));
						if ((i < size - 4) && (parameters.outputSeparator > '\0'))
							std::cout << parameters.outputSeparator;
					}
					break;
				}
				case INT64: {
					if (size % 8 != 0)
						throw std::invalid_argument("Output size must fit into eight byte blocks for output format Int64");
					for (uint8_t i = 0; i < size; i += 8) {
						std::cout << ((long long)buffer[0] + ((long long)buffer[i + 1] << 8) + ((long long)buffer[i + 2] << 16) + ((long long)buffer[i + 3] << 24) + ((long long)buffer[i + 4] << 32) + ((long long)buffer[i + 5] << 40) + ((long long)buffer[i + 6] << 48) + ((long long)buffer[i + 7] << 56));
						if ((i < size - 8) && (parameters.outputSeparator > '\0'))
							std::cout << parameters.outputSeparator;
					}
					break;
				}
				default:
					throw std::invalid_argument("Output format not supported");
				}
			}
			if (!parameters.noNewline)
				std::cout << std::endl;
		}	// output received
	} catch (const std::exception& e) {
		print_what(e);
		if (master != NULL)
			exit(master->lastError);
		else
			exit(1);
	}

	return 0;
}
