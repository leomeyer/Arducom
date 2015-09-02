#ifndef __ARDUCOMI2C_H
#define __ARDUCOMI2C_H

#include <Arducom.h>

/** This class defines the transport mechanism for Arducom commands over I2C.
*/
class ArducomTransportI2C: public ArducomTransport {

public:
	ArducomTransportI2C(uint8_t slaveAddress);
	
	virtual int8_t doWork(void);

	virtual int8_t send(uint8_t* buffer, uint8_t count);
	
protected:
	static void requestEvent(void);
	static void receiveEvent(int count);
};


#endif
