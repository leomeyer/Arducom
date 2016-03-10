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

#include "ArducomMasterI2C.h"

ArducomMasterTransportI2C::ArducomMasterTransportI2C(std::string filename, int slaveAddress) {
	this->filename = filename;
	this->slaveAddress = slaveAddress;

	this->fileHandle = 0;
	this->pos = -1;
	
	// calculate SHA1 hash of the filename
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((const unsigned char*)filename.c_str(), filename.size(), hash);
	
	// IPC semaphore key is the first four bytes of the hash
	this->semkey = *(int*)&hash;
}

ArducomMasterTransportI2C::~ArducomMasterTransportI2C() {
}

void ArducomMasterTransportI2C::init(ArducomBaseParameters* parameters) {
	this->parameters = parameters;

	// Special case for devices that use I2C:
	// Set the command delay if it has not been set manually.
	if (!parameters->delaySetManually) {
		parameters->delayMs = 10;
	}
}

void ArducomMasterTransportI2C::send(uint8_t* buffer, uint8_t size, int retries) {
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
	if (read(this->fileHandle, this->buffer, expectedBytes) != expectedBytes) {
		throw_system_error("Unable to read from I2C slave");
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
	return this->semkey;
}

void ArducomMasterTransportI2C::printBuffer(void) {
	ArducomMaster::printBuffer(this->buffer, I2C_BLOCKSIZE_LIMIT);
}
