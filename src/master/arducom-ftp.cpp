// Arducom FTP master

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <stdio.h>
#include <exception>
#include <stdexcept>
#include <cstdint>
#include <unistd.h>
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

#include "../slave/lib/Arducom.h"
#include "../slave/lib/ArducomFTP.h"
#include "ArducomMaster.h"
#include "ArducomMasterI2C.h"
#include "ArducomMasterSerial.h"

std::string transportType;
std::string device;
int deviceAddress = 0;
int baudrate = 9600;
bool verbose = false;
long delayMs = 0;
int retries = 0;
uint8_t commandBase = ARDUCOM_FTP_DEFAULT_COMMANDBASE;
bool continueFile = true;
bool useChecksum = true;
bool interactive = true;		// if false (piping input) errors cause immediate exit
bool allowDelete = false;

std::vector<std::string> pathComponents;

bool needEndl = false;		// flag: cout << endl before printing messages

// trim from start
static inline std::string &ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
        return ltrim(rtrim(s));
}

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

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

void execute(ArducomMaster &master, uint8_t command, std::vector<uint8_t> &params, uint8_t expectedBytes, std::vector<uint8_t> &result, bool canResend = false) {
	uint8_t buffer[255];
	uint8_t size;
	int m_retries = retries;
	uint8_t errorInfo;
	bool sent = false;
	int delay = delayMs;

	// retry loop
	while (m_retries >= 0) {
		if (!sent || canResend) {
			// send command
			master.send(command + commandBase, useChecksum, params.data(), params.size(), retries);
			sent = true;
		}

receive:		
		// wait for the specified delay
		usleep(delay * 1000);
		size = 0;
		
		uint8_t result = master.receive(expectedBytes, buffer, &size, &errorInfo);

		// no error?
		if (result == ARDUCOM_OK)
			break;
	
		// convert error code to string
		char errstr[21];
		sprintf(errstr, "%d", result);
		
		// convert info code to string
		char numstr[21];
		sprintf(numstr, "%d", errorInfo);
		
		// special case: if NO_DATA has been received, give the slave more time to react
		// without resending the message
		if (result == ARDUCOM_NO_DATA) {
			m_retries--;
			if (m_retries > 0) {
				if (verbose) {
					std::cout << "Error " << errstr << " (" << numstr << "), " << m_retries << " retries left" << std::endl;
				}
				// increase delay with each occurrence of this error
				delay *= 2;
				goto receive;
			}
		}		
		
		switch (result) {
		case ARDUCOM_NO_DATA:
				throw std::runtime_error((std::string("Device error ") + errstr + ": No data (not enough data sent or command not yet processed, try to increase delay -l or number of retries -x)").c_str());
		case ARDUCOM_COMMAND_UNKNOWN:
			throw std::runtime_error((std::string("Command unknown (") + errstr + "): " + numstr).c_str());
		case ARDUCOM_TOO_MUCH_DATA:
			throw std::runtime_error((std::string("Too much data (") + errstr + "); expected bytes: " + numstr).c_str());
		case ARDUCOM_PARAMETER_MISMATCH:
			throw std::runtime_error((std::string("Parameter mismatch (") + errstr + "); expected bytes: " + numstr).c_str());
		case ARDUCOM_BUFFER_OVERRUN:
			throw std::runtime_error((std::string("Buffer overrun (") + errstr + "); buffer size is: " + numstr).c_str());
		case ARDUCOM_CHECKSUM_ERROR:
			throw std::runtime_error((std::string("Checksum error (") + errstr + "); calculated checksum: " + numstr).c_str());
		case ARDUCOM_FUNCTION_ERROR:
			switch (errorInfo) {
			case ARDUCOM_FTP_SDCARD_ERROR: throw std::runtime_error((std::string("FTP error ") + numstr + ": SD card unavailable").c_str());
			case ARDUCOM_FTP_SDCARD_TYPE_UNKNOWN: throw std::runtime_error((std::string("FTP error ") + numstr + ": SD card type unknown").c_str());
			case ARDUCOM_FTP_FILESYSTEM_ERROR: throw std::runtime_error((std::string("FTP error ") + numstr + ": SD card file system error").c_str());
			case ARDUCOM_FTP_NOT_INITIALIZED: throw std::runtime_error((std::string("FTP error ") + numstr + ": FTP system not initialized").c_str());
			case ARDUCOM_FTP_MISSING_FILENAME: throw std::runtime_error((std::string("FTP error ") + numstr + ": Required file name is missing").c_str());
			case ARDUCOM_FTP_NOT_A_DIRECTORY: throw std::runtime_error((std::string("FTP error ") + numstr + ": Not a directory").c_str());		
			case ARDUCOM_FTP_FILE_OPEN_ERROR: throw std::runtime_error((std::string("FTP error ") + numstr + ": Error opening file").c_str());		
			case ARDUCOM_FTP_READ_ERROR: throw std::runtime_error((std::string("FTP error ") + numstr + ": Read error").c_str());		
			case ARDUCOM_FTP_FILE_NOT_OPEN: throw std::runtime_error((std::string("FTP error ") + numstr + ": File not open").c_str());		
			case ARDUCOM_FTP_POSITION_INVALID: throw std::runtime_error((std::string("FTP error ") + numstr + ": File seek position invalid").c_str());		
			case ARDUCOM_FTP_CANNOT_DELETE: throw std::runtime_error((std::string("FTP error ") + numstr + ": Cannot delete this file or folder (LFN?)").c_str());		
			default: throw std::runtime_error((std::string("FTP error ") + numstr + ": Unknown error").c_str());		
			}
		}
	}
	
	result.clear();

	for (size_t i = 0; i < size; i++) 
		result.push_back(buffer[i]);
}

void printPathComponents(void) {
	std::vector<std::string>::const_iterator it = pathComponents.begin();
	while (it != pathComponents.end()) {
		std::cout << *it;
		std::vector<std::string>::const_iterator prev_it = it;
		it++;
		if ((*prev_it != "/") && (it != pathComponents.end()))
			std::cout << "/";
	}
}

void prompt(void) {
	printPathComponents();
	std::cout << "> ";
}

void initSlaveFAT(ArducomMaster &master, ArducomMasterTransport *transport) {
	std::vector<uint8_t> params;
	std::vector<uint8_t> result;

	// send INIT message
	execute(master, ARDUCOM_FTP_COMMAND_INIT, params, transport->getDefaultExpectedBytes(), result);
	
	struct __attribute__((packed)) CardInfo {
		char cardType[4];
		uint8_t fatType;
		uint8_t size1;	// size is little-endian
		uint8_t size2;
		uint8_t size3;
		uint8_t size4;
	} cardInfo;

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
	needEndl = true;
}

void setVariable(std::vector<std::string> parts, bool print = true) {
	bool printOnly = false;
	if (parts.size() < 2) {
		printOnly = true;
		parts.push_back("");	// push dummy
	}
	bool found = false;
	if (parts.at(1) == "allowdelete" || printOnly) {
		if (parts.size() > 2) {
			if (parts.at(2) == "on")
				allowDelete = true;
			else
			if (parts.at(2) == "off")
				allowDelete = false;
			else
				throw std::invalid_argument("Expected 'on' or 'off'");
		}
		if (print)
			std::cout << "set allowdelete " << (allowDelete ? "on" : "off") << std::endl;
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
				continueFile = true;
			else
			if (parts.at(2) == "off")
				continueFile = false;
			else
				throw std::invalid_argument("Expected 'on' or 'off'");
		}
		if (print)
			std::cout << "set continue " << (continueFile ? "on" : "off") << std::endl;
		found = true;
	}
	if (parts.at(1) == "retries" || printOnly) {
		if (parts.size() > 2) {
			try {
				int m_retries = std::stoi(parts.at(2));
				if (m_retries < 0)
					throw std::invalid_argument("");
				retries = m_retries;
			} catch (std::exception& e) {
				throw std::invalid_argument("Expected non-negative number of retries");
			}
		}
		if (print)
			std::cout << "set retries " << retries << std::endl;
		found = true;
	}
	if (parts.at(1) == "delay" || printOnly) {
		if (parts.size() > 2) {
			try {
				long m_delayMs = std::stol(parts.at(2));
				if (m_delayMs < 0)
					throw std::invalid_argument("");
				delayMs = m_delayMs;
			} catch (std::exception& e) {
				throw std::invalid_argument("Expected non-negative delay in ms");
			}
		}
		if (print)
			std::cout << "set delay " << delayMs << std::endl;
		found = true;
	}
	
	if (!found)
		throw std::invalid_argument("Variable name unknown: " + parts.at(1));
}

int main(int argc, char *argv[]) {
	
	interactive = isatty(fileno(stdin));
	
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
			if (args.at(i) == "-n") {
				useChecksum = false;
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

		if (retries < 0)
			throw std::invalid_argument("Number of retries must not be negative (argument -x)");

		// initialize protocol
		ArducomMaster master(transport, verbose);
		
		std::vector<uint8_t> params;
		std::vector<uint8_t> result;
		
		initSlaveFAT(master, transport);

		// command loop
		while (true) {
			
			prompt();
			
			try {
				std::string command;
				getline(std::cin, command);
				// stdin is a file or a pipe?
				if (!isatty(fileno(stdin)))
					std::cout << command << std::endl;
				
				command = trim(command);
				
				// split parameters
				std::vector<std::string> parts;
				split(command, ' ', parts);
				
				if (parts.size() == 0)
					continue;
				else
				if ((parts.at(0) == "quit") || (parts.at(0) == "exit"))
					break;
				else
				if (parts.at(0) == "reset") {
					initSlaveFAT(master, transport);
				} else
				if ((parts.at(0) == "ls") || (parts.at(0) == "dir")) {
					// directory listing data structure
					struct __attribute__((packed)) FileInfo {
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
					} fileInfo;
					
					std::vector<FileInfo> fileInfos;
					params.clear();	// no parameters

					// rewind directory
					execute(master, ARDUCOM_FTP_COMMAND_REWIND, params, transport->getDefaultExpectedBytes(), result);
										
					while (true) {
						// list next file
						execute(master, ARDUCOM_FTP_COMMAND_LISTFILES, params, transport->getDefaultExpectedBytes(), result);
						
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
					while (it != fileInfos.end()) {
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
						
						std::cout << "    " << std::setfill('0') << std::setw(4) << year;
						std::cout <<    "-" << std::setfill('0') << std::setw(2) << month;
						std::cout <<    "-" << std::setfill('0') << std::setw(2) << day;
						std::cout <<    " " << std::setfill('0') << std::setw(2) << hour;
						std::cout <<    ":" << std::setfill('0') << std::setw(2) << minute;
						std::cout <<    ":" << std::setfill('0') << std::setw(2) << second;
						std::cout << std::endl;
						
						it++;
					}
					std::cout << std::setfill(' ') << std::endl;
					std::cout << std::setw(8) << std::right << totalFiles << " file(s),";
					std::cout << std::setw(15) << std::right << totalSize << " bytes total" << std::endl;				
					std::cout << std::setw(8) << std::right << totalDirs << " folder(s) " << std::endl;
					
				} else 
				if (parts.at(0) == "set") {
					setVariable(parts);
				} else
				if (parts.at(0) == "cd") {
					if (parts.size() == 1) {
						printPathComponents();
						std::cout << std::endl;
					} else if (parts.size() > 2) {
						std::cout << "Error: cd expects only one argument" << std::endl;
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
									params.clear();
									// send command to change directory
									for (size_t i = 0; i < pathComps.at(p).length(); i++)
										params.push_back(pathComps.at(p)[i]);
									execute(master, ARDUCOM_FTP_COMMAND_CHDIR, params, transport->getDefaultExpectedBytes(), result);
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
							params.clear();
							// send command to change directory
							for (size_t i = 0; i < parts.at(1).length(); i++)
								params.push_back(parts.at(1)[i]);
							execute(master, ARDUCOM_FTP_COMMAND_CHDIR, params, transport->getDefaultExpectedBytes(), result);
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
						std::cout << "Error: get expects a file name as argument" << std::endl;
					} else if (parts.size() > 2) {
						std::cout << "Error: get expects only one argument" << std::endl;
					} else {
						
						params.clear();
						// send command to open the file
						for (size_t i = 0; i < parts.at(1).length(); i++)
							params.push_back(parts.at(1)[i]);
						execute(master, ARDUCOM_FTP_COMMAND_OPENREAD, params, transport->getDefaultExpectedBytes(), result);
						
						// the result is the file size
						if (result.size() < 4) {
							std::cout << "Error: device did not send a proper file size" << std::endl;
						} else {
							struct __attribute__((packed)) FileSize {
								uint8_t size1;	// size is little-endian
								uint8_t size2;
								uint8_t size3;
								uint8_t size4;
							} fileSize;
							memcpy(&fileSize, result.data(), sizeof(fileSize));
							int32_t totalSize = (fileSize.size1 + (fileSize.size2 << 8) + (fileSize.size3 << 16) + (fileSize.size4 << 24));
							std::cout << "File size: " << totalSize << std::endl;
							if (totalSize < 0)  {
								std::cout << "File size is negative, cannot download" << std::endl;
								continue;	// next command
							}
							int fd;
							int32_t position = 0;
							bool fileExists = false;
							// check whether the file already exists on the master
							if (access(parts.at(1).c_str(), F_OK) != -1) {
								fileExists = true;
								// open local file for reading
								fd = open(parts.at(1).c_str(), O_RDONLY);
								if (fd < 0) {
									perror("Unable to read output file");
									throw std::runtime_error((std::string("Unable to read output file: ") + parts.at(1)).c_str());
								}

								// get file size; this is the position to continue reading from
								struct stat st; 

								if (stat(parts.at(1).c_str(), &st) == 0)
									position = st.st_size;
								else {
									perror("Unable to read get file size");
									throw std::runtime_error((std::string("Unable to read get file size: ") + parts.at(1)).c_str());
								}
								
								close(fd);
							}
							
							// overwrite or continue?
							if (continueFile && (position > 0) && (position < totalSize)) {
								std::cout << "Appending data to existing file (to overwrite, use 'set continue off')" << std::endl;
								// open local file for appending
								fd = open(parts.at(1).c_str(), O_APPEND | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
								if (fd < 0) {
									perror("Unable to append to output file");
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
								fd = open(parts.at(1).c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
								if (fd < 0) {
									perror("Unable to create output file");
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
								params.clear();
								params.push_back((uint8_t)position);
								params.push_back((uint8_t)(position >> 8));
								params.push_back((uint8_t)(position >> 16));
								params.push_back((uint8_t)(position >> 24));

								// this command can be resent in case of errors (idempotent)
								execute(master, ARDUCOM_FTP_COMMAND_READFILE, params, transport->getDefaultExpectedBytes(), result, true);
								
								position += result.size();
								
								// write data to local file
								write(fd, result.data(), result.size());
								
								printProgress(totalSize, position, 50);
								
								if (position >= totalSize)
									break;
							}
							
							std::cout << std::endl;
							needEndl = false;
							
							// send command to close the file
							params.clear();
							execute(master, ARDUCOM_FTP_COMMAND_CLOSEFILE, params, transport->getDefaultExpectedBytes(), result);						

							// close local file
							close(fd);
						}
					}
				} else
				if ((parts.at(0) == "rm") || (parts.at(0) == "del")) {
					if (parts.size() == 1) {
						std::cout << "Error: rm and del expect a file name as argument" << std::endl;
					} else if (parts.size() > 2) {
						std::cout << "Error: rm and del expect only one argument" << std::endl;
					} else {
						if (!allowDelete) {
							std::cout << "Warning: Deleting files is possibly buggy and can corrupt your SD card!" << std::endl;
							std::cout << "'Type 'set allowdelete on' if you want to delete anyway." << std::endl;
						} else {
							params.clear();
							// send command to delete the file
							for (size_t i = 0; i < parts.at(1).length(); i++)
								params.push_back(parts.at(1)[i]);
							execute(master, ARDUCOM_FTP_COMMAND_DELETE, params, transport->getDefaultExpectedBytes(), result);
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
