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

#ifdef ARDUINO
#include <SdFat.h>
#endif

#include "Arducom.h"

// Arducom FTP status codes
#define ARDUCOM_FTP_OK					0
#define ARDUCOM_FTP_SDCARD_ERROR		1
#define ARDUCOM_FTP_SDCARD_TYPE_UNKNOWN	2
#define ARDUCOM_FTP_FILESYSTEM_ERROR	3
#define ARDUCOM_FTP_NOT_INITIALIZED		4
#define ARDUCOM_FTP_MISSING_FILENAME	5
#define ARDUCOM_FTP_NOT_A_DIRECTORY		6
#define ARDUCOM_FTP_FILE_OPEN_ERROR		7
#define ARDUCOM_FTP_READ_ERROR			8
#define ARDUCOM_FTP_FILE_NOT_OPEN		9
#define ARDUCOM_FTP_POSITION_INVALID	10
#define ARDUCOM_FTP_CANNOT_DELETE		11

// Arducom FTP command codes
#define ARDUCOM_FTP_COMMAND_INIT		0
#define ARDUCOM_FTP_COMMAND_LISTFILES	1
#define ARDUCOM_FTP_COMMAND_REWIND	2
#define ARDUCOM_FTP_COMMAND_CHDIR		3
#define ARDUCOM_FTP_COMMAND_OPENREAD	4
#define ARDUCOM_FTP_COMMAND_READFILE	5
#define ARDUCOM_FTP_COMMAND_CLOSEFILE	6
#define ARDUCOM_FTP_COMMAND_DELETE	7

#define ARDUCOM_FTP_DEFAULT_COMMANDBASE	60

#ifdef ARDUINO

/** This class adds the ArducomFTP commands to the supplied Arducom instance.
*/
class ArducomFTP {
public:
	SdFat* sdFat;
	SdFile openFile;
 
	int8_t init(Arducom* arducom, SdFat* sdFat, uint8_t commandBase = ARDUCOM_FTP_DEFAULT_COMMANDBASE);
};

// singleton for commands to access common information
extern ArducomFTP* _arducomFTP;

/** This class implements a command to initialize or reset the Arducom FTP system.
* It returns information about an FAT formatted SD card.
*/
class ArducomFTPInit: public ArducomCommand {
public:
	ArducomFTPInit(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to read file infos from the current directory.
*/
class ArducomFTPListFiles: public ArducomCommand {
public:
	ArducomFTPListFiles(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to rewind the current directory after listing files.
*/
class ArducomFTPRewind: public ArducomCommand {
public:
	ArducomFTPRewind(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to change the current directory.
*/
class ArducomFTPChangeDir: public ArducomCommand {
public:
	ArducomFTPChangeDir(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to open a file for reading. Returns the size of the opened file.
*/
class ArducomFTPOpenRead: public ArducomCommand {
public:
	ArducomFTPOpenRead(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to read a section of the currently open file.
*/
class ArducomFTPReadFile: public ArducomCommand {
public:
	ArducomFTPReadFile(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to close the currently open file.
*/
class ArducomFTPCloseFile: public ArducomCommand {
public:
	ArducomFTPCloseFile(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

/** This class implements a command to delete a file. Only 8.3 files and folders that do not have a long file name can be deleted.
*/
class ArducomFTPDeleteFile: public ArducomCommand {
public:
	ArducomFTPDeleteFile(uint8_t commandCode);
	
	int8_t handle(Arducom* arducom, uint8_t* dataBuffer, int8_t* dataSize, uint8_t* destBuffer, const uint8_t maxBufferSize, uint8_t* errorInfo);
};

#endif
