// arducom-ftp
// Arducom file transfer ("FTP") master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

// required for non-ANSI function fileno
#define _POSIX_C_SOURCE 200809L

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <stdio.h>
#include <exception>
#include <stdexcept>
#include <cstdint>
#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>
#include <iomanip>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <bitset>

#include "../slave/lib/Arducom/Arducom.h"
#include "../slave/lib/Arducom/ArducomFTP.h"

#include "ArducomMaster.h"
#include "ArducomMasterSerial.h"
#ifndef ARDUCOM_NO_I2C
#include "ArducomMasterI2C.h"
#endif

#ifdef _MSC_VER
#include <io.h>
#else
#include <unistd.h>
#endif

// missing defines on MSC
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif
#ifndef F_OK
#define F_OK 0
#define S_IRGRP 0040
#define S_IROTH 0004
#endif

// no O_BINARY on *nix
#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef __GNUC__
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

/* Trim from start */
static inline std::string& ltrim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
}

/* Trim from end */
static inline std::string& rtrim(std::string& s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
}

/* Trim from both ends */
static inline std::string& trim(std::string& s) {
        return ltrim(rtrim(s));
}

/* Split string into parts at specified delimiter */
std::vector<std::string>& split(const std::string& s, char delim, std::vector<std::string>& elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

/* Specialized parameters class */
class ArducomFTPParameters : public ArducomBaseParameters {

public:
	uint8_t commandBase;
	bool continueFile;
	bool allowDelete;

	ArducomFTPParameters() : ArducomBaseParameters() {
		commandBase = ARDUCOM_FTP_DEFAULT_COMMANDBASE;
		continueFile = true;
		allowDelete = false;
		// increase the default command delay because SD card operations may be slow
		delayMs = 25;
		// set default number of retries
		retries = 3;
	}

	void evaluateArgument(std::vector<std::string>& args, size_t* i) override {
		if (args.at(*i) == "--no-continue") {
			continueFile = false;
		} else
		if (args.at(*i) == "--allow-delete") {
			allowDelete = true;
		} else
			ArducomBaseParameters::evaluateArgument(args, i);
	};

	ArducomMasterTransport* validate() {
		ArducomMasterTransport* transport = ArducomBaseParameters::validate();
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

	/** Returns the parameter help for this object. */
	virtual std::string getHelp(void) override {
		std::string result;
		result.append(this->getVersion());

		result.append("\n");
		result.append(ArducomBaseParameters::getHelp());
		
		result.append("\n");
		result.append("FTP tool parameters:\n");
 		result.append("  --no-continue: Always overwrite existing files.\n");
 		result.append("  --allow-delete: Allow the (experimental) deletion of files.\n");
		result.append("\n");
		result.append("Examples:\n");
		result.append("\n");
 		result.append("./arducom-ftp -t serial -d /dev/ttyUSB0 -b 115200\n");
 		result.append("  Connects to the Arduino at /dev/ttyUSB0.\n");
 		result.append("  If this command fails you perhaps need to add --initDelay 3000\n");
 		result.append("  to give the Arduino time to start up after the serial connect.\n");
		result.append("\n");
 		result.append("./arducom-ftp -t i2c -d /dev/i2c-1 -a 5 -c 0\n");
 		result.append("  Connects to an Arduino at slave address 5 over I2C bus 1.\n");
		result.append("\n");
		result.append("Usage:\n");
		result.append("\n");
		result.append("  Enter ? on the FTP tool prompt to get help.");
		result.append("\n");
		
		return result;
	}
	
	virtual std::string getVersion(void) {
		std::string result;
		result.append("Arducom FTP tool v1.2\n");
		result.append("https://github.com/leomeyer/Arducom\n");
		result.append("Build: " __DATE__ " " __TIME__ "\n");
		return result;
	}
};

/********************************************************************************/

ArducomFTPParameters parameters;
std::vector<std::string> pathComponents;
bool needEndl = false;		// flag: cout << endl before printing messages
bool interactive;			// if false (piping input) errors cause immediate exit

/********************************************************************************/

void execute(ArducomMaster& master, uint8_t command, std::vector<uint8_t>& payload, uint8_t expectedBytes, std::vector<uint8_t>& result, bool canRetry = false) {

	int8_t retries = parameters.retries;
	uint8_t errorInfo;
	
	while (retries >= 0) {
		try {
			uint8_t buffer[255];
			uint8_t size = (uint8_t)payload.size();
			errorInfo = 0;
			
			master.execute(parameters, parameters.commandBase + command, payload.data(), &size, expectedBytes, buffer, &errorInfo);

			// everything ok, copy response
			result.clear();
		
			for (size_t i = 0; i < size; i++) 
				result.push_back(buffer[i]);
				
			return;
			
		} catch (const std::exception& e) {
			
			// function error (errorInfo > 0)?
			if (errorInfo > 0) {
				// convert info code to string
				char errorInfoStr[21];
				sprintf(errorInfoStr, "%d", errorInfo);
				
				switch (errorInfo) {
				case ARDUCOM_FTP_SDCARD_ERROR: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": SD card unavailable").c_str());
				case ARDUCOM_FTP_SDCARD_TYPE_UNKNOWN: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": SD card type unknown").c_str());
				case ARDUCOM_FTP_FILESYSTEM_ERROR: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": SD card file system error").c_str());
				case ARDUCOM_FTP_NOT_INITIALIZED: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": FTP system not initialized").c_str());
				case ARDUCOM_FTP_MISSING_FILENAME: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": Required file name is missing").c_str());
				case ARDUCOM_FTP_NOT_A_DIRECTORY: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": Not a directory").c_str());
				case ARDUCOM_FTP_FILE_OPEN_ERROR: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": Error opening file").c_str());
				case ARDUCOM_FTP_READ_ERROR: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": Read error").c_str());
				case ARDUCOM_FTP_FILE_NOT_OPEN: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": File not open").c_str());
				case ARDUCOM_FTP_POSITION_INVALID: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": File seek position invalid").c_str());
				case ARDUCOM_FTP_CANNOT_DELETE: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": Cannot delete this file or folder (long file name?)").c_str());
				default: throw std::runtime_error((std::string("FTP error ") + errorInfoStr + ": Unknown error").c_str());
				}
			} else {
				if (master.lastError == ARDUCOM_COMMAND_UNKNOWN) {
					throw std::runtime_error("FTP is not supported by the slave");
				}
			}
			
			if (canRetry && (retries > 0)) {
				retries--;
				// do not print retry messages except in verbose mode
				if (parameters.verbose) {
					print_what(e);
					std::cout << "Retrying, " << (int)retries << " " << (retries == 1 ? "retry" : "retries") << " left..." << std::endl;
				}
				continue;
			}
			else
				std::throw_with_nested(std::runtime_error("Error during FTP operation"));
		}
	}	// while (retries)
}

void printPathComponents(void) {
	std::vector<std::string>::const_iterator it = pathComponents.cbegin();
	std::vector<std::string>::const_iterator ite = pathComponents.cend();
	while (it != ite) {
		std::cout << *it;
		std::vector<std::string>::const_iterator prev_it = it;
		++it;
		if (*prev_it != "/")
			std::cout << "/";
	}
}

void prompt(void) {
	printPathComponents();
	std::cout << "> ";
}

void initSlaveFAT(ArducomMaster& master, ArducomMasterTransport* transport) {
	std::vector<uint8_t> payload;
	std::vector<uint8_t> result;

	// send INIT message
	execute(master, ARDUCOM_FTP_COMMAND_INIT, payload, transport->getDefaultExpectedBytes(), result, true);

	PACK(struct CardInfo {
		char cardType[4];
		uint8_t fatType;
		uint8_t size1;	// size is little-endian
		uint8_t size2;
		uint8_t size3;
		uint8_t size4;
	}) cardInfo;

	memcpy(&cardInfo, result.data(), sizeof(cardInfo));

	// show card information
	char cardType[5];
	memcpy(&cardType, &cardInfo.cardType, 4);
	cardType[4] = '\0';
	uint32_t cardSize = (cardInfo.size1 + (cardInfo.size2 << 8) + (cardInfo.size3 << 16) + (cardInfo.size4 << 24));

	std::cout << "Connected. SD card type: " << cardType << " FAT" << (int)cardInfo.fatType << " Size: " << cardSize << " MB" << std::endl;

	// root path component
	pathComponents.clear();
	pathComponents.push_back("/");
}

void printProgress(uint32_t total, uint32_t current, uint8_t width) {
	std::cout << '\r';
	float val = current / (float)total;
	int percent = (int)(val * 100.0);
	std::cout << std::setw(3) << std::right << percent << "% [";
	for (uint8_t i = 0; i < width; i++) {
		float iCur = i / (float)width;
		if (iCur < val)
			std::cout << '#';
		else
			std::cout << ' ';
	}
	std::cout << ']';
	fflush(stdout);
	needEndl = true;
}

void setParameter(std::vector<std::string> parts, bool print = true) {
	bool printOnly = false;
	if (parts.size() < 2) {
		printOnly = true;
		parts.push_back("");	// push dummy
	}
	bool found = false;
	if (parts.at(1) == "verbose" || printOnly) {
		if (parts.size() > 2) {
			if (parts.at(2) == "on")
				parameters.verbose = true;
			else
			if (parts.at(2) == "off") {
				parameters.verbose = false;
				parameters.debug = false;
			} else
				throw std::invalid_argument("Expected 'on' or 'off'");
		}
		if (print)
			std::cout << "set verbose " << (parameters.verbose ? "on" : "off") << std::endl;
		found = true;
	}
	if (parts.at(1) == "debug" || printOnly) {
		if (parts.size() > 2) {
			if (parts.at(2) == "on") {
				parameters.verbose = true;
				parameters.debug = true;
			} else
			if (parts.at(2) == "off")
				parameters.debug = false;
			else
				throw std::invalid_argument("Expected 'on' or 'off'");
		}
		if (print)
			std::cout << "set debug " << (parameters.debug ? "on" : "off") << std::endl;
		found = true;
	}
	if (parts.at(1) == "allowdelete" || printOnly) {
		if (parts.size() > 2) {
			if (parts.at(2) == "on")
				parameters.allowDelete = true;
			else
			if (parts.at(2) == "off")
				parameters.allowDelete = false;
			else
				throw std::invalid_argument("Expected 'on' or 'off'");
		}
		if (print)
			std::cout << "set allowdelete " << (parameters.allowDelete ? "on" : "off") << std::endl;
		found = true;
	}
	if (parts.at(1) == "interactive" || printOnly) {
		if (parts.size() > 2) {
			if (parts.at(2) == "on")
				interactive = true;
			else
			if (parts.at(2) == "off")
				interactive = false;
			else
				throw std::invalid_argument("Expected 'on' or 'off'");
		}
		if (print)
			std::cout << "set interactive " << (interactive ? "on" : "off") << std::endl;
		found = true;
	}
	if (parts.at(1) == "continue" || printOnly) {
		if (parts.size() > 2) {
			if (parts.at(2) == "on")
				parameters.continueFile = true;
			else
			if (parts.at(2) == "off")
				parameters.continueFile = false;
			else
				throw std::invalid_argument("Expected 'on' or 'off'");
		}
		if (print)
			std::cout << "set continue " << (parameters.continueFile ? "on" : "off") << std::endl;
		found = true;
	}
	if (parts.at(1) == "retries" || printOnly) {
		if (parts.size() > 2) {
			try {
				int m_retries = std::stoi(parts.at(2));
				if (m_retries < 0)
					throw std::invalid_argument("");
				parameters.retries = m_retries;
			} catch (std::exception&) {
				throw std::invalid_argument("Expected non-negative number of retries");
			}
		}
		if (print)
			std::cout << "set retries " << parameters.retries << std::endl;
		found = true;
	}
	if (parts.at(1) == "delay" || printOnly) {
		if (parts.size() > 2) {
			try {
				long m_delayMs = std::stol(parts.at(2));
				if (m_delayMs < 0)
					throw std::invalid_argument("");
				parameters.delayMs = m_delayMs;
			} catch (std::exception&) {
				throw std::invalid_argument("Expected non-negative delay in ms");
			}
		}
		if (print)
			std::cout << "set delay " << parameters.delayMs << std::endl;
		found = true;
	}

	if (!found)
		throw std::invalid_argument("Parameter name unknown: " + parts.at(1));
}

void printUsageHelp() {
	std::string result;
	result.append(parameters.getVersion());
	
	result.append("\n");
	result.append("FTP tool commands:\n");
	result.append("  'exit' or 'quit': Terminates the program.\n");
	result.append("  'help' or '?': Displays tool command help.\n");
	result.append("  'reset': Resets the FTP system on the device.\n");
	result.append("  'dir' or 'ls': Retrieves a list of files from the device.\n");
	result.append("  'cd <DIR>': Changes the directory. <DIR> may also be .. or /.\n");
	result.append("  'get <FILE>': Retrieves the file <FILE> from the device.\n");
	result.append("  'rm <FILE>' or 'del <FILE>': Deletes the file <FILE> from the device.\n");
	result.append("    File deletion is experimental and may corrupt the file system on the device.\n");
	result.append("  'set': Displays a list of variables and their values.\n");
	result.append("  'set <VAR>': Displays the value of variable <VAR>.\n");
	result.append("  'set <VAR> <VALUE>': Sets the variable <VAR> to <VALUE>.\n");
	result.append("\n");
	result.append("FTP tool variables:\n");
	result.append("  'verbose': Output internal information. Corresponds to command setting -v.\n");
	result.append("  'debug': Output technical information. Corresponds to command setting -vv.\n");
	result.append("  'retries': Number of retries on error. Corresponds to command setting -x.\n");
	result.append("  'delay': Command delay in milliseconds. Corresponds to command setting -l.\n");
	result.append("  'allowdelete': If 'on', allows the experimental deletion of files.\n");
	result.append("  'continue': If 'on', appends content to partially downloaded files.\n");
	result.append("     If 'off', files are always overwritten completely.\n");
	result.append("  'interactive': Specifies program behavior for batch or interactive mode.\n");
	result.append("     This flag is set to 'on' if the program is started from a TTY, and to 'off'\n");
	result.append("     if input is being piped to the program. Normally you should not change this.\n");

	std::cout << result;
}

int main(int argc, char *argv[]) {

	std::vector<std::string> args;
	ArducomBaseParameters::convertCmdLineArgs(argc, argv, args);

	try {
		interactive = isatty(fileno(stdin));
		parameters.setFromArguments(args);

		ArducomMasterTransport* transport = parameters.validate();

		// initialize protocol
		ArducomMaster master(transport);

		std::vector<uint8_t> payload;
		std::vector<uint8_t> result;

		initSlaveFAT(master, transport);

		// command loop
		while (std::cin.good()) {

			prompt();

			try {
				std::string command;
				getline(std::cin, command);
				// stdin is a file or a pipe?
				if (!interactive)
					// print non-interactive command (for debugging)
					std::cout << command << std::endl;

				command = trim(command);

				// split command
				std::vector<std::string> parts;
				split(command, ' ', parts);

				if (parts.size() == 0)
					continue;
				else
				if ((parts.at(0) == "help") || (parts.at(0) == "?"))
					printUsageHelp();
				else
				if ((parts.at(0) == "quit") || (parts.at(0) == "exit"))
					break;
				else
				if (parts.at(0) == "reset") {
					initSlaveFAT(master, transport);
				} else
				if ((parts.at(0) == "ls") || (parts.at(0) == "dir")) {
					// directory listing data structure
					PACK(struct FileInfo {
						char name[13];
						uint8_t isDir;
						uint8_t size1;	// size is little-endian
						uint8_t size2;
						uint8_t size3;
						uint8_t size4;
						uint8_t lastWriteDate1;
						uint8_t lastWriteDate2;
						uint8_t lastWriteTime1;
						uint8_t lastWriteTime2;
					}) fileInfo;

					std::vector<FileInfo> fileInfos;
					payload.clear();	// no payload

					// rewind directory
					execute(master, ARDUCOM_FTP_COMMAND_REWIND, payload, transport->getDefaultExpectedBytes(), result, true);

					while (true) {
						// list next file
						execute(master, ARDUCOM_FTP_COMMAND_LISTFILES, payload, transport->getDefaultExpectedBytes(), result);

						// record received?
						if (result.size() > 0) {
							memcpy(&fileInfo, result.data(), sizeof(fileInfo));
							fileInfos.push_back(fileInfo);
						} else
							// no data - end of list
							break;
					}

					std::cout << std::endl;

					size_t totalDirs = 0;
					size_t totalFiles = 0;
					uint32_t totalSize = 0;

					// display file infos
					std::vector<FileInfo>::iterator it = fileInfos.begin();
					std::vector<FileInfo>::iterator ite = fileInfos.end();
					while (it != ite) {
						fileInfo = *it;
						fileInfo.name[12] = '\0';	// make sure there's no garbage
						std::cout << std::setfill(' ') << std::setw(16) << std::left << fileInfo.name;
						std::cout << std::setw(16) << std::right;
						if (fileInfo.isDir) {
							std::cout << "<DIR>";
							totalDirs++;
						} else {
							uint32_t fileSize = (fileInfo.size1 + (fileInfo.size2 << 8) + (fileInfo.size3 << 16) + (fileInfo.size4 << 24));
							std::cout << fileSize;
							totalFiles++;
							totalSize += fileSize;
						}
						int fatDate = fileInfo.lastWriteDate1 + (fileInfo.lastWriteDate2 << 8);
						int fatTime = fileInfo.lastWriteTime1 + (fileInfo.lastWriteTime2 << 8);
						int year = 1980 + (fatDate >> 9);
						int month = (fatDate >> 5) & 0XF;
						int day = fatDate & 0X1F;
						int hour = fatTime >> 11;
						int minute = (fatTime >> 5) & 0X3F;
						int second = 2*(fatTime & 0X1F);
						std::string timezoneName = "UTC";

						// convert time to UTC timestamp
						struct tm utc_tm;
						utc_tm.tm_year = year;
						utc_tm.tm_mon = month;
						utc_tm.tm_mday = day;
						utc_tm.tm_hour = hour;
						utc_tm.tm_min = minute;
						utc_tm.tm_sec = second;
						utc_tm.tm_isdst = 0;

						time_t utc_time = mktime(&utc_tm);
						if (utc_time >= 0) {
							// convert to local time
							struct tm local_tm = *gmtime(&utc_time);
							year = local_tm.tm_year;
							month = local_tm.tm_mon;
							day = local_tm.tm_mday;
							hour = local_tm.tm_hour;
							minute = local_tm.tm_min;
							second = local_tm.tm_sec;
							timezoneName = "local";
						}

						std::cout << "    " << std::setfill('0') << std::setw(4) << year;
						std::cout <<    "-" << std::setfill('0') << std::setw(2) << month;
						std::cout <<    "-" << std::setfill('0') << std::setw(2) << day;
						std::cout <<    " " << std::setfill('0') << std::setw(2) << hour;
						std::cout <<    ":" << std::setfill('0') << std::setw(2) << minute;
						std::cout <<    ":" << std::setfill('0') << std::setw(2) << second;
						std::cout << " " << timezoneName << std::endl;

						++it;
					}
					std::cout << std::setfill(' ') << std::endl;
					std::cout << std::setw(8) << std::right << totalFiles << " file(s),";
					std::cout << std::setw(15) << std::right << totalSize << " bytes total" << std::endl;
					std::cout << std::setw(8) << std::right << totalDirs << " folder(s) " << std::endl;

				} else
				if (parts.at(0) == "set") {
					setParameter(parts);
				} else
				if (parts.at(0) == "cd") {
					if (parts.size() == 1) {
						printPathComponents();
						std::cout << std::endl;
					} else if (parts.size() > 2) {
						std::cout << "Invalid input: cd expects only one argument" << std::endl;
					} else {
						bool exec = true;
						// cd into root?
						if (parts.at(1)[0] == '/') {
							pathComponents.clear();
						} else
						// cd up?
						if (parts.at(1) == "..") {
							exec = false;
							// only if we're at least one level down
							if (pathComponents.size() > 1) {
								// start at root, cd into sub directories
								std::vector<std::string> pathComps = pathComponents;
								pathComponents.clear();
								for (size_t p = 0; p < pathComps.size() - 1; p++) {
									payload.clear();
									// send command to change directory
									for (size_t i = 0; i < pathComps.at(p).length(); i++)
										payload.push_back(pathComps.at(p)[i]);
									execute(master, ARDUCOM_FTP_COMMAND_CHDIR, payload, transport->getDefaultExpectedBytes(), result);
									pathComponents.push_back(pathComps.at(p));
								}
							}
						} else
						// cd to local directory?
						if (parts.at(1) == ".") {
							// no need to execute
							exec = false;
						}

						if (exec) {
							payload.clear();
							// send command to change directory
							for (size_t i = 0; i < parts.at(1).length(); i++)
								payload.push_back(parts.at(1)[i]);
							execute(master, ARDUCOM_FTP_COMMAND_CHDIR, payload, transport->getDefaultExpectedBytes(), result);
	/*
							char dirname[13];
							size_t dirlen = result.size();
							if (dirlen > 12)
								dirlen = 12;
							std::cout << dirlen << std::endl;
							memcpy(dirname, result.data(), dirlen);
							dirname[dirlen] = '\0';
	*/
							// store current directory name
							pathComponents.push_back(parts.at(1));
						}
					}
				} else
				if (parts.at(0) == "get") {
					if (parts.size() == 1) {
						std::cout << "Invalid input: get expects a file name as argument" << std::endl;
					} else if (parts.size() > 2) {
						std::cout << "Invalid input: get expects only one argument" << std::endl;
					} else {
						payload.clear();
						// send command to open the file
						for (size_t i = 0; i < parts.at(1).length(); i++)
							payload.push_back(parts.at(1)[i]);
						execute(master, ARDUCOM_FTP_COMMAND_OPENREAD, payload, transport->getDefaultExpectedBytes(), result, true);

						// the result is the file size
						if (result.size() < 4) {
							std::cout << "Error: device did not send a proper file size" << std::endl;
						} else {
							PACK(struct FileSize {
								uint8_t size1;	// size is little-endian
								uint8_t size2;
								uint8_t size3;
								uint8_t size4;
							}) fileSize;
							memcpy(&fileSize, result.data(), sizeof(fileSize));
							int32_t totalSize = (fileSize.size1 + (fileSize.size2 << 8) + (fileSize.size3 << 16) + (fileSize.size4 << 24));
							std::cout << "File size: " << totalSize << " bytes" << std::endl;
							if (totalSize < 0)  {
								std::cout << "File size is negative, cannot download" << std::endl;
								continue;	// next command
							}
							int fd;
							int32_t position = -1;
							bool fileExists = false;
							// check whether the file already exists on the master
							if (access(parts.at(1).c_str(), F_OK) != -1) {
								fileExists = true;
								// open local file for reading
								fd = open(parts.at(1).c_str(), O_RDONLY);
								if (fd < 0) {
									throw std::runtime_error((std::string("Unable to read output file: ") + parts.at(1)).c_str());
								}

								// get file size; this is the position to continue reading from
								struct stat st;

								if (stat(parts.at(1).c_str(), &st) == 0)
									position = st.st_size;
								else {
									throw std::runtime_error((std::string("Unable to get file size: ") + parts.at(1)).c_str());
								}

								close(fd);
							}

							// overwrite or continue?
							if (parameters.continueFile && (position >= 0) && (position < totalSize)) {
								std::cout << "Appending data to existing file (to overwrite, use 'set continue off')" << std::endl;
								// open local file for appending
								fd = open(parts.at(1).c_str(), O_APPEND | O_WRONLY | O_BINARY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
								if (fd < 0) {
									throw std::runtime_error((std::string("Unable to create output file: ") + parts.at(1)).c_str());
								}
							} else {
								if (fileExists) {
									if (!interactive) {
										std::cout << "Cannot overwrite in non-interactive mode; cancelling" << std::endl;
										continue;
									} else {
										// interactive
										std::cout << "Overwrite existing file y/N (to append data, use 'set continue on')? ";
										std::string input;
										getline(std::cin, input);
										if (input != "y") {
											std::cout << "Download cancelled" << std::endl;
											continue;
										}
									}
								}
								// open local file for writing; create from scratch
								fd = open(parts.at(1).c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
								if (fd < 0) {
									throw std::runtime_error((std::string("Unable to create output file: ") + parts.at(1)).c_str());
								}
								// start downloading from beginning
								position = 0;
							}

							std::cout << "Remaining: " << totalSize - position << " bytes" << std::endl;
							if (totalSize - position == 0)  {
								std::cout << "File seems to be complete" << std::endl;
								continue;	// next command
							}

							// file read loop
							while (true) {
								// send current seek position
								payload.clear();
								payload.push_back((uint8_t)position);
								payload.push_back((uint8_t)(position >> 8));
								payload.push_back((uint8_t)(position >> 16));
								payload.push_back((uint8_t)(position >> 24));

								// this command can be resent in case of errors (idempotent)
								execute(master, ARDUCOM_FTP_COMMAND_READFILE, payload, transport->getDefaultExpectedBytes(), result, true);

								position += result.size();

								// write data to local file
								write(fd, result.data(), result.size());

								// show "progress bar" only in interactive mode
								if (interactive)
									printProgress(totalSize, position, 50);

								if (position >= totalSize)
									break;
							}

							std::cout << std::endl;
							needEndl = false;

							// send command to close the file
							payload.clear();
							execute(master, ARDUCOM_FTP_COMMAND_CLOSEFILE, payload, transport->getDefaultExpectedBytes(), result, true);

							// close local file
							close(fd);
							std::cout << "Download complete." << std::endl;
						}
					}
				} else
				if ((parts.at(0) == "rm") || (parts.at(0) == "del")) {
					if (parts.size() == 1) {
						std::cout << "Invalid input: rm and del expect a file name as argument" << std::endl;
					} else if (parts.size() > 2) {
						std::cout << "Invalid input: rm and del expect only one argument" << std::endl;
					} else {
						if (!parameters.allowDelete) {
							std::cout << "Warning: Deleting files is possibly buggy and can corrupt your SD card!" << std::endl;
							std::cout << "'Type 'set allowdelete on' if you want to delete anyway." << std::endl;
						} else {
							payload.clear();
							// send command to delete the file
							for (size_t i = 0; i < parts.at(1).length(); i++)
								payload.push_back(parts.at(1)[i]);
							execute(master, ARDUCOM_FTP_COMMAND_DELETE, payload, transport->getDefaultExpectedBytes(), result);
						}
					}
				} else {
					std::cout << "Unknown command: " << parts.at(0) << std::endl;
				}
			} catch (const std::exception& e) {
				if (needEndl)
					std::cout << std::endl;
				needEndl = false;

				print_what(e);

				// non-interactive mode causes immediate exit on errors
				// this way an exit code can be queried by scripts
				if (!interactive)
					exit(master.lastError);
			}
		}	// while (true)

	} catch (const std::exception& e) {
		if (needEndl)
			std::cout << std::endl;
		needEndl = false;

		print_what(e);
		exit(1);
	}

	return 0;
}
