#ifndef __ARDUCOMSTREAM_H
#define __ARDUCOMSTREAM_H

#include <Arducom.h>

/** This class defines the transport mechanism for Arducom commands over a Stream.
*/
class ArducomTransportStream: public ArducomTransport {

public:
	ArducomTransportStream(Stream* stream);

	virtual int8_t doWork(void);

	/** Prepares the transport to send count bytes from the buffer; returns -1 in case of errors. */
	virtual int8_t send(uint8_t* buffer, uint8_t count);

protected:
	Stream* stream;
};


#endif
