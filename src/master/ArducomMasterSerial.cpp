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

#include "ArducomMasterSerial.h"
#include "../slave/lib/Arducom.h"

ArducomMasterTransportSerial::ArducomMasterTransportSerial(std::string filename, int baudrate, int timeout) {
	this->filename = filename;
	this->baudrate = baudrate;
	this->timeout = timeout;
	this->pos = -1;
}

void ArducomMasterTransportSerial::init(void) {
	// default protocol: 8N1
	uint8_t byteSize = 8;
	uint8_t parity = 0;
	uint8_t stopBits = 1;
	
	// initialize the serial device
	struct termios tty;

	int fd = open(this->filename.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0) {
		perror("Failed to open serial device");
//		printf("error %d opening %s: %s\n", errno, portName, strerror (errno));
		throw std::runtime_error("Failed to open serial device: " + this->filename);
	}

	memset (&tty, 0, sizeof tty);
	if (tcgetattr(fd, &tty) != 0) {
		perror("tcgetattr");
		throw std::runtime_error("tcgetattr");
	}

	if (this->baudrate > 0) {
		// TODO calculate call constant from baud rate
		cfsetospeed(&tty, B9600);
		cfsetispeed(&tty, B9600);
	}

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty.c_iflag &= ~IGNBRK;         // ignore break signal
	tty.c_lflag = 0;                // no signaling chars, no echo,
									// no canonical processing
	tty.c_oflag = 0;                // no remapping, no delays
	tty.c_cc[VMIN]  = (this->timeout < 1 ? 1 : 0);	// block if no timeout specified
	tty.c_cc[VTIME] = (this->timeout < 1 ? 0 : (this->timeout / 100));	// read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
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

	if (tcsetattr (fd, TCSANOW, &tty) != 0) {
		perror("tcsetattr");
		throw std::runtime_error("tcsetattr");
	}

	this->fileHandle = fd;
}

void ArducomMasterTransportSerial::send(uint8_t* buffer, uint8_t size, int retries) {
	if (size > SERIAL_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to send exceeds serial block size limit");
	int my_retries = retries;
	while (true) {
		if ((write(this->fileHandle, buffer, size)) != size) {
			if (my_retries <= 0) {
				perror("Error sending data to serial device");
				throw std::runtime_error("Error sending data to serial device");
			} else {
				my_retries--;
				continue;
			}
		}
		break;
	}
}

uint8_t ArducomMasterTransportSerial::readByteInternal(uint8_t* buffer) {
	int bytesRead = read(this->fileHandle, buffer, 1);
	if (bytesRead < 0) {
		perror("Unable to read from serial device");
		throw std::runtime_error("Unable to read from serial device");
	} else 
	if (bytesRead != 1) {
		throw std::runtime_error("Timeout reading from serial device");
	} else 
	
	return *buffer;
}

void ArducomMasterTransportSerial::request(uint8_t expectedBytes) {
	if (expectedBytes > SERIAL_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to receive exceeds serial block size limit");
	int pos = 0;
	memset(&this->buffer, 0, SERIAL_BLOCKSIZE_LIMIT);
	
	// read the first byte
	uint8_t resultCode = this->readByteInternal(&this->buffer[pos++]);
	// inspect first byte of the reply
	// error?
	if (resultCode == ARDUCOM_ERROR_CODE) {
		// read the next two bytes (error code plus error info)
		this->readByteInternal(&this->buffer[pos++]);
		this->readByteInternal(&this->buffer[pos++]);
	} else {
		// read code byte
		uint8_t code = this->readByteInternal(&this->buffer[pos++]);
		// read payload into the buffer; up to expected bytes or returned bytes, whatever is lower
		for (uint8_t i = 0; (i < expectedBytes) && (i < (code & 0b00111111)); i++) {
			this->readByteInternal(&this->buffer[pos++]);
		if (pos > SERIAL_BLOCKSIZE_LIMIT)
			throw std::runtime_error("Error: number of received bytes exceeds serial block size limit");
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

size_t ArducomMasterTransportSerial::getMaximumCommandSize(void) {
	return SERIAL_BLOCKSIZE_LIMIT;
}

size_t ArducomMasterTransportSerial::getDefaultExpectedBytes(void) {
	return SERIAL_BLOCKSIZE_LIMIT;
}

void ArducomMasterTransportSerial::printBuffer(void) {
	ArducomMaster::printBuffer(this->buffer, SERIAL_BLOCKSIZE_LIMIT);
}
