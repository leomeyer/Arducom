// Copyright (c) 2015 Leo Meyer, leo@leomeyer.de

// *** License ***
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Werror"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"

#include <Arduino.h>

#if defined(ESP8266)
#include <EEPROM.h>
#endif

#if defined(__AVR__)
#include <avr/wdt.h>
#endif

#include <Stream.h>

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
* Arducom stream transport class implementation
******************************************************************************************/

ArducomTransportStream::ArducomTransportStream(Stream* stream): ArducomTransport() {
	this->stream = stream;
}

int8_t ArducomTransportStream::send(Arducom* arducom, uint8_t* buffer, uint8_t count) {
	#ifdef ARDUCOM_DEBUG_SUPPORT
	if (arducom->debug) {
		arducom->debug->print(F("Send: "));
		for (uint8_t i = 0; i < count; i++) {
			arducom->debug->print(buffer[i], HEX);
			arducom->debug->print(F(" "));
		}
		arducom->debug->println();
	}
	#endif
	this->stream->write((const uint8_t *)buffer, count);
	this->stream->flush();
	this->status = SENT;
	this->size = 0;
	return ARDUCOM_OK;
}

int8_t ArducomTransportStream::doWork(Arducom* arducom) {
	if (this->status != HAS_DATA)
		this->size = 0;
	// read incoming data
	while (stream->available()) {
		this->data[this->size] = stream->read();
		#ifdef ARDUCOM_DEBUG_SUPPORT
		if (arducom->debug) {
			arducom->debug->print(F("Recv: "));
			arducom->debug->print(this->data[this->size], HEX);
			arducom->debug->println(F(" "));
		}
		#endif
		this->size++;
		if (this->size >= ARDUCOM_BUFFERSIZE) {
			this->status = TOO_MUCH_DATA;
			this->size = 0;
			return ARDUCOM_OVERFLOW;
		}
		this->status = HAS_DATA;
	}
	return ARDUCOM_OK;
}

/******************************************************************************************	
* Arducom proxy transport class implementation
******************************************************************************************/

ArducomTransportProxy::ArducomTransportProxy(ArducomTransport* transport, Stream* stream): ArducomTransport() {
	this->transport = transport;
	this->stream = stream;
}

int8_t ArducomTransportProxy::send(Arducom* arducom, uint8_t* buffer, uint8_t count) {
	// if this method is ever called by the master it is an error
	// because the master should never see the data passing through this proxy
	return ARDUCOM_TRANSPORT_ERROR;
}

int8_t ArducomTransportProxy::doWork(Arducom* arducom) {
	int8_t result = transport->doWork(arducom);
	// pass errors through
	if (result != ARDUCOM_OK)
		return result;
	// data received by the transport?
	if (transport->status == HAS_DATA) {
		// send the data over the stream
		this->stream->write((const uint8_t *)transport->data, transport->size);
		this->stream->flush();
		transport->status = NO_DATA;
	}
	// read incoming data
	if (stream->available()) {
		// need to collect all data at once because the target transport
		// may require to send everything out at once. Use a small timeout between bytes
		uint32_t startTime = millis();
		while (millis() - startTime < 3) {
			if (stream->available()) {
				this->data[this->size] = stream->read();
				#ifdef ARDUCOM_DEBUG_SUPPORT
				if (arducom->debug) {
					arducom->debug->print(F("Recv: "));
					arducom->debug->print(this->data[this->size], HEX);
					arducom->debug->println(F(" "));
				}
				#endif
				this->size++;
				// this may cause problems if the buffer size of the proxied device
				// is larger than this device's buffer size. A solution is to 
				// increase the buffer size for devices that are only intended
				// to work as proxies.
				if (this->size > ARDUCOM_BUFFERSIZE) {
					this->status = TOO_MUCH_DATA;
					this->size = 0;
					return ARDUCOM_OVERFLOW;
				}
				startTime = millis();
			}
		}
	}
	if (this->size > 0) {
		// send data using underlying transport
		result = transport->send(arducom, this->data, this->size);
		this->size = 0;
	}
	return result;
}

/******************************************************************************************	
* Arducom main class implementation
******************************************************************************************/

Arducom::Arducom(ArducomTransport* transport, Print* debugPrint, uint16_t receiveTimeout) {
	this->transport = transport;
	#if ARDUCOM_DEBUG_SUPPORT == 1
	this->debug = debugPrint;
	this->origDebug = debugPrint;	// remember original pointer (debug is switched off by NULLing debug)
	#endif
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
	// do the command housekeeping
	ArducomCommand* command = this->list;
	while (command != 0) {
		command->doWork(this);
		command = command->next;
	}
	
	// transport data handling
	uint8_t result = this->transport->doWork(this);
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
		// the next byte is the code byte which contains the length
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
					
					#if ARDUCOM_DEBUG_SUPPORT == 1
					if (this->debug) {
						this->debug->print(F("Cmd: "));
						this->debug->print((int)commandByte);
						this->debug->print(F(" Params: "));
						this->debug->println((int)dataSize);
					}
					#endif
					// let the command do the work
					result = command->handle(this, &this->transport->data[(checksum ? 3 : 2)], &dataSize, 
						&destBuffer[(checksum ? 3 : 2)], ARDUCOM_BUFFERSIZE - (checksum ? 3 : 2), &errorInfo);
					#if ARDUCOM_DEBUG_SUPPORT == 1
					if (this->debug) {
						this->debug->print(F("Ret: "));
						this->debug->print((int)result);
						this->debug->print(F(" Params: "));
						this->debug->println((int)dataSize);
					}
					#endif
				}
				break;
			}
			command = command->next;
		}
		// reset data size cache (start over)
		this->lastDataSize = -1;
		this->lastReceiveTime = 0;
		// check result code
		if (result != ARDUCOM_OK) {
			// an error has occurred; send it back to the master
			#if ARDUCOM_DEBUG_SUPPORT == 1
			if (this->debug) {
				this->debug->print(F("Error: "));
				this->debug->print((int)result);
				this->debug->print(F(" Info: "));
				this->debug->println((int)errorInfo);
			}
			#endif
			// send error code back
			destBuffer[0] = ARDUCOM_ERROR_CODE;
			destBuffer[1] = result;
			destBuffer[2] = errorInfo;
			// error codes are not checksummed
			this->transport->send(this, destBuffer, 3);
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
		
		this->transport->send(this, destBuffer, (checksum ? 3 : 2) + dataSize);
		return ARDUCOM_COMMAND_HANDLED;
	} else {
		// new data is not available
		// check whether receive timeout is up
		if ((this->lastReceiveTime > 0) && (this->receiveTimeout > 0)) {
			if (millis() - this->lastReceiveTime > this->receiveTimeout) {
				// timeout is up, reset the transport and start over
				this->transport->reset();
				this->lastReceiveTime = 0;
				this->lastDataSize = -1;
				return ARDUCOM_TIMEOUT;
			}
		}
	}
	return ARDUCOM_OK;
}

void Arducom::setFlags(uint8_t mask, uint8_t flags) {
	#if ARDUCOM_DEBUG_SUPPORT == 1
	if ((mask & ARDUCOM_FLAG_ENABLEDEBUG) == ARDUCOM_FLAG_ENABLEDEBUG) {
		if ((flags & ARDUCOM_FLAG_ENABLEDEBUG) == ARDUCOM_FLAG_ENABLEDEBUG) {
			this->debug = this->origDebug;
		} else {
			this->debug = 0;
		}
	}
	#endif
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
	#if ARDUCOM_DEBUG_SUPPORT == 1
	// debug output currently active?
	if (this->debug != 0)
		result |= ARDUCOM_FLAG_ENABLEDEBUG;
	#endif
	return result;
}

bool Arducom::isCommandComplete(ArducomTransport* transport) {
	int8_t dataSize = transport->hasData();
	// expect at least two bytes: command and code byte
	if (dataSize < 2)
		return false;
	
	// the first byte is the command byte
	// the next byte is the code byte which contains the length
	uint8_t code = transport->data[1];
	// checksum expected? highest bit of the code byte
	bool checksum = (code >> 7);
	// check whether the specified number of bytes has already been received
	// the lower six bits of the code denote the payload size
	if (dataSize - (checksum ? 3 : 2) < (code & 0b00111111))
		// not enough data
		return false;
	return true;
}
	
/******************************************************************************************	
* Arducom command implementations
******************************************************************************************/

// for calculation of free RAM
#if defined(__AVR__)
extern int __heap_start, *__brkval; 
#endif

int8_t ArducomVersionCommand::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
#if defined(__AVR__)
	int v; 	
	int16_t freeRam = (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
#elif defined(ESP8266)		// no better way to check for ESP?
	int16_t freeRam = ESP.getFreeHeap();
#else
	int16_t freeRam = 0;
#endif
	// mask and flag byte provided?
	if (*dataSize >= 2) {
		uint8_t mask = dataBuffer[0];
		uint8_t flags = dataBuffer[1];
		
		if ((mask + (flags << 8) == ARDUCOM_SHUTDOWN) && (this->shutdownHook != NULL))
			(this->shutdownHook)();
		else
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
	uint8_t* flagDest = (uint8_t* )&destBuffer[pos];
	*flagDest = arducom->getFlags();
	pos += 1;
	// send free RAM info
	memcpy(destBuffer + pos, &freeRam, 2);
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
	
int8_t ArducomWriteEEPROMByte::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects three bytes; a two-byte address and a one-byte value
	uint16_t address = *((uint16_t*)dataBuffer);
	#if defined(__AVR__)
	if (E2END < address)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_update_byte((uint8_t*)address, dataBuffer[2]);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address)
		return ARDUCOM_LIMIT_EXCEEDED;
	EEPROM.write(address, dataBuffer[2]);
	if (!EEPROM.commit())
		return ARDUCOM_HARDWARE_ERROR;
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMByte::ArducomReadEEPROMByte(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMByte::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address
	uint16_t address = *((uint16_t*)dataBuffer);
	#if defined(__AVR__)
	destBuffer[0] = eeprom_read_byte((uint8_t*)address);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address)
		return ARDUCOM_LIMIT_EXCEEDED;
	destBuffer[0] = EEPROM.read(address);
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 1;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt16::ArducomWriteEEPROMInt16(uint8_t commandCode) : ArducomCommand(commandCode, 4) {}
	
int8_t ArducomWriteEEPROMInt16::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects four bytes; a two-byte address and a two-byte value
	uint16_t address = *((uint16_t*)dataBuffer);
	#if defined(__AVR__)
	if (E2END < address + 1)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_update_word((uint16_t*)address, dataBuffer[2] + (dataBuffer[3] << 8));
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address + 1)
		return ARDUCOM_LIMIT_EXCEEDED;
	EEPROM.write(address, dataBuffer[2]);
	EEPROM.write(address + 1, dataBuffer[3]);
	if (!EEPROM.commit())
		return ARDUCOM_HARDWARE_ERROR;
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt16::ArducomReadEEPROMInt16(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt16::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address
	uint16_t address = *((uint16_t*)dataBuffer);
	#if defined(__AVR__)
	if (E2END < address + 1)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_read_block(destBuffer, (uint16_t*)address, 2);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address + 1)
		return ARDUCOM_LIMIT_EXCEEDED;
	destBuffer[0] = EEPROM.read(address);
	destBuffer[1] = EEPROM.read(address + 1);
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 2;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt32::ArducomWriteEEPROMInt32(uint8_t commandCode) : ArducomCommand(commandCode, 6) {}
	
int8_t ArducomWriteEEPROMInt32::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects six bytes; a two-byte address and a four-byte value
	uint16_t address = *((uint16_t*)dataBuffer);
	#if defined(__AVR__)
	if (E2END < address + 3)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_update_block((const void *)&dataBuffer[2], (uint16_t*)address, 4);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address + 3)
		return ARDUCOM_LIMIT_EXCEEDED;
	for (uint8_t i = 0; i < 4; i++)
		EEPROM.write(address + i, dataBuffer[i + 2]);
	if (!EEPROM.commit())
		return ARDUCOM_HARDWARE_ERROR;
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt32::ArducomReadEEPROMInt32(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt32::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address
	uint16_t address = *((uint16_t*)dataBuffer);
	#if defined(__AVR__)
	if (E2END < address + 3)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_read_block(destBuffer, (uint16_t*)address, 4);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address + 3)
		return ARDUCOM_LIMIT_EXCEEDED;
	for (uint8_t i = 0; i < 4; i++)
		destBuffer[i] = EEPROM.read(address + i);
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 4;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMInt64::ArducomWriteEEPROMInt64(uint8_t commandCode) : ArducomCommand(commandCode, 10) {}
	
int8_t ArducomWriteEEPROMInt64::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects ten bytes; a two-byte address and an eight-byte value
	uint16_t address = *((uint16_t*)dataBuffer);
	#if defined(__AVR__)
	if (E2END < address + 7)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_update_block((const void *)&dataBuffer[2], (uint16_t*)address, 8);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address + 7)
		return ARDUCOM_LIMIT_EXCEEDED;
	for (uint8_t i = 0; i < 8; i++)
		EEPROM.write(address + i, dataBuffer[i + 2]);
	if (!EEPROM.commit())
		return ARDUCOM_HARDWARE_ERROR;
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMInt64::ArducomReadEEPROMInt64(uint8_t commandCode) : ArducomCommand(commandCode, 2) {}
	
int8_t ArducomReadEEPROMInt64::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address
	uint16_t address = *((uint16_t*)dataBuffer);
	#if defined(__AVR__)
	if (E2END < address + 7)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_read_block(destBuffer, (uint16_t*)address, 8);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address + 7)
		return ARDUCOM_LIMIT_EXCEEDED;
	for (uint8_t i = 0; i < 8; i++)
		destBuffer[i] = EEPROM.read(address + i);
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 8;
	return ARDUCOM_OK;
}

ArducomWriteEEPROMBlock::ArducomWriteEEPROMBlock(uint8_t commandCode) : ArducomCommand(commandCode, -1) {}
	
int8_t ArducomWriteEEPROMBlock::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address plus at least one byte to write
	#define EXPECTED_PARAM_BYTES 3
	if (*dataSize < EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t address = *((uint16_t*)dataBuffer);
	// write remaining data to the EEPROM
	#if defined(__AVR__)
	if (E2END < address + *dataSize - 2)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_update_block((const void *)&dataBuffer[2], (uint16_t*)address, *dataSize - 2);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address + *dataSize - 2)
		return ARDUCOM_LIMIT_EXCEEDED;
	for (uint8_t i = 0; i < *dataSize - 2; i++)
		EEPROM.write(address + i, dataBuffer[i + 2]);
	EEPROM.commit();
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadEEPROMBlock::ArducomReadEEPROMBlock(uint8_t commandCode) : ArducomCommand(commandCode, 3) {}
	
int8_t ArducomReadEEPROMBlock::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte address plus a length byte
	uint16_t address = *((uint16_t*)dataBuffer);
	uint8_t length = dataBuffer[2];
	// validate length
	if (length > maxBufferSize) {
		*errorInfo = maxBufferSize;
		return ARDUCOM_BUFFER_OVERRUN;
	}
	#if defined(__AVR__)
	if (E2END < address + * dataSize - 2)
		return ARDUCOM_LIMIT_EXCEEDED;
	eeprom_read_block(destBuffer, (uint16_t*)address, length);
	#elif defined(ESP8266)
	if (EEPROM.length() <= (size_t)address + *dataSize - 2)
		return ARDUCOM_LIMIT_EXCEEDED;
	for (uint8_t i = 0; i < *dataSize - 2; i++)
		destBuffer[i] = EEPROM.read(address + i);
	#else
		return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	*dataSize = length;
	return ARDUCOM_OK;
}

/***************************************
* Predefined RAM access commands
****************************************/

ArducomWriteByte::ArducomWriteByte(uint8_t commandCode, uint8_t* address) : ArducomCommand(commandCode, 1) {
	this->address = address;
}
	
int8_t ArducomWriteByte::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a one-byte value
	*this->address = dataBuffer[0];
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadByte::ArducomReadByte(uint8_t commandCode, uint8_t* address) : ArducomCommand(commandCode, 0) {
	this->address = address;
}

int8_t ArducomReadByte::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects no parameters
	destBuffer[0] = *this->address;
	*dataSize = 1;
	return ARDUCOM_OK;
}

ArducomWriteInt16::ArducomWriteInt16(uint8_t commandCode, int16_t* address) : ArducomCommand(commandCode, 2) {
	this->address = address;
}
	
int8_t ArducomWriteInt16::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte value
	*this->address = *((int16_t*)dataBuffer);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadInt16::ArducomReadInt16(uint8_t commandCode, int16_t* address) : ArducomCommand(commandCode, 0) {
	this->address = address;
}
	
int8_t ArducomReadInt16::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {

	// this method expects no parameters
	*dataSize = 2;
	memcpy(destBuffer, this->address, *dataSize);
	return ARDUCOM_OK;
}

ArducomWriteInt32::ArducomWriteInt32(uint8_t commandCode, int32_t* address) : ArducomCommand(commandCode, 4) {
	this->address = address;
}
	
int8_t ArducomWriteInt32::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a four-byte value
	*this->address = *((int32_t*)dataBuffer);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadInt32::ArducomReadInt32(uint8_t commandCode, int32_t* address) : ArducomCommand(commandCode, 0) {
	this->address = address;
}	
int8_t ArducomReadInt32::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects no parameters
	*dataSize = 4;
	memcpy(destBuffer, this->address, *dataSize);
	return ARDUCOM_OK;
}

ArducomWriteInt64::ArducomWriteInt64(uint8_t commandCode, int64_t* address) : ArducomCommand(commandCode, 8) {
	this->address = address;
}
	
int8_t ArducomWriteInt64::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects an eight-byte value
	*this->address = *((int64_t*)dataBuffer);
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadInt64::ArducomReadInt64(uint8_t commandCode, int64_t* address) : ArducomCommand(commandCode, 0) {
	this->address = address;
}
	
int8_t ArducomReadInt64::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects no parameters
	*dataSize = 8;
	memcpy(destBuffer, this->address, *dataSize);
	return ARDUCOM_OK;
}

ArducomWriteBlock::ArducomWriteBlock(uint8_t commandCode, uint8_t* address, uint16_t maxBlockSize) : ArducomCommand(commandCode, -1) {
	this->address = address;
	this->maxBlockSize = maxBlockSize;
}
	
int8_t ArducomWriteBlock::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte offset plus at least one byte to write
	#define EXPECTED_PARAM_BYTES 3
	if (*dataSize < EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}
	uint16_t offset = *((uint16_t*)dataBuffer);
	// validate block boundary
	if (offset + *dataSize - 2 > this->maxBlockSize) {
		*errorInfo = this->maxBlockSize;
		return ARDUCOM_LIMIT_EXCEEDED;
	}
	// write remaining data to the RAM
	for (uint8_t i = 2; i < *dataSize; i++) {
		this->address[offset + i - 2] = dataBuffer[i];
	}
	*dataSize = 0;	// no return value
	return ARDUCOM_OK;
}

ArducomReadBlock::ArducomReadBlock(uint8_t commandCode, uint8_t* address, uint16_t maxBlockSize) : ArducomCommand(commandCode, 3) {
	this->address = address;
	this->maxBlockSize = maxBlockSize;
}
	
int8_t ArducomReadBlock::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects a two-byte offset plus a length byte
	uint16_t offset = *((uint16_t*)dataBuffer);
	uint8_t length = dataBuffer[2];
	if (length > maxBufferSize) {
		*errorInfo = maxBufferSize;
		return ARDUCOM_BUFFER_OVERRUN;
	}
	// validate block boundary
	if ((this->maxBlockSize > 0) && (offset + length > this->maxBlockSize)) {
		*errorInfo = this->maxBlockSize;
		return ARDUCOM_LIMIT_EXCEEDED;
	}
	for (uint8_t i = 0; i < length; i++) {
		destBuffer[i] = this->address[offset + i];
	}
	*dataSize = length;
	return ARDUCOM_OK;
}

/***************************************
* Predefined port access commands
****************************************/

ArducomSetPortDirection::ArducomSetPortDirection(uint8_t commandCode, volatile uint8_t* ddRegister, uint8_t allowedMask) : ArducomCommand(commandCode, 2) {
	this->ddRegister = ddRegister;
	this->allowedMask = allowedMask;
}
	
int8_t ArducomSetPortDirection::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects two bytes
	uint8_t mask = dataBuffer[0];
	uint8_t dir = dataBuffer[1];
	// get current direction values
	uint8_t ddr = *this->ddRegister;
	uint8_t actualMask = mask & this->allowedMask;
	// zero out directions to be set
	ddr &= ~actualMask;
	// set the bits according to the new value
	ddr |= dir & actualMask;
	// set new directions
	*this->ddRegister = ddr;
	// return the directions of the port pins that the master may know about
	*dataSize = 1;
	destBuffer[0] = *this->ddRegister & this->allowedMask;
	return ARDUCOM_OK;
}
	
ArducomGetPortDirection::ArducomGetPortDirection(uint8_t commandCode, volatile uint8_t* ddRegister, uint8_t allowedMask) : ArducomCommand(commandCode, 0) {
	this->ddRegister = ddRegister;
	this->allowedMask = allowedMask;
}

int8_t ArducomGetPortDirection::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects nothing
	// get current direction values
	uint8_t ddr = *this->ddRegister;
	// zero out disallowed directions
	ddr &= this->allowedMask;
	destBuffer[0] = ddr;
	*dataSize = 1;
	return ARDUCOM_OK;
}
	
ArducomSetPortState::ArducomSetPortState(uint8_t commandCode, volatile uint8_t* portRegister, volatile uint8_t* pinRegister, uint8_t allowedMask) : ArducomCommand(commandCode, 2) {
	this->portRegister = portRegister;
	this->pinRegister = pinRegister;
	this->allowedMask = allowedMask;
}

int8_t ArducomSetPortState::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects two bytes
	uint8_t mask = dataBuffer[0];
	uint8_t state = dataBuffer[1];
	// get current state values
	uint8_t port = *this->pinRegister;
	uint8_t actualMask = mask & this->allowedMask;
	// zero out state to be set
	port &= ~actualMask;
	// set the bits according to the new value
	port |= state & actualMask;
	// set new directions
	*this->portRegister = port;
	// return the states of the port pins that the master may know about
	destBuffer[0] = *this->pinRegister & this->allowedMask;
	*dataSize = 1;
	return ARDUCOM_OK;
}
	
ArducomGetPortState::ArducomGetPortState(uint8_t commandCode, volatile uint8_t* pinRegister, uint8_t allowedMask) : ArducomCommand(commandCode, 0) {
	this->pinRegister = pinRegister;
	this->allowedMask = allowedMask;
}

int8_t ArducomGetPortState::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects nothing
	// get current port values
	uint8_t pins = *this->pinRegister;
	// zero out disallowed pins
	pins &= this->allowedMask;
	destBuffer[0] = pins;
	*dataSize = 1;
	return ARDUCOM_OK;
}

/***************************************
* Predefined pin access commands
****************************************/

ArducomSetPinDirection::ArducomSetPinDirection(uint8_t commandCode, uint8_t pinNumber) : ArducomCommand(commandCode, 1) {
	this->pinNumber = pinNumber;
}
	
int8_t ArducomSetPinDirection::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects one byte
	uint8_t dir = dataBuffer[0];
	pinMode(this->pinNumber, (dir == 0 ? INPUT : OUTPUT));
	destBuffer[0] = 0; // getPinMode(this->pinNumber);
	*dataSize = 1;
	return ARDUCOM_OK;
}
/*
ArducomGetPinDirection::ArducomGetPinDirection(uint8_t commandCode, uint8_t pinNumber) : ArducomCommand(commandCode, 0) {
	this->pinNumber = pinNumber;
}

int8_t ArducomGetPinDirection::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects nothing
	*dataSize = 1;
	destBuffer[0] = getPinMode(this->pinNumber);
	return ARDUCOM_OK;
}
*/	
ArducomSetPinState::ArducomSetPinState(uint8_t commandCode, uint8_t pinNumber) : ArducomCommand(commandCode, 1) {
	this->pinNumber = pinNumber;
}

int8_t ArducomSetPinState::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects one byte
	uint8_t state = dataBuffer[0];
	// set new state
	digitalWrite(this->pinNumber, (state == 0 ? LOW : HIGH));
	// return the state of the pn
	destBuffer[0] = digitalRead(this->pinNumber);
	*dataSize = 1;
	return ARDUCOM_OK;
}
	
ArducomGetPinState::ArducomGetPinState(uint8_t commandCode, uint8_t pinNumber) : ArducomCommand(commandCode, 0) {
	this->pinNumber = pinNumber;
}

int8_t ArducomGetPinState::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects nothing
	// get current port values
	// return the state of the pn
	destBuffer[0] = digitalRead(this->pinNumber);
	*dataSize = 1;
	return ARDUCOM_OK;
}

ArducomGetAnalogPin::ArducomGetAnalogPin(uint8_t commandCode) : ArducomCommand(commandCode, 1) {
}

int8_t ArducomGetAnalogPin::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	#ifndef NUM_ANALOG_INPUTS
	return ARDUCOM_NOT_IMPLEMENTED;
	#endif
	// this method expects the channel number in the first byte
	uint8_t channel = dataBuffer[0];
	if (channel >= NUM_ANALOG_INPUTS) {
		*errorInfo = (NUM_ANALOG_INPUTS) - 1;
		return ARDUCOM_LIMIT_EXCEEDED;
	}
	*((int16_t*)destBuffer) = analogRead(channel);
	*dataSize = 2;
	return ARDUCOM_OK;
}

ArducomSetPWM::ArducomSetPWM(uint8_t commandCode) : ArducomCommand(commandCode, 2) {
}

int8_t ArducomSetPWM::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this method expects the pin number in the first byte and the PWM value in the second byte
	uint8_t pin = dataBuffer[0];
	uint8_t pwm = dataBuffer[1];
	analogWrite(pin, pwm);
	*dataSize = 0;
	return ARDUCOM_OK;
}

// ArducomTimedToggle

ArducomTimedToggle::ArducomTimedToggle(uint8_t commandCode, uint8_t pin, uint8_t initialState) : ArducomCommand(commandCode, 2) { 
    this->pin = (pin == 1 ? -1 : pin);
    this->switchTime = 0;
	this->initialState = initialState;
    pinMode(pin, OUTPUT);
	digitalWrite(pin, this->initialState);
}

void ArducomTimedToggle::doWork(Arducom* arducom) {
    if (this->switchTime > 0) {
		if (millis() > this->switchTime) {
			digitalWrite(pin, this->initialState);
			this->switchTime = 0;
		}
    }
}
  
int8_t ArducomTimedToggle::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
    if (this->pin < 0)
      return ARDUCOM_INVALID_CONFIG;
	uint16_t duration;
    memcpy(&duration, dataBuffer, 2);
    digitalWrite(pin, this->initialState == LOW ? HIGH : LOW);
	this->switchTime = millis() + duration;
    *dataSize = 0;
    return ARDUCOM_OK;
}
