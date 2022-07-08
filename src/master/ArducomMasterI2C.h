// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#include "ArducomMaster.h"

#include <string>
#include <sys/sem.h>
#include <sys/ipc.h>

#define I2C_BLOCKSIZE_LIMIT	32

/** Implements an I2C transport mechanism. This class is not thread-safe!
*/
class ArducomMasterTransportI2C: public ArducomMasterTransport {

public:

	ArducomMasterTransportI2C();

	virtual void init(ArducomBaseParameters* parameters) override;

	virtual void sendBytes(uint8_t* buffer, uint8_t size, int retries = 0) override;

	virtual void request(uint8_t expectedBytes) override;

	virtual uint8_t readByte(void) override;

	virtual void done(void) override;

	virtual uint8_t getMaximumCommandSize(void) override;

	virtual uint8_t getDefaultExpectedBytes(void) override;

	virtual int getSemkey(void) override;

	virtual void printBuffer(void) override;

protected:
	std::string filename;
	int slaveAddress;
	ArducomBaseParameters* parameters;

	int fileHandle;
#ifndef ARDUCOM__NO_LOCK_MECHANISM
	// semaphore key
	key_t semkey;
#endif

	uint8_t buffer[I2C_BLOCKSIZE_LIMIT];
	int8_t pos;

};
