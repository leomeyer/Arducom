#include <Arduino.h>
#include <avr/eeprom.h>

#include <Arducom.h>

void ArducomTransport::reset(void) {
	this->status = NO_DATA;
	this->size = 0;
}

int8_t ArducomTransport::hasData(void) {
	if (this->status != HAS_DATA)
		return -1;
	return this->size;
}

int8_t ArducomTransport::sendError(uint8_t errorCode, uint8_t info) {
	// signal error
	this->data[0] = ARDUCOM_ERROR_CODE;
	this->data[1] = errorCode;
	this->data[2] = info;
	this->size = 3;
	this->status = READY_TO_SEND;
}

/******************************************************************************************	
* Arducom main class implementation
******************************************************************************************/

Arducom::Arducom(ArducomTransport* transport, Print* debugPrint, uint16_t receiveTimeout) {
	this->transport = transport;
	this->debug = debugPrint;
	this->origDebug = debugPrint;	// remember original pointer (debug is switched off by NULLing debug)
	this->currentCommand = 0;
	this->lastDataSize = -1;
	this->receiveTimeout = receiveTimeout;
	this->lastReceiveTime = 0;
}
	
uint8_t Arducom::addCommand(ArducomCommand* cmd) {
	if (cmd->commandCode > 126)
		return ARDUCOM_COMMANDCODE_INVALID;
	// check whether the command is already in the list
	ArducomCommand* command = this->list;
	while (command != 0) {
		if (command->commandCode == cmd->commandCode)
			return ARDUCOM_COMMAND_ALREADY_EXISTS;
		command = command->next;
	}
	cmd->next = this->list;
	this->list = cmd;
	return ARDUCOM_OK;
}
	
uint8_t Arducom::doWork(void) {
	// transport data handling
	uint8_t result = this->transport->doWork();
	if (result != ARDUCOM_OK)
		return result;

	int8_t dataSize = this->transport->hasData();
	uint8_t errorInfo = 0;
	
	// performance optimization: test commands only if a new data size is reported
	// expect at least two bytes: command and code byte
	if ((dataSize != this->lastDataSize) && (dataSize > 1)) {
		this->lastDataSize = dataSize;
		// reset timeout counter
		this->lastReceiveTime = millis();
		// the first byte is always the command byte
		uint8_t commandByte = this->transport->data[0];
		uint8_t code = this->transport->data[1];
		// check whether the specified number of bytes has already been received
		// the lower six bits of the code denote the payload size
		if (dataSize - 2 < (code & 0b00111111))
			// not enough data
			return ARDUCOM_OK;
		// the command has been fully received
		uint8_t destBuffer[ARDUCOM_BUFFERSIZE];
		// payload data size is always without command and code
		dataSize -= 2;
		// go through commands to find a match
		ArducomCommand* command = this->list;
		result = ARDUCOM_COMMAND_UNKNOWN;
		while (command != 0) {
			// set errorInfo to the command code; if no command is found this code is returned
			errorInfo = commandByte;
			if (command->commandCode == commandByte) {
				// clear error info before executing a command (if handle() does not set it no garbage will be returned)
				errorInfo = 0;
				bool handle = true;
				// if specified, the number of payload bytes must match the number of expected bytes
				if (command->expectedBytes >= 0) {
					// has the expected number of bytes been received?
					if (dataSize != command->expectedBytes) {
						result = ARDUCOM_PARAMETER_MISMATCH;
						errorInfo = command->expectedBytes;		// return number of expected bytes
						handle = false;							// do not handle the command
					}
				}
				
				if (handle) {
					if (this->debug) {
						this->debug->print(F("Cmd: "));
						this->debug->print((int)commandByte);
						this->debug->print(F(" Params: "));
						this->debug->println((int)dataSize);
					}
					// let the command do the work
					result = command->handle(this, &this->transport->data[2], &dataSize, &destBuffer[2], ARDUCOM_BUFFERSIZE - 2, &errorInfo);
					if (this->debug) {
						this->debug->print(F("Ret: "));
						this->debug->print((int)result);
						this->debug->print(F(" Params: "));
						this->debug->println((int)dataSize);
					}
				}
				break;
			}
			command = command->next;
		}
		// reset data size cache (start over)
		this->lastDataSize = -1;
		// check result code
		if (result != ARDUCOM_OK) {
			// an error has occurred; send it back to the master
			if (this->debug) {
				this->debug->print(F("Error: "));
				this->debug->print((int)result);
				this->debug->print(F(" Info: "));
				this->debug->println((int)errorInfo);
			}
			this->transport->sendError(result, errorInfo);
			this->transport->reset();
			return ARDUCOM_COMMAND_ERROR;
		}
		// the command has been handled, send data back
		// set MSB of command byte
		destBuffer[0] = command->commandCode | 0x80;
		// prepare return code: lower six bits are length of payload
		destBuffer[1] = (dataSize & 0b00111111);
		this->transport->send(destBuffer, 2 + dataSize);
	} else {
		// new data is not available
		// check whether receive timeout is up
		if ((this->lastReceiveTime > 0) && (this->receiveTimeout > 0)) {
			if (millis() - this->lastReceiveTime > this->receiveTimeout) {
				// timeout is up, reset the transport
				this->transport->reset();
				return ARDUCOM_TIMEOUT;
			}
		}
	}
	return ARDUCOM_OK;
}

void Arducom::setFlags(uint8_t flags) {
	if ((flags & ARDUCOM_FLAG_ENABLEDEBUG) == ARDUCOM_FLAG_ENABLEDEBUG) {
		this->debug = this->origDebug;
	} else {
		this->debug = 0;
	}
}
	
uint8_t Arducom::getFlags(void) {
	uint8_t result = 0;
	if (this->debug != 0)
		result |= ARDUCOM_FLAG_ENABLEDEBUG;
	
	return result;
}
	
/******************************************************************************************	
* Arducom command implementations
******************************************************************************************/
	
int8_t ArducomVersionCommand::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {

	uint8_t pos = 0;
	// this method expects one optional flag byte
	// flag byte provided?
	if (*dataSize > 0) {
		uint8_t flags = *((uint8_t*)dataBuffer);
		arducom->setFlags(flags);
	}
	
	// assume that destBuffer is big enough
	destBuffer[pos++] = arducom->VERSION;
	// send uptime in milliseconds (four bytes)
	unsigned long *millisDest = (unsigned long *)&destBuffer[pos];
	*millisDest = millis();
	pos += 4;
	// send config flags (one byte)
	uint8_t *flagDest = (uint8_t *)&destBuffer[pos];
	*flagDest = arducom->getFlags();
	pos += 1;
	// copy data to destination buffer
	uint8_t c = 0;
	while (this->data[c]) {
		destBuffer[pos] = this->data[c];
		pos++;
		c++;
		if (pos >= maxBufferSize) {
			*errorInfo = maxBufferSize;
			return ARDUCOM_BUFFER_OVERRUN;
		}
	}

	*dataSize = pos;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMByte::ArducomWriteEEPROMByte(uint8_t commandCode) : ArducomCommand(commandCode, 3) {}
	
int8_t ArducomWriteEEPROMByte::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects three bytes from the master; a two-byte address and a one-byte value
	#define EXPECTED_PARAM_BYTES 3
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_update_byte((uint8_t*)address, dataBuffer[2]);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMByte::ArducomReadEEPROMByte(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMByte::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address from the master
	#define EXPECTED_PARAM_BYTES 2
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	destBuffer[0] = eeprom_read_byte((uint8_t*)address);
	*dataSize = 1;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt16::ArducomWriteEEPROMInt16(uint8_t commandCode) : ArducomCommand(commandCode, 4) {}
	
int8_t ArducomWriteEEPROMInt16::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects six bytes from the master; a two-byte address and a two-byte value
	#define EXPECTED_PARAM_BYTES 4
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_update_word((uint16_t*)address, dataBuffer[2] + (dataBuffer[3] << 8));
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt16::ArducomReadEEPROMInt16(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt16::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address from the master
	#define EXPECTED_PARAM_BYTES 2
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_read_block(destBuffer, (uint16_t*)address, 2);
	*dataSize = 2;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt32::ArducomWriteEEPROMInt32(uint8_t commandCode) : ArducomCommand(commandCode, 6) {}
	
int8_t ArducomWriteEEPROMInt32::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects six bytes from the master; a two-byte address and a four-byte value
	#define EXPECTED_PARAM_BYTES 6
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_update_block((const void *)&dataBuffer[2], (uint16_t*)address, 4);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt32::ArducomReadEEPROMInt32(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt32::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address from the master
	#define EXPECTED_PARAM_BYTES 2
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_read_block(destBuffer, (uint16_t*)address, 4);
	*dataSize = 4;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt64::ArducomWriteEEPROMInt64(uint8_t commandCode) : ArducomCommand(commandCode, 10) {}
	
int8_t ArducomWriteEEPROMInt64::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects ten bytes from the master; a two-byte address and an eight-byte value
	#define EXPECTED_PARAM_BYTES 10
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_update_block((const void *)&dataBuffer[2], (uint16_t*)address, 8);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt64::ArducomReadEEPROMInt64(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt64::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address from the master
	#define EXPECTED_PARAM_BYTES 2
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_read_block(destBuffer, (uint16_t*)address, 8);
	*dataSize = 8;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMBlock::ArducomWriteEEPROMBlock(uint8_t commandCode) : ArducomCommand(commandCode, -1) {}
	
int8_t ArducomWriteEEPROMBlock::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address from the master plus at least one byte to write
	#define EXPECTED_PARAM_BYTES 3
	if (*dataSize < EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	// write remaining data to the EEPROM
	eeprom_update_block((const void *)&dataBuffer[2], (uint16_t*)address, *dataSize - 2);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMBlock::ArducomReadEEPROMBlock(uint8_t commandCode) : ArducomCommand(commandCode, 3) {}
	
int8_t ArducomReadEEPROMBlock::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address from the master plus a length byte
	#define EXPECTED_PARAM_BYTES 3
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	uint8_t length = dataBuffer[2];
	if (length > maxBufferSize) {
		*errorInfo = maxBufferSize;
		return ARDUCOM_BUFFER_OVERRUN;
	}
	eeprom_read_block(destBuffer, (uint16_t*)address, length);
	*dataSize = length;
	return ARDUCOM_OK;
}