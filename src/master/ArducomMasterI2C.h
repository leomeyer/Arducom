#include <string>
#include <sys/sem.h>
#include <sys/ipc.h>

#include "ArducomMaster.h"

#define I2C_BLOCKSIZE_LIMIT	32

/** Implements an I2C transport mechanism. This class is not thread-safe!
*/
class ArducomMasterTransportI2C: public ArducomMasterTransport {
public:

	ArducomMasterTransportI2C(std::string filename, int slaveAddress);

	~ArducomMasterTransportI2C();

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
	int slaveAddress;

	int fileHandle;
	// semaphore key
	key_t semkey;
	// semaphore for mutually exclusive access
	int semid;

	uint8_t buffer[I2C_BLOCKSIZE_LIMIT];
	int8_t pos;

	bool hasLock;
	// release the semaphore
	void unlock();
};
