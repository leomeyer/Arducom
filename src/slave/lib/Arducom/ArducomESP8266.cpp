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

#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic error "-Werror"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <ArducomESP8266.h>
	
ESP8266WifiTransport::ESP8266WifiTransport(WiFiNetwork* networks, WiFiAddresses* addresses, const char* hostname) : ArducomTransport() {
	this->server = nullptr;
	this->port = ARDUCOM_TCP_DEFAULT_PORT;
	this->networks = networks;
	this->currentNetwork = networks;
	this->addresses = addresses;
	this->hostname = hostname;
	this->processingStart = 0;
	this->timeoutMs = ARDUCOM_DEFAULT_TIMEOUT_MS;
	this->connectAttempts = 20;
}

void ESP8266WifiTransport::setHostname(const char* hostname) {
	this->hostname = hostname;
}

void ESP8266WifiTransport::setPort(int port) {
	this->port = port;
}

void ESP8266WifiTransport::setTimeout(long timeoutMs) {
	this->timeoutMs = timeoutMs;
}

void ESP8266WifiTransport::setConnectAttempts(uint8_t connectAttempts) {
	this->connectAttempts = connectAttempts;
}

int8_t ESP8266WifiTransport::send(Arducom* arducom, uint8_t* buffer, uint8_t count) {
	#if ARDUCOM_DEBUG_SUPPORT == 1
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
	this->client.stop();
	this->status = SENT;
	this->processingStart = 0;
	return ARDUCOM_OK;
}

bool ESP8266WifiTransport::connect(WiFiNetwork* network, Arducom* arducom) {
	if (!this->networks)
		return false;
	WiFi.disconnect();
	delay(100);
	// set hostname if specified
	if (this->hostname)
		WiFi.hostname(this->hostname);
		// set static IP if specified (otherwise use DHCP)
	if (this->addresses) {
		#if ARDUCOM_DEBUG_SUPPORT == 1
		if (arducom->debug)
			arducom->debug->println(F("Configuring static IP address"));
		#endif
		if (!WiFi.config(this->addresses->staticIP, this->addresses->gateway, this->addresses->subnet, this->addresses->dnsServer)) {
			#if ARDUCOM_DEBUG_SUPPORT == 1
			if (arducom->debug)
				arducom->debug->println(F("Static address configuration failed"));
			#endif
		}
	}
	WiFi.mode(WIFI_STA);
	WiFi.begin(network->name, network->password);
	
	int attempts = this->connectAttempts;
	while (attempts > 0 && WiFi.status() != WL_CONNECTED)
	{
		delay(this->timeoutMs);
		#if ARDUCOM_DEBUG_SUPPORT == 1
		if (arducom->debug)
			arducom->debug->print(F("."));
		#endif
		attempts--;
	}
	// not yet connected?
	if (WiFi.status() != WL_CONNECTED) {
		return false;
	}
	return true;
}

int8_t ESP8266WifiTransport::doWork(Arducom* arducom) {
	// sever not set up (disconnected)?
	if (server == nullptr) {
		#if ARDUCOM_DEBUG_SUPPORT == 1
		if (arducom->debug)
			arducom->debug->printf("Connecting to network: %s\n", this->currentNetwork->name);
		#endif
		if (this->connect(this->currentNetwork, arducom)) {
			// connected, start server
			server = new WiFiServer(this->port);
			server->begin();
			#if ARDUCOM_DEBUG_SUPPORT == 1
			if (arducom->debug)
				arducom->debug->printf("Server started, listening at %s:%d\n", WiFi.localIP().toString().c_str(), this->port);
			#endif
		} else {
			#if ARDUCOM_DEBUG_SUPPORT == 1
			if (arducom->debug)
				arducom->debug->printf("\nConnecting to network %s failed with status %d\n", this->currentNetwork->name, WiFi.status());
			#endif
			// try next network
			this->currentNetwork++;
			// end of list?
			if (!this->currentNetwork || !this->currentNetwork->name)
				// start over
				this->currentNetwork = this->networks;
			return ARDUCOM_OK;
		}
	}

	// check whether still connected
	if (WiFi.status() != WL_CONNECTED) {
		#if ARDUCOM_DEBUG_SUPPORT == 1
		if (arducom->debug)
			arducom->debug->printf("\nDisconnected from %s with status %d\n", this->currentNetwork->name, WiFi.status());
		#endif
		server->close();
		server->stop();
		free(server);
		server = nullptr;
		return ARDUCOM_OK;
	}

	// Processing of the last command must finish within a certain time.
	// Otherwise we will query for a new client (an old connection will be discarded).
	// This is mainly to support the ArducomProxyTransport class. Regular Arducom
	// commands will be immediately processed by the Arducom class, so that send
	// can be expected to be called immediately after this method returns.
	// If the method is called repeatedly without send() being called, however,
	// the client instance will be overwritten with the next client (or an invalid value),
	// causing the connection to be dropped. Thus, if send is called later, the data
	// is lost because the client cannot be determined any more.
	// This mechanism avoids this problem.
	if (millis() - this->processingStart < this->timeoutMs)
		return ARDUCOM_OK;

	// free to listen for new connections
	
	// check for incoming connection
	client = server->available();
	if (!client)
		// nothing to do
		return ARDUCOM_OK;

	#if ARDUCOM_DEBUG_SUPPORT == 1
	if (arducom->debug)
		arducom->debug->println(F("Client connected, reading data"));
	#endif
	if (this->status != HAS_DATA)
		this->size = 0;
	uint32_t startTime = millis();
	while (client.connected() && (millis() - startTime < this->timeoutMs)) {
		// read incoming data
		if (client.available()) {
			this->data[this->size] = client.read();
			#if ARDUCOM_DEBUG_SUPPORT == 1
			if (arducom->debug) {
				arducom->debug->print(F("Recv: "));
				arducom->debug->print(this->data[this->size], HEX);
				arducom->debug->println(F(" "));
			}
			#endif
			this->size++;
			this->status = HAS_DATA;
			if (this->size > ARDUCOM_BUFFERSIZE) {
				this->status = TOO_MUCH_DATA;
				this->size = 0;
				return ARDUCOM_OVERFLOW;
			}
			if (arducom->isCommandComplete(this))
				break;
			startTime = millis();
		}
	}
	if (this->status == NO_DATA)
		return ARDUCOM_TIMEOUT;
	#if ARDUCOM_DEBUG_SUPPORT == 1
	if (arducom->debug)
		arducom->debug->printf("Received %d bytes\n", this->size);
	#endif
	this->processingStart = millis();    
	return ARDUCOM_OK;
}
