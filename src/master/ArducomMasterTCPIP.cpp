// Arducom master implementation
//
// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de
// Arduino communications library
// Project page: https://github.com/leomeyer/Arducom
// License: MIT License. For details see the project page.

#include "ArducomMasterTCPIP.h"

#include <exception>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <sys/types.h>

#include "../slave/lib/Arducom/Arducom.h"

#ifdef _MSC_VER
#define SOCKERR_FUNC WSAGetLastError()
#else
#define SOCKERR_FUNC errno
#endif

ArducomMasterTransportTCPIP::ArducomMasterTransportTCPIP() {
	this->pos = -1;
	this->sockfd = -1;
	this->parameters = nullptr;
	this->port = ARDUCOM_TCP_DEFAULT_PORT;
	this->sockcomm = 0;
}

void ArducomMasterTransportTCPIP::init(ArducomBaseParameters* parameters) {
	this->parameters = parameters;
	this->host = parameters->device;
	this->port = parameters->deviceAddress;
#ifndef _MSC_VER
	// calculate SHA1 hash of host:port
	std::stringstream fullNameSS;
	fullNameSS << this->host << ":" << this->port;
	std::string fullName = fullNameSS.str();

#ifndef ARDUCOM__NO_LOCK_MECHANISM
	unsigned char hash[SHA_DIGEST_LENGTH];
	SHA1((const unsigned char*)fullName.c_str(), fullName.size(), hash);

	// IPC semaphore key is the first four bytes of the hash
	this->semkey = *(int*)&hash;
	// std::cout << "Semaphore key:" << this->semkey << std::endl;
#endif	
#else
	// initialize socket subsystem
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		throw_system_error("Windows socket subsystem failure", nullptr, SOCKERR_FUNC);
	}
#endif
}

void ArducomMasterTransportTCPIP::sendBytes(uint8_t* buffer, uint8_t size, int retries) {
	if (size > TCPIP_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to send exceeds TCP/IP block size limit");

	// socket closed?
	if (this->sockfd < 0) {
		struct sockaddr_in serv_addr;
		struct hostent *server;

		server = gethostbyname(this->host.c_str());
		if (server == NULL)
			throw std::runtime_error(std::string("Host not found: " + this->host
				+ std::string(" (") + std::to_string(SOCKERR_FUNC) + std::string(")")
			).c_str());

		this->sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (this->sockfd < 0)
			throw_system_error("Failed to open TCP/IP socket", nullptr, SOCKERR_FUNC);

		// set socket to blocking
#ifdef _MSC_VER
		u_long iMode = 0;
		// If iMode = 0, blocking is enabled; 
		// If iMode != 0, non-blocking mode is enabled.
		int iResult = ioctlsocket(this->sockfd, FIONBIO, &iMode);
		if (iResult != NO_ERROR)
			throw_system_error("ioctlsocket failed", nullptr, SOCKERR_FUNC);
#endif

		if (this->parameters->timeoutMs > 0) {
			struct timeval timeout;
			timeout.tv_sec = this->parameters->timeoutMs / 1000;
			timeout.tv_usec = 0;

			if (setsockopt(this->sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) < 0)
				throw_system_error("Error setting TCP receive timeout", nullptr, SOCKERR_FUNC);

			if (setsockopt(this->sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) < 0)
				throw_system_error("Error setting TCP send timeout", nullptr, SOCKERR_FUNC);
		}

		// disable nagling
		int flag = 1;
		if (setsockopt(this->sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) < 0)
			throw_system_error("Error disabling TCP nagling", nullptr, SOCKERR_FUNC);

		memset((char*)&serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		memcpy((char*)&serv_addr.sin_addr.s_addr, (char*)server->h_addr, server->h_length);
		serv_addr.sin_port = htons(this->port);

		if (connect(this->sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
			throw_system_error("Could not connect to host", this->host.c_str(), SOCKERR_FUNC);

		// reset communication counter
		this->sockcomm = 0;
	}

	int my_retries = retries;
	repeat:
#ifdef _MSC_VER
	if ((send(this->sockfd, (const char*)&buffer[0], size, 0)) == SOCKET_ERROR) {
#else
	if ((write(this->sockfd, &buffer[0], size)) != size) {
#endif
		if (my_retries <= 0) {
			throw_system_error("Error sending data via TCP/IP", nullptr, SOCKERR_FUNC);
		} else {
			my_retries--;
			goto repeat;
		}
	}
}

#ifdef _MSC_VER
uint8_t ArducomMasterTransportTCPIP::readByteInternal(uint8_t* buffer) {
	return *buffer;
}
#else
uint8_t ArducomMasterTransportTCPIP::readByteInternal(uint8_t* buffer) {
	int bytesRead = read(this->sockfd, buffer, 1);
	if (bytesRead < 0) {
		throw_system_error("Unable to read from network", nullptr, SOCKERR_FUNC);
	}
	else
	if (bytesRead == 0) {
		throw Arducom::TimeoutException("Timeout");
	}
	else
	if (bytesRead > 1) {
		throw std::runtime_error("Big trouble! Read returned more than one byte");
	}
	else {
		/*
		std::cout << "Byte read: ";
		ArducomMaster::printBuffer(buffer, 1);
		std::cout << std::endl;
		*/
		return *buffer;
	}
	return 0;
}
#endif 

void ArducomMasterTransportTCPIP::request(uint8_t expectedBytes) {
	if (expectedBytes > TCPIP_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Error: number of bytes to receive exceeds TCP/IP block size limit");
	uint8_t pos = 0;
	memset(&this->buffer, 0, TCPIP_BLOCKSIZE_LIMIT);

#ifdef _MSC_VER
	int bytesRead = recv(this->sockfd, (char*)buffer, expectedBytes, 0);
	if (bytesRead < 0) {
		throw_system_error("Unable to read from network", nullptr, SOCKERR_FUNC);
	}
	else
	if (bytesRead == 0) {
		throw Arducom::TimeoutException("Timeout");
	}
#endif

	// read the first byte
	uint8_t resultCode = this->readByteInternal(&this->buffer[pos++]);
	if (expectedBytes > 1) {
		// inspect first byte of the reply
		// error?
		if (resultCode == ARDUCOM_ERROR_CODE) {
			// read the next two bytes (error code plus error info)
			this->readByteInternal(&this->buffer[pos++]);
			if (expectedBytes > 2)
				this->readByteInternal(&this->buffer[pos++]);
		} else {
			// read code byte
			uint8_t code = this->readByteInternal(&this->buffer[pos++]);
			uint8_t length = (code & 0b00111111);

//			std::cout << "Expecting: " << (int)length << " bytes" << std::endl;
			// read payload into the buffer; up to expected bytes or returned bytes, whatever is lower
			bool checksum = (code & 0x80) == 0x80;
			while ((pos < expectedBytes) && (pos < length + (checksum ? 3 : 2))) {
				this->readByteInternal(&this->buffer[pos++]);
				if (pos > TCPIP_BLOCKSIZE_LIMIT)
					throw std::runtime_error("Error: number of received bytes exceeds TCP/IP block size limit");
			}
		}
	}
	this->pos = 0;
}

uint8_t ArducomMasterTransportTCPIP::readByte(void) {
	if (this->pos < 0)
		throw std::runtime_error("Can't read: Data must be requested first");
	if (pos >= TCPIP_BLOCKSIZE_LIMIT)
		throw std::runtime_error("Can't read: Too many bytes requested");
	return this->buffer[this->pos++];
}

void ArducomMasterTransportTCPIP::done() {
#ifdef _MSC_VER
	closesocket(this->sockfd);
#else
	close(this->sockfd);
#endif
	this->sockfd = -1;
}

uint8_t ArducomMasterTransportTCPIP::getMaximumCommandSize(void) {
	return TCPIP_BLOCKSIZE_LIMIT;
}

uint8_t ArducomMasterTransportTCPIP::getDefaultExpectedBytes(void) {
	return TCPIP_BLOCKSIZE_LIMIT;
}

int ArducomMasterTransportTCPIP::getSemkey(void) {
#ifdef ARDUCOM__NO_LOCK_MECHANISM
	return 0;
#else
	return this->semkey;
#endif
}

void ArducomMasterTransportTCPIP::printBuffer(void) {
	ArducomMaster::printBuffer(this->buffer, TCPIP_BLOCKSIZE_LIMIT);
}
