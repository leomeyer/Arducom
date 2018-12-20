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
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <cstring>

#include "../slave/lib/Arducom/Arducom.h"

struct baud_mapping {
	long baud;
	int speed;
};

static struct baud_mapping baud_lookup_table [] = {
  { 110,    CBR_110 },
  { 300,    CBR_300 },
  { 600,    CBR_600 },
  { 1200,   CBR_1200 },
  { 2400,   CBR_2400 },
  { 4800,   CBR_4800 },
  { 9600,   CBR_9600 },
  { 19200,  CBR_19200 },
  { 38400,  CBR_38400 },
  { 57600,  CBR_57600 },
  { 115200, CBR_115200 },
  { 0,      0 }
};

static int serial_baud_lookup(long baud)
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
	this->baudrate = serial_baud_lookup(parameters->baudrate);

#ifndef __NO_LOCK_MECHANISM
	// calculate SHA1 hash of the filename
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((const unsigned char*)filename.c_str(), filename.size(), hash);

	// IPC semaphore key is the first four bytes of the hash
	this->semkey = *(int*)&hash;
	// std::cout << "Semaphore key:" << this->semkey << std::endl;
#endif

	// default protocol: 8N1
	uint8_t byteSize = 8;
	int parity = 0;
	uint8_t stopBits = 1;

	// initialize the serial device
	DCB dcb;

	HANDLE hPort = CreateFileA(this->filename.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (hPort == INVALID_HANDLE_VALUE) {
		throw_system_error("Failed to open serial device", this->filename.c_str());
	}

	if (this->parameters->debug)
		std::cout << "Opened serial port." << this->filename.c_str() << std::endl;

	if (this->parameters->initDelayMs > 0) {
		if (this->parameters->debug)
			std::cout << "Initialization delay: " << this->parameters->initDelayMs << "ms; use --initDelay to reduce" << std::endl;
		Sleep(this->parameters->initDelayMs);
	}

	if (!GetCommState(hPort, &dcb))
		throw_system_error("Error getting serial device attributes (is the device valid?)");

	if (this->baudrate > 0) {
		dcb.BaudRate = this->baudrate;
	}

	dcb.ByteSize = byteSize;
	if (parity == 0)
		dcb.Parity = NOPARITY;
	else
	if (parity == 1)
		dcb.Parity = ODDPARITY;
	else
	if (parity == 2)
		dcb.Parity = EVENPARITY;
	else
		throw_system_error("Invalid parity value", std::to_string(parity).c_str());
	dcb.StopBits = stopBits;

	if (!SetCommState(hPort, &dcb)) {
		throw_system_error("Error setting serial device attributes (is the device valid?)");
	}

	COMMTIMEOUTS timeouts = { 0, //interval timeout. 0 = not used
		0, // read multiplier
		1, // read constant (milliseconds)
		0, // Write multiplier
		0  // Write Constant
	};

	SetCommTimeouts(hPort, &timeouts);

	this->fileHandle = hPort;

	// clear input buffer
	uint8_t buffer;
	DWORD bytesRead;
	while (ReadFile(this->fileHandle, &buffer, 1, &bytesRead, nullptr) > 0) {
		if (bytesRead == 0) break;
	}

	timeouts = { 0, //interval timeout. 0 = not used
		0, // read multiplier
		(DWORD)parameters->timeoutMs, // read constant (milliseconds)
		0, // Write multiplier
		0  // Write Constant
	};

	SetCommTimeouts(hPort, &timeouts);

	if (this->parameters->debug)
		std::cout << "Serial port initialized successfully." << std::endl;
}

void ArducomMasterTransportSerial::sendBytes(uint8_t* buffer, uint8_t size, int retries) {
	if (size > SERIAL_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to send exceeds serial block size limit");

	for (uint8_t i = 0; i < size; i++) {
		int my_retries = retries;
repeat:
		DWORD length;
		if (WriteFile(this->fileHandle, &buffer[i], 1, &length, nullptr) == 0) {
			if (my_retries <= 0) {
				throw_system_error("Error sending data to serial device", nullptr, GetLastError());
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
}

uint8_t ArducomMasterTransportSerial::readByteInternal(uint8_t* buffer) {
	int timeout = this->parameters->timeoutMs;
	while (timeout >= 0) {
		DWORD bytesRead;
		if (ReadFile(this->fileHandle, buffer, 1, &bytesRead, nullptr) == 0)
			throw_system_error("Unable to read from serial device", nullptr, GetLastError());
		// timeout?
		if (bytesRead <= 0) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				if (this->parameters->timeoutMs > 0) {
					if (timeout <= 0)
						break;
					timeout--;
				}
				// in case of no timeout, repeat infinitely
				continue;
			}

			throw_system_error("Unable to read from serial device", nullptr, GetLastError());
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

	return *buffer;
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

size_t ArducomMasterTransportSerial::getMaximumCommandSize(void) {
	return SERIAL_BLOCKSIZE_LIMIT;
}

size_t ArducomMasterTransportSerial::getDefaultExpectedBytes(void) {
	return SERIAL_BLOCKSIZE_LIMIT;
}

int ArducomMasterTransportSerial::getSemkey(void) {
#ifdef __NO_LOCK_MECHANISM
	return 0;
#else
	return this->semkey;
#endif
}

void ArducomMasterTransportSerial::printBuffer(void) {
	ArducomMaster::printBuffer(this->buffer, SERIAL_BLOCKSIZE_LIMIT);
}
