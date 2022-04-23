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
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <Wire.h>

#include <ArducomI2C.h>

static ArducomHardwareI2C* hardwareI2C;

ArducomHardwareI2C::ArducomHardwareI2C(uint8_t slaveAddress): ArducomTransport() {
	hardwareI2C = this;
	// join i2c bus
	Wire.begin(slaveAddress);
	// register events
	Wire.onReceive(receiveEvent);
	Wire.onRequest(requestEvent); 
}

int8_t ArducomHardwareI2C::doWork(Arducom* arducom) {
	return ARDUCOM_OK;
}

int8_t ArducomHardwareI2C::send(Arducom* arducom, uint8_t* buffer, uint8_t count) {
	this->status = READY_TO_SEND;
	if (count > ARDUCOM_BUFFERSIZE) {
		this->data[0] = ARDUCOM_ERROR_CODE;
		this->data[1] = ARDUCOM_TOO_MUCH_DATA;
		this->data[2] = count;
		this->size = 3;
		return ARDUCOM_OVERFLOW;
	} else {
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
		// copy buffer data
		for (uint8_t i = 0; i < count; i++)
			this->data[i] = buffer[i];
		this->size = count;
	}
	return ARDUCOM_OK;
}

void ArducomHardwareI2C::receiveEvent(int count) {
	hardwareI2C->size = 0;
	while (Wire.available()) {
		// read byte
		hardwareI2C->data[hardwareI2C->size] = Wire.read(); 
		// no debug support here because it's an interrupt routine
		hardwareI2C->size++;
		if (hardwareI2C->size >= ARDUCOM_BUFFERSIZE) {
			hardwareI2C->status = TOO_MUCH_DATA;
			return;
		}
	}
	hardwareI2C->status = HAS_DATA;
}

void ArducomHardwareI2C::requestEvent(void) {
	if (hardwareI2C->status == READY_TO_SEND) {
		Wire.write((const uint8_t *)hardwareI2C->data, hardwareI2C->size);
	
		hardwareI2C->status = SENT;
		hardwareI2C->size = 0;
	} else {
		// there is nothing to send (premature request)
		const uint8_t noDataResponse[] = { ARDUCOM_ERROR_CODE, ARDUCOM_NO_DATA, 0 };
		Wire.write(noDataResponse, 3);
	}
}


static ArducomSoftwareI2C* softwareI2C;

/** This class defines the transport mechanism for Arducom commands over Software I2C.
*/

// interrupt notification routine
void ArducomSoftwareI2C::I2CReceive(uint8_t length) {
	softwareI2C->size = 0;
	if (length >= ARDUCOM_BUFFERSIZE) {
		softwareI2C->status = TOO_MUCH_DATA;
		return;
	}
	while (softwareI2C->size < length) {
		// read byte
		softwareI2C->data[softwareI2C->size] = softwareI2C->i2c_buffer[softwareI2C->size];
		// no debug support here because it's an interrupt routine
		softwareI2C->size++;
	}
	softwareI2C->status = HAS_DATA;
}

ArducomSoftwareI2C::ArducomSoftwareI2C(I2CSlaveInit i2cInit, I2CSlaveSend i2cSend, uint8_t *i2cBuffer): ArducomTransport() {
	softwareI2C = this;
	this->i2c_send = i2cSend;
	this->i2c_buffer = i2cBuffer;
	i2cInit(&ArducomSoftwareI2C::I2CReceive);		
}
	
int8_t ArducomSoftwareI2C::doWork(Arducom* arducom) {
	return ARDUCOM_OK;
}
	
int8_t ArducomSoftwareI2C::send(Arducom* arducom, uint8_t* buffer, uint8_t count) {
	this->status = READY_TO_SEND;
	if (count > ARDUCOM_BUFFERSIZE) {
		this->data[0] = ARDUCOM_ERROR_CODE;
		this->data[1] = ARDUCOM_TOO_MUCH_DATA;
		this->data[2] = count;
		this->size = 3;
		this->i2c_send(this->data, (uint8_t)this->size);
		return ARDUCOM_OVERFLOW;
	} else {
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
		this->size = count;
		this->i2c_send(buffer, count);
	}
	return ARDUCOM_OK;
}
