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

#ifndef __ARDUCOMETHERNET_H
#define __ARDUCOMETHERNET_H

#include "SPI.h"
#include "Ethernet.h"

#include <Arducom.h>

/** This class defines the transport mechanism for Arducom commands over 
 * an Ethernet LAN module.
 * Ethernet must be initialized before using this transport by calling
 *   Ethernet.init(init_pin);
 * and
 *   Ethernet.begin(mac, ip, gateway, subnet);
 * or equivalent calls. Any board specific setup must also be done beforehand.
 * For example, the DFRobot W5200 shield requires the following init code:
 *   pinMode(8, OUTPUT);
 *   pinMode(9, OUTPUT);
 *   digitalWrite(8, HIGH);
 *   digitalWrite(9, LOW);
 * Consequently those pins cannot be used for IO any more.
 *
 * This class starts a listening server on the given port. It accepts data
 * from connecting clients and turns it over to Arducom. Returned data
 * is sent back to the client.
 * This class does not close connections. It is the responsibility of the
 * client to do that.
 * Make sure to call Ethernet.maintain() in your loop if using dynamic
 * IP addresses. However, it may be more useful to use a static IP address.
*/
class ArducomTransportEthernet: public ArducomTransport {

public:
	/** Sets up the transport as a WLAN device that registers with the given network. */
	ArducomTransportEthernet(uint16_t port = ARDUCOM_TCP_DEFAULT_PORT);

	virtual int8_t doWork(Arducom* arducom);

	/** Prepares the transport to send count bytes from the buffer; returns -1 in case of errors. */
	virtual int8_t send(Arducom* arducom, uint8_t* buffer, uint8_t count);

protected:
	EthernetServer server;
	EthernetClient client;
	bool initOK;
	uint32_t lastSendTime;
};

#endif
