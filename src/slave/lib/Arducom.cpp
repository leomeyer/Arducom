#include <Arduino.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>

#include <Arducom.h>

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

/******************************************************************************************	
* Arducom transport base class implementation
******************************************************************************************/

void ArducomTransport::reset(void) {
	this->status = NO_DATA;
	this->size = 0;
}

int8_t ArducomTransport::hasData(void) {
	if (this->status != HAS_DATA)
		return -1;
	return this->size;
}

/******************************************************************************************	
* Arducom main class implementation
******************************************************************************************/

Arducom::Arducom(ArducomTransport* transport, Print* debugPrint, uint16_t receiveTimeout) {
	this->transport = transport;
	this->debug = debugPrint;
	this->origDebug = debugPrint;	// remember original pointer (debug is switched off by NULLing debug)
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
		// the first byte is the command byte
		uint8_t commandByte = this->transport->data[0];
		// the first byte is the code byte which contains the length
		uint8_t code = this->transport->data[1];
		// check whether the specified number of bytes has already been received
		// the lower six bits of the code denote the payload size
		if (dataSize - 2 < (code & 0b00111111))
			// not enough data
			return ARDUCOM_OK;
		// checksum expected? highest bit of the code byte
		bool checksum = (code >> 7);
		// check whether checksum byte arrived
		if (checksum) {
			if (dataSize - 3 < (code & 0b00111111))
				// not enough data
				return ARDUCOM_OK;
		}
		// the command has been fully received
		uint8_t destBuffer[ARDUCOM_BUFFERSIZE];
		// payload data size is always without command and code and optional checksum byte
		dataSize -= (checksum ? 3 : 2);
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
					if (checksum) {
						// calculate and verify checksum 
						uint8_t cksum = calculateChecksum(commandByte, code, (uint8_t*)&this->transport->data[3], dataSize);
						if (cksum != this->transport->data[2]) {
							result = ARDUCOM_CHECKSUM_ERROR;
							errorInfo = cksum;
							break;
						}
					}
					
					if (this->debug) {
						this->debug->print(F("Cmd: "));
						this->debug->print((int)commandByte);
						this->debug->print(F(" Params: "));
						this->debug->println((int)dataSize);
					}
					// let the command do the work
					result = command->handle(this, &this->transport->data[(checksum ? 3 : 2)], &dataSize, 
						&destBuffer[(checksum ? 3 : 2)], ARDUCOM_BUFFERSIZE - (checksum ? 3 : 2), &errorInfo);
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
			// send error code back
			destBuffer[0] = ARDUCOM_ERROR_CODE;
			destBuffer[1] = result;
			destBuffer[2] = errorInfo;
			// error codes are not checksummed
			this->transport->send(destBuffer, 3);
			return ARDUCOM_COMMAND_ERROR;
		}
		// the command has been handled, send data back
		// set MSB of command byte
		destBuffer[0] = command->commandCode | 0x80;
		// prepare return code: lower six bits are length of payload
		destBuffer[1] = (dataSize & 0b00111111);
		// checksum calculation
		if (checksum) {
			// indicate checksum to master
			destBuffer[1] |= 0x80;
			// calculate checksum
			destBuffer[2] = calculateChecksum(destBuffer[0], destBuffer[1], &destBuffer[3], dataSize);
		}
		
		this->transport->send(destBuffer, (checksum ? 3 : 2) + dataSize);
		return ARDUCOM_COMMAND_HANDLED;
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

void Arducom::setFlags(uint8_t mask, uint8_t flags) {
	if ((mask & ARDUCOM_FLAG_ENABLEDEBUG) == ARDUCOM_FLAG_ENABLEDEBUG) {
		if ((flags & ARDUCOM_FLAG_ENABLEDEBUG) == ARDUCOM_FLAG_ENABLEDEBUG) {
			this->debug = this->origDebug;
		} else {
			this->debug = 0;
		}
	}
	if ((mask & ARDUCOM_FLAG_INFINITELOOP) == ARDUCOM_FLAG_INFINITELOOP) {
		if ((flags & ARDUCOM_FLAG_INFINITELOOP) == ARDUCOM_FLAG_INFINITELOOP) {
			// go into an infinite loop (to test a watchdog)
			while (true) ;
		}
	}
	if ((mask & ARDUCOM_FLAG_SOFTRESET) == ARDUCOM_FLAG_SOFTRESET) {
		if ((flags & ARDUCOM_FLAG_SOFTRESET) == ARDUCOM_FLAG_SOFTRESET) {
			// perform a soft reset using the watchdog
			wdt_enable(WDTO_15MS); 
			while (true) ;
		}
	}
}
	
uint8_t Arducom::getFlags(void) {
	uint8_t result = 0;
	// debug output currently active?
	if (this->debug != 0)
		result |= ARDUCOM_FLAG_ENABLEDEBUG;
	
	return result;
}
	
/******************************************************************************************	
* Arducom command implementations
******************************************************************************************/
// for calculation of free RAM
extern int __heap_start, *__brkval; 

int8_t ArducomVersionCommand::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	int v; 	
	int16_t freeRam = (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 

	// mask and flag byte provided?
	if (*dataSize >= 2) {
		uint8_t mask = dataBuffer[0];
		uint8_t flags = dataBuffer[1];
		arducom->setFlags(mask, flags);
	}
	
	uint8_t pos = 0;
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
	// send free RAM info
	uint16_t *freeRamDest = (uint16_t *)&destBuffer[pos];
	*freeRamDest = freeRam;	
	pos += 2;
	uint8_t c = 0;
	// copy data to destination buffer
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

/***************************************
* Predefined EEPROM access commands
****************************************/

ArducomWriteEEPROMByte::ArducomWriteEEPROMByte(uint8_t commandCode) : ArducomCommand(commandCode, 3) {}
	
int8_t ArducomWriteEEPROMByte::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects three bytes; a two-byte address and a one-byte value
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_update_byte((uint8_t*)address, dataBuffer[2]);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMByte::ArducomReadEEPROMByte(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMByte::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address
	uint16_t address = *((uint16_t*)dataBuffer);
	destBuffer[0] = eeprom_read_byte((uint8_t*)address);
	*dataSize = 1;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt16::ArducomWriteEEPROMInt16(uint8_t commandCode) : ArducomCommand(commandCode, 4) {}
	
int8_t ArducomWriteEEPROMInt16::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects four bytes; a two-byte address and a two-byte value
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_update_word((uint16_t*)address, dataBuffer[2] + (dataBuffer[3] << 8));
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt16::ArducomReadEEPROMInt16(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt16::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_read_block(destBuffer, (uint16_t*)address, 2);
	*dataSize = 2;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt32::ArducomWriteEEPROMInt32(uint8_t commandCode) : ArducomCommand(commandCode, 6) {}
	
int8_t ArducomWriteEEPROMInt32::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects six bytes; a two-byte address and a four-byte value
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_update_block((const void *)&dataBuffer[2], (uint16_t*)address, 4);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt32::ArducomReadEEPROMInt32(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt32::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_read_block(destBuffer, (uint16_t*)address, 4);
	*dataSize = 4;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt64::ArducomWriteEEPROMInt64(uint8_t commandCode) : ArducomCommand(commandCode, 10) {}
	
int8_t ArducomWriteEEPROMInt64::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects ten bytes; a two-byte address and an eight-byte value
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_update_block((const void *)&dataBuffer[2], (uint16_t*)address, 8);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt64::ArducomReadEEPROMInt64(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt64::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address
	uint16_t address = *((uint16_t*)dataBuffer);
	eeprom_read_block(destBuffer, (uint16_t*)address, 8);
	*dataSize = 8;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMBlock::ArducomWriteEEPROMBlock(uint8_t commandCode) : ArducomCommand(commandCode, -1) {}
	
int8_t ArducomWriteEEPROMBlock::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address plus at least one byte to write
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
	// this method expects a two-byte address plus a length byte
	uint16_t address = *((uint16_t*)dataBuffer);
	uint8_t length = dataBuffer[2];
	// validate length
	if (length > maxBufferSize) {
		*errorInfo = maxBufferSize;
		return ARDUCOM_BUFFER_OVERRUN;
	}
	eeprom_read_block(destBuffer, (uint16_t*)address, length);
	*dataSize = length;
	return ARDUCOM_OK;
}

/***************************************
* Predefined RAM access commands
****************************************/

ArducomWriteByte::ArducomWriteByte(uint8_t commandCode, uint8_t* address) : ArducomCommand(commandCode, 1) {
	this->address = address;
}
	
int8_t ArducomWriteByte::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a one-byte value
	*this->address = dataBuffer[0];
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadByte::ArducomReadByte(uint8_t commandCode, uint8_t* address) : ArducomCommand(commandCode, 0) {
	this->address = address;
}

int8_t ArducomReadByte::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects no parameters
	destBuffer[0] = *this->address;
	*dataSize = 1;
	return ARDUCOM_OK;
}

ArducomWriteInt16::ArducomWriteInt16(uint8_t commandCode, int16_t* address) : ArducomCommand(commandCode, 2) {
	this->address = address;
}
	
int8_t ArducomWriteInt16::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte value
	*this->address = *((int16_t*)dataBuffer);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadInt16::ArducomReadInt16(uint8_t commandCode, int16_t* address) : ArducomCommand(commandCode, 0) {
	this->address = address;
}
	
int8_t ArducomReadInt16::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects no parameters
	*((int16_t*)destBuffer) = *this->address;
	*dataSize = 2;
	return ARDUCOM_OK;
}

ArducomWriteInt32::ArducomWriteInt32(uint8_t commandCode, int32_t* address) : ArducomCommand(commandCode, 4) {
	this->address = address;
}
	
int8_t ArducomWriteInt32::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a four-byte value
	*this->address = *((int32_t*)dataBuffer);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadInt32::ArducomReadInt32(uint8_t commandCode, int32_t* address) : ArducomCommand(commandCode, 0) {
	this->address = address;
}	
int8_t ArducomReadInt32::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects no parameters
	*((int32_t*)destBuffer) = *this->address;
	*dataSize = 4;
	return ARDUCOM_OK;
}

ArducomWriteInt64::ArducomWriteInt64(uint8_t commandCode, int64_t* address) : ArducomCommand(commandCode, 8) {
	this->address = address;
}
	
int8_t ArducomWriteInt64::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects an eight-byte value
	*this->address = *((int64_t*)dataBuffer);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadInt64::ArducomReadInt64(uint8_t commandCode, int64_t* address) : ArducomCommand(commandCode, 0) {
	this->address = address;
}
	
int8_t ArducomReadInt64::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects no parameters
	*((int64_t*)destBuffer) = *this->address;
	*dataSize = 8;
	return ARDUCOM_OK;
}

ArducomWriteBlock::ArducomWriteBlock(uint8_t commandCode, uint8_t* address, uint16_t maxBlockSize) : ArducomCommand(commandCode, -1) {
	this->address = address;
	this->maxBlockSize = maxBlockSize;
}
	
int8_t ArducomWriteBlock::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte offset plus at least one byte to write
	#define EXPECTED_PARAM_BYTES 3
	if (*dataSize < EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t offset = *((uint16_t*)dataBuffer);
	// validate block boundary
	if (offset + *dataSize - 2 > this->maxBlockSize) {
		*errorInfo = maxBufferSize;
		return ARDUCOM_BUFFER_OVERRUN;
	}
	// write remaining data to the RAM
	for (uint8_t i = 2; i < *dataSize; i++) {
		this->address[offset + i - 2] = dataBuffer[i];
	}
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadBlock::ArducomReadBlock(uint8_t commandCode, uint8_t* address) : ArducomCommand(commandCode, 3) {
	this->address = address;
}
	
int8_t ArducomReadBlock::handle(Arducom* arducom, volatile uint8_t *dataBuffer, int8_t *dataSize, uint8_t *destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte offset plus a length byte
	uint16_t offset = *((uint16_t*)dataBuffer);
	uint8_t length = dataBuffer[2];
	if (length > maxBufferSize) {
		*errorInfo = maxBufferSize;
		return ARDUCOM_BUFFER_OVERRUN;
	}
	for (uint8_t i = 0; i < length; i++) {
		destBuffer[i] = this->address[offset + i];
	}
	*dataSize = length;
	return ARDUCOM_OK;
}
