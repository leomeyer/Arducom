#include <exception>
#include <stdexcept>
#include <iostream>

#include "../slave/lib/Arducom.h"
#include "ArducomMaster.h"

static uint8_t calculateChecksum(uint8_t commandByte, uint8_t code, uint8_t* data, uint8_t dataSize) {
	int16_t sum = commandByte + code;
	// carry overflow?
	if (sum > 255) {
		sum = (sum & 0xFF) + 1;
	}
	for (uint8_t i = 0; i < dataSize; i++) {
		sum += data[i];
		if (sum > 255) {
			sum = (sum & 0xFF) + 1;
		}
	}
	// return two's complement of result
	return ~(uint8_t)sum;
}

void ArducomMaster::printBuffer(uint8_t* buffer, uint8_t size, bool noHex, bool noRAW) {
	if (size == 0)
		return;
	if (!noHex) {
		char outBuf[4];
		for (uint8_t j = 0; j < size; j++) {
			sprintf(outBuf, "%02X", buffer[j]);
			std::cout << outBuf;
		}
	}
	if (!noHex && !noRAW)
		std::cout << ' ';
	if (!noRAW) {
		for (uint8_t j = 0; j < size; j++) {
			// if no hex, output as raw
			if (noHex || ((buffer[j] >= ' ') && (buffer[j] <= 127)))
				std::cout << buffer[j];
			else
				std::cout << '.';
		}
	}
}

void ArducomMaster::send(uint8_t command, bool checksum, uint8_t* buffer, uint8_t size, int retries) {
	uint8_t data[size + (checksum ? 3 : 2)];
	data[0] = command;
	data[1] = size | (checksum ? 0x80 : 0);
	for (uint8_t i = 0; i < size; i++) {
		data[i + (checksum ? 3 : 2)] = buffer[i];
	}
	if (checksum)
		data[2] = calculateChecksum(data[0], data[1], &data[3], size);
	if (this->verbose) {
		std::cout << "Sending bytes: " ;
		this->printBuffer(data, size + (checksum ? 3 : 2));
		std::cout << std::endl;
	}
	this->transport->send(data, size + (checksum ? 3 : 2), retries);
	this->lastCommand = command;
}

uint8_t ArducomMaster::receive(uint8_t expected, uint8_t* destBuffer, uint8_t* size, uint8_t *errorInfo) {
	if (this->lastCommand > 127)
		throw std::runtime_error("Cannot receive without sending a command first");
	
	this->transport->request(expected);
	if (verbose) {
		std::cout << "Receive buffer: ";
		this->transport->printBuffer();
		std::cout << std::endl;
	}
	// read first byte of the reply
	uint8_t resultCode = this->transport->readByte();
	// error?
	if (resultCode == ARDUCOM_ERROR_CODE) {
		if (this->verbose)
			std::cout << "Received error code 0xff" << std::endl;
		resultCode = this->transport->readByte();
		if (this->verbose) {
			std::cout << "Error: ";
			this->printBuffer(&resultCode, 1);
		}
		*errorInfo = this->transport->readByte();
		if (this->verbose) {
			std::cout << ", additional info: ";
			this->printBuffer(errorInfo, 1);
			std::cout << std::endl;
		}
		return resultCode;
	} else
	if (resultCode == 0) {
		throw std::runtime_error("Communication error: Didn't receive a valid reply");
	}

	// device reacted to different command (result command code has highest bit set)?
	if (resultCode != (this->lastCommand | 0x80)) {
		this->invalidResponse(resultCode & ~0x80);
	}
	if (this->verbose) {
		std::cout << "Response command code is ok." << std::endl;
	}
	// read code byte
	uint8_t code = this->transport->readByte();
	uint8_t length = (code & 0b00111111);
	bool checksum = (code & 0x80) == 0x80;
	if (this->verbose) {
		std::cout << "Code byte: ";
		this->printBuffer(&code, 1);
		std::cout << " Payload length is " << (int)length << " bytes.";
		if (checksum)
			std::cout << " Verifying data using checksum.";
		std::cout << std::endl;
	}
	if (length > ARDUCOM_BUFFERSIZE) 
		throw std::runtime_error("Protocol error: Returned payload length exceeds maximum buffer size");

	// checksum expected?
	uint8_t checkbyte = (checksum ? this->transport->readByte() : 0);
	
	*size = 0;
	// read payload into the buffer; up to expected bytes or returned bytes, whatever is lower
	for (uint8_t i = 0; (i < expected) && (i < length); i++) {
		destBuffer[i] = this->transport->readByte();
		*size = i + 1;
	}
	if (this->verbose) {
		std::cout << "Received payload: ";
		this->printBuffer(destBuffer, *size);
		std::cout << std::endl;
	}
	if (checksum) {
		uint8_t ckbyte = calculateChecksum(resultCode, code, destBuffer, *size);
		if (ckbyte != checkbyte) {
			*errorInfo = checkbyte;
			return ARDUCOM_CHECKSUM_ERROR;
		}
	}
	return ARDUCOM_OK;
}
	
void ArducomMaster::invalidResponse(uint8_t commandByte) {
	uint8_t expectedReply = this->lastCommand | 0x80;
	std::cout << "Expected reply to command ";
	this->printBuffer(&this->lastCommand, 1);
	std::cout << " (";
	this->printBuffer(&expectedReply, 1);
	std::cout << ") but received ";
	this->printBuffer(&commandByte, 1);
	std::cout << std::endl;
	throw std::runtime_error("Invalid response");
}
