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

	this->fileHandle = 0;
	this->pos = -1;
	// IPC key is the beginning of the SHA-1 hash of "ArducomI2C"
	this->semkey = 0x681f4fbc;
	this->semid = 0;
}

ArducomMasterTransportI2C::~ArducomMasterTransportI2C() {
}

void ArducomMasterTransportI2C::init(void) {
	// nothing to do for I2C (semaphore checking is done in send)
}

void ArducomMasterTransportI2C::unlock() {
	// close the file if it's open
	if (this->fileHandle > 0) {
		close(this->fileHandle);
		this->fileHandle = 0;
	}

	if (!this->hasLock)
		return;
	// decrease the semaphore
	struct sembuf semops;
	semops.sem_num = 0;
	semops.sem_op = -1;
	semops.sem_flg = SEM_UNDO;
	if (semop(this->semid, &semops, 1) < 0) {
		// error decreasing semaphore
		perror("Error decreasing I2C semaphore");
		throw std::runtime_error("Error decreasing I2C semaphore");
	}
	this->hasLock = false;
}

void ArducomMasterTransportI2C::send(uint8_t* buffer, uint8_t size, int retries) {
	if (size > I2C_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to send exceeds I2C block size limit");
		
	// mutex not yet opened?
	if (this->semid == 0) {
		// acquire interprocess semaphore to avoid contention
		// allow access for processes running under all users
		this->semid = semget(this->semkey, 1, IPC_CREAT | 0666);
		if (this->semid < 0) {
			perror("Unable to create or open semaphore");
			throw std::runtime_error("Unable to create or open semaphore");
		}
	}
	
	struct sembuf semops;
	
/*	
	// debug
	int sval;
	if (sem_getvalue(this->mutex, &sval) == 0) {
		std::cout << "Semaphore getvalue: " << sval << std::endl;
	} else {
		perror("unable to sem_getvalue on semaphore");
	}
*/
	// avoid increasing the semaphore more than once
	if (!this->hasLock) {

		// semaphore has been created or opened, wait until it becomes available
		// allow a wait time of one second (I2C operations are generally fast, so this should be enough)
		semops.sem_num = 0;
		semops.sem_op = 0;		// wait until the semaphore becomes zero
		semops.sem_flg = IPC_NOWAIT;
		int counter = 1000;
		while (counter > 0) {
			// try to acquire resource
			if (semop(this->semid, &semops, 1) < 0) {
				// still locked?
				if (errno == EAGAIN) {
					counter--;
					if (counter <= 0)
						throw std::runtime_error("Timeout waiting for I2C semaphore");
					// wait for a ms
					usleep(1000000);
					continue;
				} else {
					// other error acquiring semaphore
					perror("Error acquiring I2C semaphore");
					throw std::runtime_error("Error acquiring I2C semaphore");
				}
			}
			// ok
			break;
		}

		// increase the semaphore (allocate the resource)
		semops.sem_num = 0;
		semops.sem_op = 1;
		semops.sem_flg = SEM_UNDO;
		if (semop(this->semid, &semops, 1) < 0) {
			// error increasing semaphore
			perror("Error increasing I2C semaphore");
			throw std::runtime_error("Error increasing I2C semaphore");
		}

		this->hasLock = true;
	}
	
	// initialize the I2C bus
	if ((this->fileHandle = open(this->filename.c_str(), O_RDWR)) < 0) {
		this->unlock();
		perror("Failed to open I2C device");
		throw std::runtime_error("Failed to open I2C device: " + this->filename);
	}
	
	if (ioctl(this->fileHandle, I2C_SLAVE, this->slaveAddress) < 0) {
		// release semaphore
		this->unlock();
		perror("Unable to get bus access to talk to slave");
		throw std::runtime_error("Unable to get bus access to talk to slave");
	}
	
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

	// the semaphore is now being held by this process. It is released after data has been received.
	// Programs using this API always MUST use send and request in pairs and call done!
}

void ArducomMasterTransportI2C::request(uint8_t expectedBytes) {
	if (expectedBytes > I2C_BLOCKSIZE_LIMIT) {
		this->unlock();
		throw std::runtime_error("Error: number of bytes to receive exceeds I2C block size limit");
	}
	if (read(this->fileHandle, this->buffer, expectedBytes) != expectedBytes) {
		this->unlock();
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

void ArducomMasterTransportI2C::done(void) {
	// release semaphore
	this->unlock();
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
