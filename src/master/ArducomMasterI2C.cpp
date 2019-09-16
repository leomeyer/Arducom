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

uint8_t ArducomMasterTransportI2C::readByteInternal(uint8_t* buffer) {
	int timeout = this->parameters->timeoutMs;
	while (timeout >= 0) {
		int bytesRead = read(this->fileHandle, buffer, 1);
		if (bytesRead < 0) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				if (this->parameters->timeoutMs > 0) {
					if (timeout <= 0)
						break;
					timeout--;
					// sleep for a ms
					timespec sleeptime;
					sleeptime.tv_sec = 0;
					sleeptime.tv_nsec = 1000000;
					nanosleep(&sleeptime, nullptr);
				}
				// in case of no timeout, repeat infinitely
				continue;
			}
			
			throw_system_error("Unable to read from I2C");
		} else 
		if (bytesRead > 1) {
			throw std::runtime_error("Big trouble! Read returned more than one byte");
		}
		
		if (this->parameters->debug) {
			std::cout << "Byte received: ";
			ArducomMaster::printBuffer(buffer, 1);
			std::cout << std::endl;
		}
		
		return *buffer;
	}
	
	throw Arducom::TimeoutException("Timeout reading from I2C");
}

void ArducomMasterTransportI2C::request(uint8_t expectedBytes) {
	if (expectedBytes > I2C_BLOCKSIZE_LIMIT) {
		throw std::runtime_error("Error: number of bytes to receive exceeds I2C block size limit");
	}
	uint8_t pos = 0;
	memset(&this->buffer, 0, I2C_BLOCKSIZE_LIMIT);

	// read the first byte
	uint8_t resultCode = this->readByteInternal(&this->buffer[pos++]);
	if (expectedBytes > 1) {
		// inspect first byte of the reply
		// error?
		if (resultCode == ARDUCOM_ERROR_CODE) {
			// read the next two bytes (error code plus error info)
			this->readByteInternal(&this->buffer[pos++]);
			if (expectedBytes > 2)
				this->readByteInternal(&this->buffer[pos++]);
		} else {
			// read code byte
			uint8_t code = this->readByteInternal(&this->buffer[pos++]);
			uint8_t length = (code & 0b00111111);

//			std::cout << "Expecting: " << (int)length << " bytes" << std::endl;
			// read payload into the buffer; up to expected bytes or returned bytes, whatever is lower
			bool checksum = (code & 0x80) == 0x80;
			while ((pos < expectedBytes) && (pos < length + (checksum ? 3 : 2))) {
				this->readByteInternal(&this->buffer[pos++]);
			if (pos > I2C_BLOCKSIZE_LIMIT)
				throw std::runtime_error("Error: number of received bytes exceeds serial block size limit");
			}
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
