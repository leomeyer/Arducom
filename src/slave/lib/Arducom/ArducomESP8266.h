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

#ifndef __ARDUCOMESP8266_H
#define __ARDUCOMESP8266_H

#ifdef ESP8266

#include <ESP8266WiFi.h>

#include <Arducom.h>

/** This structure defines the access information for a WiFi network. */
struct WiFiNetwork {
	const char* name;
	const char* password;
};

/** This structure can be used to set up the static IP addresses for a WLAN connection. */
struct WiFiAddresses {
	IPAddress staticIP;
	IPAddress gateway;
	IPAddress subnet;
	IPAddress dnsServer;
};


/** This class defines the transport mechanism for Arducom commands over 
 * an ESP266 module.
 */
class ESP8266WifiTransport: public ArducomTransport {

protected:
	WiFiServer *server;
	int port;
	WiFiClient client;
	WiFiNetwork* networks;
	WiFiNetwork* currentNetwork;
	WiFiAddresses* staticIPs;
	const char* hostname;
	uint32_t processingStart;  // important for proxy functionality
	uint32_t timeoutMs;
	uint8_t connectAttempts;

	virtual bool connect(WiFiNetwork* network, Arducom* arducom);
	
public:
	ESP8266WifiTransport(WiFiNetwork* networks, const char* hostname = nullptr, WiFiAddresses* staticIPs = nullptr);

	virtual void setHostname(const char* hostname);

	virtual void setPort(int port);

	/** Sets the timeout for the various network operations. Defaults to ARDUCOM_DEFAULT_TIMEOUT_MS. */
	virtual void setTimeout(long timeoutMs);

	/** Sets the number of connection attempts until a different network is attempted (if defined). */
	virtual void setConnectAttempts(uint8_t connectAttempts);

	virtual int8_t send(Arducom* arducom, uint8_t* buffer, uint8_t count) override;

	virtual int8_t doWork(Arducom* arducom) override;
};

#endif		// def ESP8266

#endif		// def __ARDUCOMESP8266_H