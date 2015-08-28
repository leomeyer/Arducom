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

#include "ArducomMasterI2C.h"

ArducomMasterTransportI2C::ArducomMasterTransportI2C(std::string filename, int slaveAddress) {
	this->filename = filename;
	this->slaveAddress = slaveAddress;
	this->pos = -1;
}

void ArducomMasterTransportI2C::init(void) {
	// initialize the I2C bus
	if ((this->fileHandle = open(this->filename.c_str(), O_RDWR)) < 0) {
		perror("Failed to open I2C device");
		throw std::runtime_error("Failed to open I2C device: " + this->filename);
	}
	
	if (ioctl(this->fileHandle, I2C_SLAVE, this->slaveAddress) < 0) {
		perror("Unable to get bus access to talk to slave");
		throw std::runtime_error("Unable to get bus access to talk to slave");
	}
}

void ArducomMasterTransportI2C::send(uint8_t* buffer, uint8_t size, int retries) {
	if (size > I2C_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to send exceeds I2C block size limit");
	int my_retries = retries;
	while (true) {
		if ((write(this->fileHandle, buffer, size)) != size) {
			if (my_retries <= 0) {
				perror("Error sending data to I2C slave");
				throw std::runtime_error("Error sending data to I2C slave");
			} else {
				my_retries--;
				continue;
			}
		}
		break;
	}
}

void ArducomMasterTransportI2C::request(uint8_t expectedBytes) {
	if (expectedBytes > I2C_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to receive exceeds I2C block size limit");
	if (read(this->fileHandle, this->buffer, expectedBytes) != expectedBytes) {
		perror("Unable to read from I2C slave");
		throw std::runtime_error("Unable to read from I2C slave");
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

size_t ArducomMasterTransportI2C::getMaximumCommandSize(void) {
	return I2C_BLOCKSIZE_LIMIT;
}

size_t ArducomMasterTransportI2C::getDefaultExpectedBytes(void) {
	return I2C_BLOCKSIZE_LIMIT;
}

void ArducomMasterTransportI2C::printBuffer(void) {
	ArducomMaster::printBuffer(this->buffer, I2C_BLOCKSIZE_LIMIT);
}
