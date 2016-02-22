#include <string>

#include "ArducomMaster.h"

#define TCPIP_BLOCKSIZE_LIMIT	32
#define DEFAULT_PORT			4152

class ArducomMasterTransportTCPIP: public ArducomMasterTransport {
public:

	ArducomMasterTransportTCPIP(std::string host, int port);

	virtual void init(ArducomBaseParameters* parameters);

	virtual void send(uint8_t* buffer, uint8_t size, int retries = 0);

	virtual void request(uint8_t expectedBytes);

	virtual uint8_t readByte(void);

	virtual void done(void);

	virtual size_t getMaximumCommandSize(void);

	virtual size_t getDefaultExpectedBytes(void);

	virtual int getSemkey(void);

	virtual void printBuffer(void);

protected:
	std::string host;
	int port;
	ArducomBaseParameters* parameters;
	
	// semaphore key
	key_t semkey;

	int sockfd;

	uint8_t buffer[TCPIP_BLOCKSIZE_LIMIT];
	int8_t pos;

	uint8_t readByteInternal(uint8_t* buffer);
};
