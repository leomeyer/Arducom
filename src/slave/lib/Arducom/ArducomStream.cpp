// Copyright (c) 2015 Leo Meyer, leo@leomeyer.de

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

#include <ArducomStream.h>

ArducomTransportStream::ArducomTransportStream(Stream* stream): ArducomTransport() {
	this->stream = stream;
}

int8_t ArducomTransportStream::send(Arducom* arducom, uint8_t* buffer, uint8_t count) {
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
	this->stream->write((const uint8_t *)buffer, count);
	this->stream->flush();
	this->status = SENT;
	this->size = 0;
	return ARDUCOM_OK;
}

int8_t ArducomTransportStream::doWork(Arducom* arducom) {
	if (this->status != HAS_DATA)
		this->size = 0;
	// read incoming data
	while (stream->available()) {
		this->data[this->size] = stream->read();
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
	return ARDUCOM_OK;
}
