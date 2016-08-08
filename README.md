Arducom - Arduino communication library
=======================================

_Please note - this project is pretty much work in progress. Also, documentation is not yet complete._

Arducom simplifies interaction between Arduinos and Linux devices.
It is designed to be versatile and easy to extend. It currently supports serial, I2C and TCP/IP connections.

Currently supported functions:
	
	- Read and write data from and to EEPROM
	- Read and write data from and to RAM
	- Set and read a Real Time Clock (DS1307 is supported)
	- "FTP-style" SD card file access
	- Access Arduino digital pins (read and set state)
	- Read the values of analog inputs
	- Serial port communication via RS232 (or via Bluetooth module)
	- I2C via hardware I2C (or software I2C on almost arbitrary pins)
	- TCP/IP (e. g. with Ethernet shield)

Arducom is useful for e. g.:

	- Remote-controlling Arduinos
	- Data acquisition and data logging
	- Controlling relays or other actors
	- Taking measurements
	
For example, you may want to use an Arduino with an SD card as a low cost data logger. More often
than not it is impractical to remove the SD card and insert it into a reader to
extract the data logs because this means interrupting the data logging.
Perhaps you may as well want to query the Arduino for current readings from elsewhere.

Arducom allows you to communicate with the Arduino and can transfer files from the Arduino's SD card 
without interrupting real-time operation.

Arducom currently supports I2C, serial and TCP/IP communication. It also contains a software
implementation of an I2C slave for Arduinos. There is also an implementation of a versatile data logger
that supports up to four S0 lines, DHT22 temperature sensors, and an OBIS parser for metering data (D0).

The library provides command line tools for testing and integration as well as a C++ API
for use in your own programs.

Quick start
-----------

For a quick introduction how to setup the hello-world demo and connect to it from
a Linux machine, see here:

https://github.com/leomeyer/Arducom/tree/master/src/slave/hello-world

For the bare necessities, have a look at:

https://github.com/leomeyer/Arducom/blob/master/src/slave/minimal/minimal_arducom.ino

For a list of possible hardware setups, go to:

https://github.com/leomeyer/Arducom/tree/master/doc/setups.md

Protocol description
--------------------

The Arduino running the Arducom library code is called the "slave". The device calling the Arduino is dubbed the "master".

Arducom operates using a block oriented protocol, i. e. data is transferred in packets.
The maximum length of a packet is defined by the underlying transport layer;
for example, I2C supports up to 32 bytes. This is also the recommended packet size.
Bigger packets increase the RAM demand on the Arduino and are not recommended.

The Arduino acting as a slave listens to commands from the master. It replies to each
command with either an error code or an acknowledge code, followed by optional data.
The command codes can be freely chosen from the range of 1 - 126. The meaning of the
command codes can be defined by the implementation. There is a number of pre-defined
command implementations that can be used to support e. g. reading from and writing to the EEPROM;
you can freely choose the command code numbers you want to assign to these functions.

Commands and replies consist of at least two bytes, the command code, a payload length byte, 
an optional checksum byte, and an optional payload. After receiving a command, the slave
tries to find an implementation for the command code which is executed when found.
The implementation can examine the payload and send data back. In case of errors, 
or if no matching command can be found, an error message is returned. 
Error messages consist of three bytes: the error token 0xFF, the error code, and an error specific info byte.
The checksum byte is optional. The system verifies the checksum if the highest bit of the
payload length byte is set. Using a checksum makes communication a tiny bit slower but more secure.

Implementing your own commands
------------------------------

Arducom makes it easy to implement your own commands. Each command is represented by
a class that derives from the ArducomCommand class.

See hello-world.ino for a simple example:
https://github.com/leomeyer/Arducom/blob/master/src/slave/hello-world/hello-world.ino#L188

Master implementation
---------------------

The master implementation is a command line program called "arducom". 
arducom allows communicating with Arducom slaves via the command line.

arducom has a number of options:

    -t <transport>: defines the transport layer. Currently "i2c", "serial" and "tcpip" are supported.
    -d <device>: the device that is to be used for the transport, i. e. "/dev/i2c-1".
    -a <address>: the slave address. For I2C, a number between 2 and 127.
    -b <baudrate>: For serial devices, the baud rate to use. Default is 57600.
    -c <commandcode>: the numeric command code that is to be sent to the slave, between 0 and 127.
    -p <parameters>: command parameters in the input format.
    -l <delay>: the delay in milliseconds between sending and requesting data.
    -x <retries>: the number of retries in case of errors.
    -i <format>: the input format for command parameters.
    -o <format>: the output format for the received payload.
    -s <separator>: sets the input and output separators to <separator>. Default is comma (,).
    -si <separator>: sets the input separator to <separator>.
    -so <separator>: sets the output separator to <separator>.
    -v: verbose mode.
    -vv: extra verbose mode.
    --no-newline: omit newline character(s) after outputting the payload.
    -r: read input from stdin. Cannot be used together with -p.
    -n: do not use a checksum on data packets (not recommended).
    --no-interpret: do not try to interpret the result of the version command 0 (display slave information).

For the most current parameter information, use

	$ ./arducom -?
		
For input and output formats the following values are recognized:
Hex, Raw, Bin, Byte, Int16, Int32, Int64.

* Hex input/output consists of groups of two characters matching [0-9a-fA-F], optionally
separated by the respective separator. This is the default setting.

* Raw input/output consists of raw bytes, i. e. strings. There is no separation.

* Bin input/output consists of strings of length 8 made of 0s and 1s, 
separated by the respective separator.

* Byte input/output consists of a sequence of numeric values in range 0..255, 
separated by the respective separator.

* Int16 input/output consists of a sequence of numeric values in range -32768..32767, 
separated by the respective separator.

* Int32 input/output consists of a sequence of numeric values in range -2147483648..2147483647, 
separated by the respective separator.

* Int64 input/output consists of a sequence of numeric values in range -2^63..2^63-1, 
separated by the respective separator.

Examples:

    ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 0
Sends the command number 0 (version command) via I2C to address 5. The test implementations on the Arduino
recognize this special command and send back slave information. By default arducom interprets this response 
and outputs something like:

	Arducom slave version: 1; Uptime: 2413668 ms; Flags: 0 (debug off); Free RAM: 292 bytes; Info: HelloWorld

Use -p to send parameters to the slave:
	
    ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 9 -o Hex -i Byte -p 0,0,4
This example sends the command number 9 via I2C to address 5 and prints the result as hex.
The command parameters are three bytes: 0x00, 0x00, 0x04.
Command 9, in case of the hello-world sketch, reads a block of data from the EEPROM, and returns the result.

Input formats can also be mixed:

    ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 10 -i Byte -p 10,0 -i Raw -p 'Hello, World!'
Sends the command number 10 via I2C to address 5. The command parameters are two bytes: 0x10, 0x00. 
The input format is then switched to Raw allowing to append additional parameter bytes as the string 'Hello, World!'.
Command 10, in case of the hello-world sketch, writes a block of data to the EEPROM. It returns nothing.

    date +"%s" | ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 22 -i Int32 -r -l 10
Outputs the current datetime as Unix timestamp and sends it to arducom which reads the value
from the command line and sends it via I2C to address 5 with command 22.
This command, in case of the hello-world sketch, updates the current time of a Real Time Clock. It returns nothing.

FTP transfer
------------

The program arducom-ftp implements a simple FTP client. It works with the hello-world.ino sketch
when an SD card is present. There are currently some limitations: arducom-ftp supports only 8.3
file names and no uploads.

arducom-ftp understands the following parameters:

    -t <transport>: defines the transport layer. Currently "i2c", "serial" and "tcpip" are supported.
    -d <device>: the device that is to be used for the transport, i. e. "/dev/i2c-1".
    -a <address>: the slave address. For I2C, a number between 2 and 127.
    -b <baudrate>: For serial devices, the baud rate to use. Default: 56700
    -l <delay>: the delay in milliseconds between sending and requesting data.
    -v: verbose mode.
    -vv: extra verbose mode.
    -x <retries>: the number of retries in case of errors.
    -n: do not use a checksum on data packets (not recommended).

For the most current parameter information, use

	$ ./arducom-ftp -?
	
Example:

    ./arducom-ftp -t serial -d /dev/ttyACM0 -x 3
	
This example connects to the slave using the serial device ttyACM0 specifying 3 retries.

First, arducom-ftp will try to connect to the slave. If successful, a message will be displayed:

    Connected. SD card type: SD1  FAT16 Size: 127 MB

You can list files and directories using "dir" or "ls". To change the current folder, use "cd _folder_". 
You can specify only one directory level at at time. To change a directory up, use "cd ..". 
To change to root, use "cd /" or "reset".

To retrieve files, use "get _file_". If a file with the same name already exists on the master and
the variable "continue" is on (default), the download starts after the last position if possible and the 
downloaded content is appended to the existing file. If you use "set continue off" files are always overwritten.

To change the number of retries, use "set retries _n_".
To change the command delay, use "set delay _n_" with n in milliseconds.

"help" displays a list of commands and some more information.
