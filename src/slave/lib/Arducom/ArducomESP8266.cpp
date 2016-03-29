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

#include <ArducomESP8266.h>

ArducomTransportESP8266::ArducomTransportESP8266(Stream* stream, char* ssid, const char* password, uint16_t port): ArducomTransport() {
	this->stream = stream;
	this->server = new WiFiEspServer(port);
	this->ssid = ssid;
	this->password = password;
	this->initOK = false;
}

int8_t ArducomTransportESP8266::send(Arducom* arducom, uint8_t* buffer, uint8_t count) {
	if (!this->client.connected()) {
		if (arducom->debug)
			arducom->debug->println(F("Client has disconnected"));
		return ARDUCOM_NETWORK_ERROR;
	}
	this->client.write((const uint8_t *)buffer, count);
	this->client.flush();
	this->status = SENT;
	this->size = 0;
	return ARDUCOM_OK;
}

int8_t ArducomTransportESP8266::doWork(Arducom* arducom) {
	// initialize ESP8266 on first call
	if (!this->initOK) {
		if (arducom->debug)
			arducom->debug->println(F("Initializing ESP8266..."));
		WiFi.init(this->stream);
		if (WiFi.status() == WL_NO_SHIELD) {
			if (arducom->debug)
				arducom->debug->println(F("No ESP8266 detected."));
			return ARDUCOM_HARDWARE_ERROR;
		}
		// try to connect to the network
		uint16_t timeout = 5000;		// milliseconds
		unsigned long start = millis();
		int8_t status = 0;
		while ((status != WL_CONNECTED) && (millis() - start > timeout)) {
			status = WiFi.begin(this->ssid, this->password);
		}
		// connection error?
		if (status != WL_CONNECTED)
			return ARDUCOM_NETWORK_ERROR;
		
		// connected
		this->initOK = true;
	} else {
		// initialization was ok
		
		// check WiFi status
		if (WiFi.status() != WL_CONNECTED) {
			this->initOK = false;
			return ARDUCOM_NETWORK_ERROR;
		}
	
		// check whether no client is connected
		if (!this->client || !this->client.connected()) {
			this->client = server->available();
			if (this->client && arducom->debug)
				arducom->debug->println(F("New client connected."));
		}
		
		// get data from a connected client
		if (this->client && this->client.connected()) {
			if (this->status != HAS_DATA)
				this->size = 0;
			// read incoming data
			while (this->client.available()) {
				this->data[this->size] = this->client.read();
				this->size++;
				if (this->size > ARDUCOM_BUFFERSIZE) {
					this->status = TOO_MUCH_DATA;
					return ARDUCOM_OVERFLOW;
				}
				this->status = HAS_DATA;
			}
		}
	}
	return ARDUCOM_OK;
}
