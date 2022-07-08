// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#include "ArducomMasterSerial.h"

#include <exception>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <sys/param.h>
#include <termios.h>
#include <cstring>
#include <openssl/sha.h>

#include "../slave/lib/Arducom/Arducom.h"

struct baud_mapping {
	long baud;
	speed_t speed;
};

static struct baud_mapping baud_lookup_table [] = {
  { 50,     B50 },
  { 75,     B75 },
  { 110,    B110 },
  { 134,    B134 },
  { 150,    B150 },
  { 200,    B200 },
  { 300,    B300 },
  { 600,    B600 },
  { 1200,   B1200 },
  { 2400,   B2400 },
  { 4800,   B4800 },
  { 9600,   B9600 },
  { 19200,  B19200 },
  { 38400,  B38400 },
  { 57600,  B57600 },
  { 115200, B115200 },
  { 230400, B230400 },
  { 0,      0 }
};

static speed_t serial_baud_lookup(long baud)
{
  struct baud_mapping *map = baud_lookup_table;

  while (map->baud) {
    if (map->baud == baud)
      return map->speed;
    map++;
  }

  throw std::invalid_argument("Unsupported baud rate");
}

ArducomMasterTransportSerial::ArducomMasterTransportSerial() {
	this->pos = -1;
}

void ArducomMasterTransportSerial::init(ArducomBaseParameters* parameters) {
	this->parameters = parameters;
	
	this->filename = parameters->device;
	this->baudrate = parameters->baudrate;
	
#ifndef ARDUCOM__NO_LOCK_MECHANISM
	// calculate SHA1 hash of the filename
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((const unsigned char*)filename.c_str(), filename.size(), hash);
	
	// IPC semaphore key is the first four bytes of the hash
	this->semkey = *(int*)&hash;
#endif
	
	// std::cout << "Semaphore key:" << this->semkey << std::endl;
	
	// Special case for devices that use USB over serial:
	// To account for resets of the Arduino that might occur because of the
	// usage of the DTR line the --initDelay is set if it has not been
	// set manually.
	if (!parameters->initDelaySetManually) {
		if ((this->filename.find("ttyUSB") != std::string::npos)
			|| (this->filename.find("ttyACM") != std::string::npos)) {
			parameters->initDelayMs = ARDUCOM_DEFAULT_INIT_DELAY_MS;
		}
	}
	
	// default protocol: 8N1
	uint8_t byteSize = 8;
	uint8_t parity = 0;
	uint8_t stopBits = 1;

	// initialize the serial device
	struct termios tty;

	int fd = open(this->filename.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		throw_system_error("Failed to open serial device", this->filename.c_str());
	}
	
	if (this->parameters->debug)
		std::cout << "Opened serial port " << this->filename.c_str() << std::endl;
	
	if (this->parameters->initDelayMs > 0) {
		if (this->parameters->debug)
			std::cout << "Initialization delay: " << this->parameters->initDelayMs << "ms; use --initDelay to reduce" << std::endl;
		// sleep for the specified time
		timespec sleeptime;
		sleeptime.tv_sec = this->parameters->initDelayMs / 1000;
		sleeptime.tv_nsec = (this->parameters->initDelayMs % 1000) * 1000000L;
		nanosleep(&sleeptime, nullptr);
	}
	
	memset(&tty, 0, sizeof(tty));
	if (tcgetattr(fd, &tty) != 0) {
		throw_system_error("Error getting serial device attributes (is the device valid?)");
	}

	if (this->baudrate > 0) {
		// calculate call constant from baud rate
		cfsetospeed(&tty, serial_baud_lookup(this->baudrate));
		cfsetispeed(&tty, serial_baud_lookup(this->baudrate));
	}

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty.c_iflag &= ~IGNBRK;         // ignore break signal
	tty.c_lflag = 0;                // no signaling chars, no echo,
									// no canonical processing
	tty.c_oflag = 0;                // no remapping, no delays
	tty.c_cc[VMIN] = 1; //(this->parameters->timeoutMs < 1 ? 1 : 0);	// block if no timeout specified
	tty.c_cc[VTIME] = (this->parameters->timeoutMs < 1 ? 0 : (this->parameters->timeoutMs / 100));	// read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_cflag |= (HUPCL | CLOCAL | CREAD);// ignore modem controls,
									// enable reading
	if (parity == 0)
		tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
	else
	if (parity == 1) {
		tty.c_cflag |= PARENB | PARODD;		// parity odd
	} else
	if (parity == 2) {
		tty.c_cflag &= ~(PARODD);		// parity even
		tty.c_cflag |= PARENB;
	}

	if (stopBits == 2) {
		tty.c_cflag |= CSTOPB;
	} else
	if (stopBits == 1) {
		tty.c_cflag &= ~CSTOPB;
	}

	if (byteSize == 5) {
		tty.c_cflag &= ~CSIZE;
		tty.c_cflag |= CS5;
	} else
	if (byteSize == 6) {
		tty.c_cflag &= ~CSIZE;
		tty.c_cflag |= CS6;
	} else
	if (byteSize == 7) {
		tty.c_cflag &= ~CSIZE;
		tty.c_cflag |= CS7;
	} else
	if (byteSize == 8) {
		tty.c_cflag &= ~CSIZE;
		tty.c_cflag |= CS8;
	}

	tty.c_cflag &= ~CRTSCTS;
	// tty.c_cflag &= ~HUPCL;          // disable hang up on close (toggling DTR)
	
	cfmakeraw(&tty);

	// initialization delay specified?
	if (this->parameters->initDelayMs > 0) {
		if (this->parameters->debug)
			std::cout << "Opened serial port. Initialization delay: " << this->parameters->initDelayMs << "ms; use --initDelay to reduce" << std::endl;
		// sleep for the specified time
		struct timespec sleeptime;
		sleeptime.tv_sec = 0;
		sleeptime.tv_nsec = this->parameters->initDelayMs * 1000000;
		nanosleep(&sleeptime, nullptr);
	}
	
	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		throw_system_error("Error setting serial device attributes (is the device valid?)");
	}
	
	tcflush(fd, TCIOFLUSH);
	// clear input buffer (TCIOFLUSH doesn't seem to work)
	uint8_t buffer;
	while (read(fd, &buffer, 1) >= 0) {}

	this->fileHandle = fd;

	if (this->parameters->debug)
		std::cout << "Serial port initialized successfully." << std::endl;
}

void ArducomMasterTransportSerial::sendBytes(uint8_t* buffer, uint8_t size, int retries) {
	if (size > SERIAL_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to send exceeds serial block size limit");

	// clear buffers
	tcflush(this->fileHandle, TCIOFLUSH);

	for (uint8_t i = 0; i < size; i++) {
		int my_retries = retries;
repeat:
		if ((write(this->fileHandle, &buffer[i], 1)) != 1) {
			if (my_retries <= 0) {
				throw_system_error("Error sending data to serial device");
			} else {
				my_retries--;
				goto repeat;
			}
		}
		if (this->parameters->debug) {
			std::cout << "Byte sent: ";
			ArducomMaster::printBuffer(&buffer[i], 1);
			std::cout << std::endl;
		}
	}
	fsync(this->fileHandle);
}

uint8_t ArducomMasterTransportSerial::readByteInternal(uint8_t* buffer) {
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
			
			throw_system_error("Unable to read from serial device");
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
	
	throw Arducom::TimeoutException("Timeout reading from serial device");
}

void ArducomMasterTransportSerial::request(uint8_t expectedBytes) {
	if (expectedBytes > SERIAL_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to receive exceeds serial block size limit");
	uint8_t pos = 0;
	memset(&this->buffer, 0, SERIAL_BLOCKSIZE_LIMIT);

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
			if (pos > SERIAL_BLOCKSIZE_LIMIT)
				throw std::runtime_error("Error: number of received bytes exceeds serial block size limit");
			}
		}
	}
	this->pos = 0;
}

uint8_t ArducomMasterTransportSerial::readByte(void) {
	if (this->pos < 0)
		throw std::runtime_error("Can't read: Data must be requested first");
	if (pos >= SERIAL_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Can't read: Too many bytes requested");
	return this->buffer[this->pos++];
}

void ArducomMasterTransportSerial::done() {
	// nothing to do (file remains open)
}

uint8_t ArducomMasterTransportSerial::getMaximumCommandSize(void) {
	return SERIAL_BLOCKSIZE_LIMIT;
}

uint8_t ArducomMasterTransportSerial::getDefaultExpectedBytes(void) {
	return SERIAL_BLOCKSIZE_LIMIT;
}

int ArducomMasterTransportSerial::getSemkey(void) {
#ifdef ARDUCOM__NO_LOCK_MECHANISM
	return 0;
#else
	return this->semkey;
#endif
}

void ArducomMasterTransportSerial::printBuffer(void) {
	ArducomMaster::printBuffer(this->buffer, SERIAL_BLOCKSIZE_LIMIT);
}
