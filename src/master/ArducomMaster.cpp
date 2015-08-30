#include <exception>
#include <stdexcept>
#include <iostream>

#include "../slave/lib/Arducom.h"
#include "ArducomMaster.h"

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

void ArducomMaster::send(uint8_t command, uint8_t* buffer, uint8_t size, int retries) {
	uint8_t data[size + 2];
	data[0] = command;
	data[1] = size;
	for (uint8_t i = 0; i < size; i++) {
		data[i + 2] = buffer[i];
	}
	if (this->verbose) {
		std::cout << "Sending bytes: " ;
		this->printBuffer(data, size + 2);
		std::cout << std::endl;
	}
	this->transport->send(data, size + 2, retries);
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
		std::cout << "Response command code is ok" << std::endl;
	}
	// read code byte
	uint8_t code = this->transport->readByte();
	if (this->verbose) {
		std::cout << "Code byte: ";
		this->printBuffer(&code, 1);
		std::cout << std::endl;
	}
	*size = 0;
	// read payload into the buffer; up to expected bytes or returned bytes, whatever is lower
	for (uint8_t i = 0; (i < expected) && (i < (code & 0b00111111)); i++) {
		destBuffer[i] = this->transport->readByte();
		*size = i + 1;
	}
	if (this->verbose) {
		std::cout << "Received payload: ";
		this->printBuffer(destBuffer, *size);
		std::cout << std::endl;
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
