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

//#pragma GCC diagnostic error "-Wall"
//#pragma GCC diagnostic error "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"^

#include "ArducomFTP.h"

ArducomFTP* _arducomFTP;

int8_t ArducomFTP::init(Arducom* arducom, SdFat* sdFat, uint8_t commandBase) {
	_arducomFTP = 0;
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

	result = arducom->addCommand(new ArducomFTPDeleteFile(ARDUCOM_FTP_COMMAND_DELETE + commandBase));
	if (result != ARDUCOM_OK)
		return result;

	// store singleton instance
	_arducomFTP = this;

	return ARDUCOM_OK;
}


ArducomFTPInit::ArducomFTPInit(uint8_t commandCode) : ArducomCommand(commandCode) {
}

const char string_0[] PROGMEM = "SD1 ";
const char string_1[] PROGMEM = "SD2 ";
const char string_2[] PROGMEM = "SDHC";
const char string_3[] PROGMEM = "SDXC";

const char* const cardTypes[] PROGMEM = { string_0, string_1, string_2, string_3 }; 

int8_t ArducomFTPInit::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
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
	
	char cardType[5];
	switch (_arducomFTP->sdFat->card()->type()) {
	case SD_CARD_TYPE_SD1:
		strcpy_P(cardType, cardTypes[0]);
		break;
	case SD_CARD_TYPE_SD2:
		strcpy_P(cardType, cardTypes[1]);
		break;
	case SD_CARD_TYPE_SDHC:
		if (cardSize < 70000000) {
			strcpy_P(cardType, cardTypes[2]);
		} else {
			strcpy_P(cardType, cardTypes[3]);
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

int8_t ArducomFTPListFiles::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
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

int8_t ArducomFTPRewind::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
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

int8_t ArducomFTPChangeDir::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// this command expects the new directory name
	#define MAXBUFFERSIZE	13
	char dirname[MAXBUFFERSIZE];
	uint8_t pos = 0;
	while (pos < *dataSize) {
		dirname[pos] = dataBuffer[pos];
		pos++;
		if (pos >= MAXBUFFERSIZE) {
			*errorInfo = MAXBUFFERSIZE;
			return ARDUCOM_BUFFER_OVERRUN;
		}
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

int8_t ArducomFTPOpenRead::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// this command expects the file name
	#define MAXBUFFERSIZE	13
	char filename[MAXBUFFERSIZE];
	uint8_t pos = 0;
	while (pos < *dataSize) {
		filename[pos] = dataBuffer[pos];
		pos++;
		if (pos >= MAXBUFFERSIZE) {
			*errorInfo = MAXBUFFERSIZE;
			return ARDUCOM_BUFFER_OVERRUN;
		}
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

int8_t ArducomFTPReadFile::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}

	if (!_arducomFTP->openFile.isOpen()) {
		*errorInfo = ARDUCOM_FTP_FILE_NOT_OPEN;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	uint32_t position = *((uint32_t*)dataBuffer);
	
	#if ARDUCOM_DEBUG_SUPPORT == 1
	if (arducom->debug) {
		arducom->debug->print(F("Read pos: "));
		arducom->debug->println(position);
	}
	#endif

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

int8_t ArducomFTPCloseFile::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
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

ArducomFTPDeleteFile::ArducomFTPDeleteFile(uint8_t commandCode) : ArducomCommand(commandCode) {
}
	
int8_t ArducomFTPDeleteFile::handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo) {
	if (!_arducomFTP) {
		*errorInfo = ARDUCOM_FTP_NOT_INITIALIZED;
		return ARDUCOM_FUNCTION_ERROR;
	}

	// this command expects the file name
	#define MAXBUFFERSIZE	13
	char filename[MAXBUFFERSIZE];
	uint8_t pos = 0;
	while (pos < *dataSize) {
		filename[pos] = dataBuffer[pos];
		pos++;
		if (pos >= MAXBUFFERSIZE) {
			*errorInfo = MAXBUFFERSIZE;
			return ARDUCOM_BUFFER_OVERRUN;
		}
	}
	filename[pos] = '\0';
	// parameter missing?
	if (filename[0] == '\0') {
		*errorInfo = ARDUCOM_FTP_MISSING_FILENAME;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	SdFile file;
	// regular files must be opened O_WRITE
	if (!file.open(filename, O_WRITE)) {
		// may be a folder; try to open as O_READ
		file.open(filename, O_READ);
	}
	
	if (!file.isOpen()) {
		*errorInfo = ARDUCOM_FTP_FILE_NOT_OPEN;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// has long file name? cannot delete
	if (file.isLFN()) {
		file.close();
		*errorInfo = ARDUCOM_FTP_CANNOT_DELETE;
		return ARDUCOM_FUNCTION_ERROR;
	}
	
	// directory?
	if (file.isDir()) {
		if (!file.rmdir()) {
			file.close();
			*errorInfo = ARDUCOM_FTP_CANNOT_DELETE;
			return ARDUCOM_FUNCTION_ERROR;
		}
	} else {
		// is file
		if (!file.remove()) {
			file.close();
			*errorInfo = ARDUCOM_FTP_CANNOT_DELETE;
			return ARDUCOM_FUNCTION_ERROR;
		}
	}
	
	return ARDUCOM_OK;
}

