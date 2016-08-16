// Copyright (c) 2016 Leo Meyer, leo@leomeyer.de

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

#include <Stream.h>

#include <ArducomEthernet.h>

ArducomTransportEthernet::ArducomTransportEthernet(uint16_t port): ArducomTransport(), server(port) {
	this->initOK = false;
}

int8_t ArducomTransportEthernet::send(Arducom* arducom, uint8_t* buffer, uint8_t count) {
	if (!this->client.connected()) {
		#if ARDUCOM_DEBUG_SUPPORT == 1
		if (arducom->debug)
			arducom->debug->println(F("Client not connected"));
		#endif
		return ARDUCOM_NETWORK_ERROR;
	}
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
	this->client.write((const uint8_t *)buffer, count);
	this->client.flush();
	this->lastSendTime = millis();
	this->status = SENT;
	this->size = 0;
	return ARDUCOM_OK;
}

int8_t ArducomTransportEthernet::doWork(Arducom* arducom) {
	// initialize server on first call
	if (!this->initOK) {
		#if ARDUCOM_DEBUG_SUPPORT == 1
		if (arducom->debug)
			arducom->debug->println(F("Initializing Ethernet..."));
		#endif
		this->server.begin();
 		this->initOK = true;
	} else {
		// get a connecting client
		if (server.available())
			this->client = server.available();
		// get data from a connected client
		if (this->client.available() > 0) {
			if (this->status != HAS_DATA)
				this->size = 0;
			// read incoming data
			while (this->client.available()) {
				this->data[this->size] = this->client.read();
				#ifdef ARDUCOM_DEBUG_SUPPORT
				if (arducom->debug) {
					arducom->debug->print(F("Recv: "));
					arducom->debug->print(this->data[this->size], HEX);
					arducom->debug->println(F(" "));
				}
				#endif
				this->size++;
				if (this->size > ARDUCOM_BUFFERSIZE) {
					this->status = TOO_MUCH_DATA;
					return ARDUCOM_OVERFLOW;
				}
				this->status = HAS_DATA;
			}
		} else if ((this->lastSendTime > 0) && (millis() - this->lastSendTime > 1000)) {
			this->client.stop();		// disconnect
			this->initOK = false;
			this->lastSendTime = 0;
		}
	}
	return ARDUCOM_OK;
}
