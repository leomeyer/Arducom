
#include <Wire.h>

#include <ArducomStream.h>

ArducomTransportStream::ArducomTransportStream(Stream* stream): ArducomTransport() {
	this->stream = stream;
}

int8_t ArducomTransportStream::send(uint8_t* buffer, uint8_t count) {
	this->stream->write((const uint8_t *)buffer, count);
	this->stream->flush();
	this->status = SENT;
	this->size = 0;
	return ARDUCOM_OK;
}

int8_t ArducomTransportStream::doWork(void) {
	if (this->status != HAS_DATA)
		this->size = 0;
	// read incoming data
	while (stream->available()) {
		this->data[this->size++] = stream->read();
		if (this->size > ARDUCOM_BUFFERSIZE) {
			this->status = TOO_MUCH_DATA;
			return ARDUCOM_OVERFLOW;
		}
		this->status = HAS_DATA;
	}
	return ARDUCOM_OK;
}
