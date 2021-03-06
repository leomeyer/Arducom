// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#include <string>

#include "ArducomMaster.h"

#define TCPIP_BLOCKSIZE_LIMIT	32
// maximum number of executes before socket is closed to prevent slave hangups
#define TCPIP_MAXSOCKETCOMM		16

class ArducomMasterTransportTCPIP: public ArducomMasterTransport {
public:

	ArducomMasterTransportTCPIP();

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
	std::string host;
	int port;
	ArducomBaseParameters* parameters;
	
#ifndef __NO_LOCK_MECHANISM
	// semaphore key
	key_t semkey;
#endif

	int sockfd;
	int sockcomm;

	uint8_t buffer[TCPIP_BLOCKSIZE_LIMIT];
	int8_t pos;

	uint8_t readByteInternal(uint8_t* buffer);
};
