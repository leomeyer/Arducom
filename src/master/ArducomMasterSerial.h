#include <string>

#include "ArducomMaster.h"

#define SERIAL_BLOCKSIZE_LIMIT	32

class ArducomMasterTransportSerial: public ArducomMasterTransport {
public:

	ArducomMasterTransportSerial(std::string filename, int baudrate, int timeout);
	
	virtual void init(void);

	virtual void send(uint8_t* buffer, uint8_t size, int retries = 0);
	
	virtual void request(uint8_t expectedBytes);

	virtual uint8_t readByte(void);
	
	virtual void done(void);
	
	virtual size_t getMaximumCommandSize(void);

	virtual size_t getDefaultExpectedBytes(void);
	
	virtual void printBuffer(void);

protected:
	std::string filename;
	int baudrate;
	
	int fileHandle;
	long timeout;
	
	uint8_t buffer[SERIAL_BLOCKSIZE_LIMIT];
	int8_t pos;
	
	uint8_t readByteInternal(uint8_t* buffer);
};
