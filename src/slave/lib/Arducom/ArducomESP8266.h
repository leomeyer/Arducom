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

#include "WiFiEsp.h"

#include <Arducom.h>

/** This class defines the transport mechanism for Arducom commands over 
 * an ESP8266 WLAN module.
*/
class ArducomTransportESP8266: public ArducomTransport {

public:
	/** Sets up the transport as a WLAN device that registers with the given network. */
	ArducomTransportESP8266(Stream* stream, char* ssid, const char* password, uint16_t port = ARDUCOM_TCP_DEFAULT_PORT);

	virtual int8_t doWork(Arducom* arducom);

	/** Prepares the transport to send count bytes from the buffer; returns -1 in case of errors. */
	virtual int8_t send(Arducom* arducom, uint8_t* buffer, uint8_t count);

protected:
	Stream* stream;
	char* ssid;
	const char* password;
	bool initOK;
	WiFiEspServer* server;
	WiFiEspClient client;
};

#endif
