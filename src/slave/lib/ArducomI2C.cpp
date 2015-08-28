
#include <Wire.h>

#include <ArducomI2C.h>

static ArducomTransportI2C* instance;

ArducomTransportI2C::ArducomTransportI2C(uint8_t slaveAddress): ArducomTransport() {
	instance = this;
	// join i2c bus
	Wire.begin(slaveAddress);
	// register events
	Wire.onReceive(receiveEvent);
	Wire.onRequest(requestEvent); 	
}

int8_t ArducomTransportI2C::doWork(void) {
	return ARDUCOM_OK;
}

int8_t ArducomTransportI2C::send(uint8_t* buffer, uint8_t count) {
	if (this->status != HAS_DATA)
		return -1;
	if (count > ARDUCOM_BUFFERSIZE)
		return this->sendError(ARDUCOM_TOO_MUCH_DATA, count);
	// copy buffer data
	for (uint8_t i = 0; i < count; i++)
		this->data[i] = buffer[i];
	this->size = count;
	this->status = READY_TO_SEND;
}

void ArducomTransportI2C::receiveEvent(int count) {
	instance->size = 0;
	// loop through all but the last
	while (Wire.available()) {
		// read byte
		instance->data[instance->size] = Wire.read(); 
		instance->size++;
		if (instance->size >= ARDUCOM_BUFFERSIZE) {
			instance->status = TOO_MUCH_DATA;
			return;
		}
	}
	instance->status = HAS_DATA;
}

void ArducomTransportI2C::requestEvent(void) {
	if (instance->status == READY_TO_SEND) {
		Wire.write((const uint8_t *)instance->data, instance->size);
	
		instance->status = SENT;
		instance->size = 0;
	} else {
		// there is nothing to send (premature request)
		const uint8_t noDataResponse[] = { ARDUCOM_ERROR_CODE, ARDUCOM_NO_DATA, 0 };
		Wire.write(noDataResponse, 3);
	}
}