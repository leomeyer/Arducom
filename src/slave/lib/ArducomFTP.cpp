
#include "ArducomFTP.h"

ArducomFTP* _arducomFTP;

int8_t ArducomFTP::init(Arducom* arducom, SdFat* sdFat, uint8_t commandBase) {
	_arducomFTP = 0;
	this->arducom = arducom;
	this->sdFat = sdFat;

	int8_t result = arducom->addCommand(new ArducomFTPInit(ARDUCOM_FTP_COMMAND_INIT + commandBase));
	if (result != ARDUCOM_OK)
		return result;
	result = arducom->addCommand(new ArducomFTPListFiles(ARDUCOM_FTP_COMMAND_LISTFILES + commandBase));
	if (result != ARDUCOM_OK)
		return result;

	result = arducom->addCommand(new ArducomFTPRewind(ARDUCOM_FTP_COMMAND_REWIND + commandBase));
	if (result != ARDUCOM_OK)
		return result;

	result = arducom->addCommand(new ArducomFTPChangeDir(ARDUCOM_FTP_COMMAND_CHDIR + commandBase));
	if (result != ARDUCOM_OK)
		return result;

	result = arducom->addCommand(new ArducomFTPOpenRead(ARDUCOM_FTP_COMMAND_OPENREAD + commandBase));
	if (result != ARDUCOM_OK)
		return result;
	
	result = arducom->addCommand(new ArducomFTPReadFile(ARDUCOM_FTP_COMMAND_READFILE + commandBase));
	if (result != ARDUCOM_OK)
		return result;
	
	result = arducom->addCommand(new ArducomFTPCloseFile(ARDUCOM_FTP_COMMAND_CLOSEFILE + commandBase));
	if (result != ARDUCOM_OK)
		return result;

	// store singleton instance
	_arducomFTP = this;

	return ARDUCOM_OK;
}


ArducomFTPInit::ArducomFTPInit(uint8_t commandCode) : ArducomCommand(commandCode) {
}
	
int8_t ArducomFTPInit::handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this command does not expect any data from the master
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}

	uint32_t cardSize = _arducomFTP->sdFat->card()->cardSize();
	if (cardSize == 0) {
		*errorInfo = ARDUCOM_FTP_SDCARD_ERROR;
		return ARDUCOM_FUNCTION_ERROR;		
	}
	
	char* cardType;
	switch (_arducomFTP->sdFat->card()->type()) {
	case SD_CARD_TYPE_SD1:
		cardType = "SD1 ";
		break;
	case SD_CARD_TYPE_SD2:
		cardType = "SD2 ";
		break;
	case SD_CARD_TYPE_SDHC:
		if (cardSize < 70000000) {
			cardType = "SDHC";
		} else {
			cardType = "SDXC";
		}
		break;
	default:
		*errorInfo = ARDUCOM_FTP_SDCARD_TYPE_UNKNOWN;
		return ARDUCOM_FUNCTION_ERROR;
	}

	// check FAT type
	if ((_arducomFTP->sdFat->vol()->fatType() != 16) && (_arducomFTP->sdFat->vol()->fatType() != 32)) {
		*errorInfo = ARDUCOM_FTP_FILESYSTEM_ERROR;
		return ARDUCOM_FUNCTION_ERROR;		
	}
	
	// in MB
	uint32_t volumeSize = 0.000512 * cardSize + 0.5;

	// assume that destBuffer is big enough
	uint8_t pos = 0;
	uint8_t i = 0;
	// send four bytes card type
	while (i < 4) {
		destBuffer[pos] = cardType[i];
		i++;
		pos++;
	}
	// send FAT type
	destBuffer[pos++] = _arducomFTP->sdFat->vol()->fatType();
	// send four bytes card size (in MB)
	uint32_t* sizeDest = (uint32_t*)&destBuffer[pos];
	*sizeDest = volumeSize;
	pos += 4;
	*dataSize = pos;

	// chdir to root
	if (!_arducomFTP->sdFat->chdir()) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	return ARDUCOM_OK;
}

ArducomFTPListFiles::ArducomFTPListFiles(uint8_t commandCode) : ArducomCommand(commandCode) {
}

int8_t ArducomFTPListFiles::handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// assume no files left to enumerate
	*dataSize = 0;
	
	SdFile entry;
	if (!entry.openNext(_arducomFTP->sdFat->vwd(), O_READ))
		return ARDUCOM_OK;

	// transfer name
	char name[13];
	memset(name, 0, 13);
	if (!entry.getSFN(name)) {
		*errorInfo = ARDUCOM_FTP_READ_ERROR;
		return ARDUCOM_FUNCTION_ERROR;
	}

	uint8_t pos = 0;
	for (uint8_t i = 0; i < 12; i++) {
		destBuffer[pos++] = name[i];
	}
	// pad with NUL
	destBuffer[pos++] = '\0';
	// directory flag (one byte)
	destBuffer[pos++] = (entry.isDir() ? 1 : 0);
	// size (four bytes)
	uint32_t* size = (uint32_t*)&destBuffer[pos];
	*size = entry.fileSize();
	pos += 4;
	// modification date
	dir_t dir;
	entry.dirEntry(&dir);
	uint16_t* lastWriteDate = (uint16_t*)&destBuffer[pos];
	*lastWriteDate = dir.lastWriteDate;
	pos += 2;
	uint16_t* lastWriteTime = (uint16_t*)&destBuffer[pos];
	*lastWriteTime = dir.lastWriteTime;
	pos += 2;
	
	entry.close();

	*dataSize = pos;

	return ARDUCOM_OK;
}

ArducomFTPRewind::ArducomFTPRewind(uint8_t commandCode) : ArducomCommand(commandCode) {
}

int8_t ArducomFTPRewind::handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// rewind current directory (so list files can start over)
	_arducomFTP->sdFat->vwd()->rewind();
	*dataSize = 0;

	return ARDUCOM_OK;
}


ArducomFTPChangeDir::ArducomFTPChangeDir(uint8_t commandCode) : ArducomCommand(commandCode) {
}

int8_t ArducomFTPChangeDir::handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// this command expects the new directory name
	char dirname[13];
	uint8_t pos = 0;
	while (pos < *dataSize) {
		dirname[pos] = dataBuffer[pos];
		pos++;
	}
	dirname[pos] = '\0';
	// parameter missing?
	if (dirname[0] == '\0') {
		*errorInfo = ARDUCOM_FTP_MISSING_FILENAME;
		return ARDUCOM_FUNCTION_ERROR;
	}
		
	if (!_arducomFTP->sdFat->chdir(dirname)) {
		*errorInfo = ARDUCOM_FTP_NOT_A_DIRECTORY;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// transfer name
	char name[13];
	if (!_arducomFTP->sdFat->vwd()->getSFN(name)) {
		*errorInfo = ARDUCOM_FTP_READ_ERROR;
		return ARDUCOM_FUNCTION_ERROR;
	}

	pos = 0;
	for (uint8_t i = 0; (i < 12) && name[i]; i++) {
		destBuffer[pos++] = name[i];
	}
	destBuffer[pos++] = '\0';
	*dataSize = pos;

	return ARDUCOM_OK;
}

ArducomFTPOpenRead::ArducomFTPOpenRead(uint8_t commandCode) : ArducomCommand(commandCode) {
}

int8_t ArducomFTPOpenRead::handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// this command expects the file name
	char filename[13];
	uint8_t pos = 0;
	while (pos < *dataSize) {
		filename[pos] = dataBuffer[pos];
		pos++;
	}
	filename[pos] = '\0';
	// parameter missing?
	if (filename[0] == '\0') {
		*errorInfo = ARDUCOM_FTP_MISSING_FILENAME;
		return ARDUCOM_FUNCTION_ERROR;
	}
		
	if (_arducomFTP->openFile.isOpen())
		_arducomFTP->openFile.close();
		
	if (!_arducomFTP->openFile.open(filename, O_READ)) {
		*errorInfo = ARDUCOM_FTP_FILE_OPEN_ERROR;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	if (!_arducomFTP->openFile.isOpen()) {
		*errorInfo = ARDUCOM_FTP_FILE_NOT_OPEN;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	pos = 0;
	// transfer size (four bytes)
	uint32_t* size = (uint32_t*)&destBuffer[pos];
	*size = _arducomFTP->openFile.fileSize();
	pos += 4;
	*dataSize = pos;

	return ARDUCOM_OK;
}

ArducomFTPReadFile::ArducomFTPReadFile(uint8_t commandCode) : ArducomCommand(commandCode, 4) {
}

int8_t ArducomFTPReadFile::handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	// this command expects a four-byte position to read from
	#define EXPECTED_PARAM_BYTES 4
	if (*dataSize != EXPECTED_PARAM_BYTES) {
		*errorInfo = EXPECTED_PARAM_BYTES;
		return ARDUCOM_PARAMETER_MISMATCH;
	}

	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}

	if (!_arducomFTP->openFile.isOpen()) {
		*errorInfo = ARDUCOM_FTP_FILE_NOT_OPEN;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	uint32_t position = *((uint32_t*)dataBuffer);
	
	if (_arducomFTP->arducom->debug) {
		_arducomFTP->arducom->debug->print(F("Read pos: "));
		_arducomFTP->arducom->debug->println(position);
	}

	if (!_arducomFTP->openFile.seekSet(position)) {
		*errorInfo = ARDUCOM_FTP_POSITION_INVALID;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	int readBytes = _arducomFTP->openFile.read(destBuffer, maxBufferSize);
	if (readBytes < 0) {
		*errorInfo = ARDUCOM_FTP_READ_ERROR;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	*dataSize = readBytes;
	
	return ARDUCOM_OK;
}

ArducomFTPCloseFile::ArducomFTPCloseFile(uint8_t commandCode) : ArducomCommand(commandCode) {
}

int8_t ArducomFTPCloseFile::handle(Arducom* arducom, volatile uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}

	if (!_arducomFTP->openFile.isOpen()) {
		*errorInfo = ARDUCOM_FTP_FILE_NOT_OPEN;
		return ARDUCOM_FUNCTION_ERROR;
	}

	_arducomFTP->openFile.close();
	
	return ARDUCOM_OK;
}
