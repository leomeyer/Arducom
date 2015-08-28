#include <string>

#include "ArducomMaster.h"

#define I2C_BLOCKSIZE_LIMIT	32

class ArducomMasterTransportI2C: public ArducomMasterTransport {
public:

	ArducomMasterTransportI2C(std::string filename, int slaveAddress);
	
	virtual void init(void);

	virtual void send(uint8_t* buffer, uint8_t size, int retries = 0);
	
	virtual void request(uint8_t expectedBytes);

	virtual uint8_t readByte(void);
	
	virtual size_t getMaximumCommandSize(void);

	virtual size_t getDefaultExpectedBytes(void);
	
	virtual void printBuffer(void);

protected:
	std::string filename;
	int slaveAddress;
	
	int fileHandle;
	
	uint8_t buffer[I2C_BLOCKSIZE_LIMIT];
	int8_t pos;
};
