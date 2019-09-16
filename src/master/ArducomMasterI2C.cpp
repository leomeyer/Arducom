// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#include "ArducomMasterI2C.h"

#if defined(__CYGWIN__) || defined(WIN32)
	#warning I2C is not supported on Windows
#else

#include <string.h>
#include <exception>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>
#include <unistd.h>
#include <iostream>
#include <openssl/sha.h>

ArducomMasterTransportI2C::ArducomMasterTransportI2C() {
	this->fileHandle = 0;
	this->pos = -1;
}

void ArducomMasterTransportI2C::init(ArducomBaseParameters* parameters) {
	this->parameters = parameters;

	this->filename = parameters->device;
	this->slaveAddress = parameters->deviceAddress;
	
#ifdef __NO_LOCK_MECHANISM
	// calculate SHA1 hash of the filename
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((const unsigned char*)filename.c_str(), filename.size(), hash);
	
	// IPC semaphore key is the first four bytes of the hash
	this->semkey = *(int*)&hash;
#endif
	// Special case for devices that use I2C:
	// Set the command delay if it has not been set manually.
	if (!parameters->delaySetManually) {
		parameters->delayMs = 10;
	}
}

void ArducomMasterTransportI2C::sendBytes(uint8_t* buffer, uint8_t size, int retries) {
	if (size > I2C_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to send exceeds I2C block size limit");


	// initialize the I2C bus
	if ((this->fileHandle = open(this->filename.c_str(), O_RDWR)) < 0) {
		throw_system_error("Failed to open I2C device: ", this->filename.c_str());
	}

	if (ioctl(this->fileHandle, I2C_SLAVE, this->slaveAddress) < 0) {
		throw_system_error("Unable to get device access to talk to I2C slave");
	}

	int my_retries = retries;
	while (true) {
		if ((write(this->fileHandle, buffer, size)) != size) {
			if (my_retries <= 0) {
				throw_system_error("Error sending data to I2C slave");
			} else {
				my_retries--;
				continue;
			}
		}
		break;
	}
}

void ArducomMasterTransportI2C::request(uint8_t expectedBytes) {
	if (expectedBytes > I2C_BLOCKSIZE_LIMIT) {
		throw std::runtime_error("Error: number of bytes to receive exceeds I2C block size limit");
	}
	memset(&this->buffer, 0, I2C_BLOCKSIZE_LIMIT);

	int bytesRead;
	// read available data from I2C with timeout
	int timeout = this->parameters->timeoutMs;
	while (timeout >= 0) {
		bytesRead = read(this->fileHandle, buffer, I2C_BLOCKSIZE_LIMIT);
		// error?
		if (bytesRead < 0) {
			if (this->parameters->timeoutMs > 0) {
				if (timeout <= 0)
						throw Arducom::TimeoutException("Timeout reading from I2C");
				timeout--;
				// sleep for a ms
				timespec sleeptime;
				sleeptime.tv_sec = 0;
				sleeptime.tv_nsec = 1000000;
				nanosleep(&sleeptime, nullptr);
			}
			// in case of no timeout, repeat infinitely
			continue;
		} else {
			if (this->parameters->debug) {
				std::cout << "Data received after " << (this->parameters->timeoutMs - timeout) << " ms: ";
				ArducomMaster::printBuffer(buffer, bytesRead);
				std::cout << std::endl;
			}
			break;
		}
	}
  if (bytesRead <= 0)
		throw_system_error("Unable to read from I2C");

	// inspect first byte of the reply
	uint8_t resultCode = this->buffer[0];
	// error?
	if (resultCode == ARDUCOM_ERROR_CODE) {
		// expect two bytes more (error code plus error info)
		if (bytesRead < 3)
			throw Arducom::TimeoutException("Not enough data");
	} else {
		// read code byte
		uint8_t code = this->buffer[1];
		uint8_t length = (code & 0b00111111);
		bool checksum = (code & 0x80) == 0x80;
		if ((bytesRead < length + (checksum ? 3 : 2))) {
			throw Arducom::TimeoutException("Not enough data");
		}
	}
	this->pos = 0;
}

uint8_t ArducomMasterTransportI2C::readByte(void) {
	if (this->pos < 0)
		throw std::runtime_error("Can't read: Data must be requested first");
	if (pos >= I2C_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Can't read: Too many bytes requested");
	return this->buffer[this->pos++];
}

void ArducomMasterTransportI2C::done(void) {
	// close the file if it's open
	if (this->fileHandle > 0) {
		close(this->fileHandle);
		this->fileHandle = 0;
	}
}

size_t ArducomMasterTransportI2C::getMaximumCommandSize(void) {
	return I2C_BLOCKSIZE_LIMIT;
}

size_t ArducomMasterTransportI2C::getDefaultExpectedBytes(void) {
	return I2C_BLOCKSIZE_LIMIT;
}

int ArducomMasterTransportI2C::getSemkey(void) {
#ifdef __NO_LOCK_MECHANISM
	return 0;
#else
	return this->semkey;
#endif
}

void ArducomMasterTransportI2C::printBuffer(void) {
	ArducomMaster::printBuffer(this->buffer, I2C_BLOCKSIZE_LIMIT);
}

#endif		// !WIN32
