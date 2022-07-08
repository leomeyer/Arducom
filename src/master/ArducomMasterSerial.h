// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#ifdef _MSC_VER
#include <windows.h>
#endif

#include <string>

#include "ArducomMaster.h"

#define SERIAL_BLOCKSIZE_LIMIT	32

class ArducomMasterTransportSerial: public ArducomMasterTransport {
public:

	ArducomMasterTransportSerial();

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
	int baudrate;
	ArducomBaseParameters* parameters;
	
#ifndef ARDUCOM__NO_LOCK_MECHANISM
	// semaphore key
	key_t semkey;
#endif

#ifdef _MSC_VER
	HANDLE fileHandle;
#else
	int fileHandle;
#endif

	uint8_t buffer[SERIAL_BLOCKSIZE_LIMIT] = { };
	int8_t pos;

	uint8_t readByteInternal(uint8_t* buffer);
};
