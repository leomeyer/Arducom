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

#ifndef __ARDUCOMI2C_H
#define __ARDUCOMI2C_H

#include <Arducom.h>

/** This class defines the transport mechanism for Arducom commands over I2C.
*   This implementation uses hardware I2C via the WSWire library.
*/
class ArducomHardwareI2C: public ArducomTransport {

public:
	ArducomHardwareI2C(uint8_t slaveAddress);

	virtual int8_t doWork(Arducom* arducom);

	virtual int8_t send(Arducom* arducom, uint8_t* buffer, uint8_t count);

protected:
	static void requestEvent(void);
	static void receiveEvent(int count);
};

/** This class defines the transport mechanism for Arducom commands over I2C.
*   This implementation uses software I2C via the SoftwareI2CSlave library.
*   The library is not linked directly. Instead, function pointers must be passed
*   into the constructor call.
*/

// function pointer types for callback and library "linking"
typedef void (*I2CSlaveOnReceive)(uint8_t length);
typedef void (*I2CSlaveInit)(I2CSlaveOnReceive);
typedef void (*I2CSlaveSend)(uint8_t* buffer, uint8_t length);

class ArducomSoftwareI2C: public ArducomTransport {

public:
	ArducomSoftwareI2C(I2CSlaveInit i2cInit, I2CSlaveSend i2cSend, uint8_t *i2cBuffer);

	virtual int8_t doWork(Arducom* arducom);

	virtual int8_t send(Arducom* arducom, uint8_t* buffer, uint8_t count);

	static void I2CReceive(uint8_t length);
	
protected:
	I2CSlaveSend i2c_send;
	uint8_t* i2c_buffer;	
};

#endif
