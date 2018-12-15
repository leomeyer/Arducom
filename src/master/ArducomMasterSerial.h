// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#ifdef WIN32
#include <windows.h>
#endif

#include <string>

#include "ArducomMaster.h"

#define SERIAL_BLOCKSIZE_LIMIT	32

class ArducomMasterTransportSerial: public ArducomMasterTransport {
public:

	ArducomMasterTransportSerial();

	virtual void init(ArducomBaseParameters* parameters);

	virtual void sendBytes(uint8_t* buffer, uint8_t size, int retries = 0);

	virtual void request(uint8_t expectedBytes);

	virtual uint8_t readByte(void);

	virtual void done(void);

	virtual size_t getMaximumCommandSize(void);

	virtual size_t getDefaultExpectedBytes(void);

	virtual int getSemkey(void);

	virtual void printBuffer(void);

protected:
	std::string filename;
	int baudrate;
	ArducomBaseParameters* parameters;
	
#ifndef __NO_LOCK_MECHANISM
	// semaphore key
	key_t semkey;
#endif

#ifdef WIN32
	HANDLE fileHandle;
#else
	int fileHandle;
#endif

	uint8_t buffer[SERIAL_BLOCKSIZE_LIMIT];
	int8_t pos;

	uint8_t readByteInternal(uint8_t* buffer);
};
