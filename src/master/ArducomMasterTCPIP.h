// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#ifndef _MSC_VER
#include <openssl/sha.h>		// requires libssl-devel
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#endif

#include <string>

#include "ArducomMaster.h"

#define TCPIP_BLOCKSIZE_LIMIT	32
// maximum number of executes before socket is closed to prevent slave hangups
#define TCPIP_MAXSOCKETCOMM		16

class ArducomMasterTransportTCPIP: public ArducomMasterTransport {
public:

	ArducomMasterTransportTCPIP();

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
	std::string host;
	int port;
	ArducomBaseParameters* parameters;
	
#ifndef ARDUCOM__NO_LOCK_MECHANISM
	// semaphore key
	key_t semkey;
#endif

	int sockfd;
	int sockcomm;

	uint8_t buffer[TCPIP_BLOCKSIZE_LIMIT];
	int8_t pos;

	uint8_t readByteInternal(uint8_t* buffer);
};
