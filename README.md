Arducom - Arduino communications library
----------------------------------------

WARNING - this is pretty much work in progress. Also, documentation is not yet complete.

Arducom attempts to make communications between Arduinos and other devices easier.
It is designed to be easy to extend and flexible. Moreover, it tries to abstract
away the transport layer, i. e. data can be transferred over serial, I2C, TCP or
other connections without affecting the actual command implementations.

A common scenario is using an Arduino as a data logger with an SD card. More often
than not it is impractical to remove the SD card and insert it into a reader to
extract the data logs because this means interrupting the data logging.
Furthermore, you may want to query the Arduino for current readings from elsewhere.
Communicating over serial interfaces might not be possible because the
Arduino already uses these for data acquisition. Software serial might also be
impractical due to timing constraints.

Arducom allows you to communicate with the Arduino under these circumstances and
even transfer files to and from the Arduino's SD card.

Arducom currently supports I2C and serial communication. The Arduino running the
Arducom library code is called the slave. This slave implementation has been tested
on an Arduino Uno clone. The device calling the Arduino is dubbed the master.
This master implementation has been tested on a Raspberry Pi. 
Other systems may probably require some work.

* Protocol description
======================

Arducom operates using a block oriented protocol, i. e. data is transferred in packets.
The maximum length of a packet is defined by the underlying transport layer;
for example, I2C supports up to 32 bytes. This is also the recommended packet size.
Bigger packets increase the RAM demand on the Arduino because of the required buffers
and are not recommended.

The Arduino acting as a slave listens to commands from the master. It replies to each
command with either an error code or an acknowledge code, followed by optional data.
The command codes can be freely chosen from the range 1 - 126. The meaning of the
command codes can be defined by the implementation. There is a number of pre-defined
commands that can be used to support e. g. reading from and writing to the EEPROM.

Commands and replies consist of at least two bytes, the command code, a payload length byte, 
and an optional payload. After receiving a command, the slave tries to find an implementation
for this command code which is executed when found. The implementation can examine the
payload and send data back. In case of errors, or if no matching command can be found,
an error message is returned. Error messages consist of three bytes: the error token 0xFF,
the error code, and an implementation defined info byte.

* Implementing your own commands
================================

Arducom makes it easy to implement your own commands. Each command is represented by
a class that derives from the ArducomCommand class.

* Master implementation
=======================

The master implementation is a command line program called "arducom". 
arducom allows communicating with Arducom slaves via the command line.

arducom has a number of options:

    -t <transport>: defines the transport layer. Currently "i2c" and "serial" are supported.
    -d <device>: the device that is to be used for the transport, i. e. "/dev/i2c-1".
    -a <address>: the slave address. For I2C, a number between 2 and 127.
    -b <baudrate>: For serial devices, the baud rate to use (TODO).
    -c <commandcode>: the numeric command code that is to be sent to the slave.
    -l <delay>: the delay in milliseconds between sending and requesting data.
    -x <retries>: the number of retries in case of errors.
    -i <format>: the input format for command parameters.
    -o <format>: the output format for the received payload.
    -s <separator>: sets the input and output separators to <separator>. Default is comma (,).
    -si <separator>: sets the input separator to <separator>.
    -so <separator>: sets the output separator to <separator>.
    -v: verbose mode.
    --no-newline: omit newline character(s) after outputting the payload.
    -r: read input from stdin. Cannot be used together with -p.
    -p <parameters>: command parameters in the input format.
  
For input and output formats the following values are recognized:
Hex, Raw, Byte, Int16, Int32, Int64.
Hex input/output consists of groups of two characters matching [0-9a-fA-F], optionally
separated by the respective separator (default).
Raw input/output consists of raw bytes, i. e. strings. There is no separation.
Byte input/output consists of a sequence of numeric values in range 0..255, optionally
separated by the respective separator.
Int16 input/output consists of a sequence of numeric values in range -32768..32767, 
optionally separated by the respective separator.
Int32 input/output consists of a sequence of numeric values in range -2147483648..2147483647, 
optionally separated by the respective separator.
Int64 input/output consists of a sequence of numeric values in range -(2^63)..2^63-1, 
optionally separated by the respective separator.

Examples:

    ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 0 -o Raw
Sends the command number 0 via I2C to address 5 and prints the result as string.

    ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 9 -o Hex -i Byte -p 0,0,4
Sends the command number 9 via I2C to address 5 and prints the result as hex.
The command parameters are three bytes: 0x00, 0x00, 0x04.

Input formats can also be mixed:

    ./arducom -t i2c -d /dev/i2c-1 -a 5 -c 10 -i Byte -p 10,0 -i Raw -p 'Hello, World!'
Sends the command number 10 via I2C to address 5 and prints the result as hex.
The command parameters are two bytes: 0x10, 0x00. The input format is then switched to
Raw allowing to append additional parameter bytes as the string 'Hello, World!'.

* FTP transfer
==============

The program arducom-ftp implements a simple FTP client. It works with the hello-world.ino sketch
when an SD card is present. There are currently some limitations: arducom-ftp supports only 8.3
file names.

arducom-ftp understands the following parameters:

    -t <transport>: defines the transport layer. Currently "i2c" and "serial" are supported.
    -d <device>: the device that is to be used for the transport, i. e. "/dev/i2c-1".
    -a <address>: the slave address. For I2C, a number between 2 and 127.
    -b <baudrate>: For serial devices, the baud rate to use (TODO).
    -l <delay>: the delay in milliseconds between sending and requesting data.
    -v: verbose mode.
    -x <retries>: the number of retries in case of errors.
  
Example:

    ./arducom-ftp -t serial -d /dev/ttyACM0 -x 30
This example connects to the slave using the serial device ttyACM0 specifying 30 retries.

After start, arducom-ftp will try to connect to the slave. If successful, a message will be displayed:

Connected. SD card type: SD1  FAT16 Size: 127 MB

You can list directories using "dir" or "ls". To change a directory, use "cd". You can specify only
one directory level at at time. To change a directory up, use "cd ..". To change to root, use "cd /" or
"reset".

To retrieve files, use "get &lt;filename&gt;". If a file with the same name already exists on the master and
the variable "continue" is on (default), the file content is appended. If you use "set continue off" files
are always overwritten.

To change the number of retries, use "set retries &lt;n&gt;".
To change the command delay, use "set delay &lt;n&gt;" with n in milliseconds.

