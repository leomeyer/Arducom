Getting started with the Arducom hello-world example
====================================================

The recommended system for this sketch is an Arduino Uno with a DS 1307 Real Time Clock and an SD card shield.

The simplest setup would be:

![Raspberry Pi - USB - Arduino Uno](../../../doc/Raspberry-USB-Arduino.png)

Of course, you can also use a PC or laptop instead of a Raspberry Pi.

For additional features, use a data logging shield such as this:

![Keyes Data Logging Shield](../../../doc/Keyes-Data-Logging-Shield.png)

For more setups, see https://github.com/leomeyer/Arducom/tree/master/doc/setups.md

Installing the required software
--------------------------------

Suppose you start from a clean Linux installation (e. g. Ubuntu 14.04).

    $ sudo apt-get update
    $ sudo apt-get upgrade

Install the Arduino software:

    $ sudo apt-get install arduino

Connect your Arduino. Start the Arduino IDE:

    $ arduino &

Open the Blink example. Select the serial port you use for the connection and the correct type of board.
Upload the sketch. Do not continue until you get this step working.

If you want to use additional components or shields such as a DS1307 RTC or an SD card shield,
please try the example sketches for these components before using Arducom to make sure that everything works fine.

Go to your user's home directory:

    $ cd ~

If git is not yet installed, install it:

    $ sudo apt-get install git

Clone the Arducom git repository:

    $ git clone https://github.com/leomeyer/Arducom

Building the hello-world example is done from the command line.
Install arduino-mk:

    $ sudo apt-get install arduino-mk
	
Compiling and uploading
-----------------------

Change into the hello-world folder and check the Makefile. Make sure that you have set the correct BOARD_TAG ("uno" by default). For an Arduino Mega, try "mega" for a 1280 model and "mega2560" for a 2560 model.

If you want to use an SD card shield, check the following line in hello-world.ino and adjust the pin number if necessary:

	#define SDCARD_CHIPSELECT		4

If you are using an RTC of type DS1307 you can enable it using the following macro:

	#define USE_DS1307

You can enable both RTC and SD card. The sketch will try to detect whether they are in fact present and disable unneeded Arducom commands. However, with some transport methods flash ROM usage will exceed an Uno's limit (about 32 kB). You would have to use an Arduino Mega or disable e. g. the RTC.  

Build the sketch:

    $ cd Arducom/src/slave/hello-world
    $ make

You may get a message saying that make is not yet installed. Install it:

	$ sudo apt-get install make
	
If you get an error message saying that make can't find the command "ard-parse-boards", your Arduino.mk is too old. This can happen with older Linux distributions.
Perform the following steps:

	$ sudo su
	$ cd /usr/share/arduino
	$ mv Arduino.mk Arduino.mk_old
	$ wget https://raw.githubusercontent.com/sudar/Arduino-Makefile/master/Common.mk
	$ wget https://raw.githubusercontent.com/sudar/Arduino-Makefile/master/Arduino.mk
	$ mkdir bin
	$ cd bin
	$ wget https://raw.githubusercontent.com/sudar/Arduino-Makefile/master/bin/ard-reset-arduino
	$ chmod +x ard-reset-arduino
	$ apt-get install python-pip
	$ pip install pyserial
	$ exit
	
The make command should now produce output similar to this:

    ...

	Program:   25272 bytes (77.1% Full)
	(.text + .data + .bootloader)

	Data:       1441 bytes (70.4% Full)
	(.data + .bss + .noinit)

Check that the Arduino's serial port is correctly set in the Makefile. If you are using a USB connection, this will be something like /dev/ttyUSB* or /dev/ttyACM*.
Upload the compiled sketch to your Arduino:

    $ make upload

In case of errors try 

	$ sudo make upload

If using certain clones of the Arduino Mega uploading may fail (avrdude communication timeouts). In such cases, issue the following upload command manually (example for Mega 2560):

	$ make reset && /usr/bin/avrdude -q -V -p atmega2560 -C /etc/avrdude.conf -c stk500v2 -P /dev/ttyACM0 -U flash:w:build-cli/hello-world.hex:i -b 115200 -D

The default hello-world sketch uses serial communication at 57600 baud. To test the sketch go to the folder ~/Arducom/src/master. Build the programs:

    $ ./make.sh && ./make-ftp.sh

These programs require C++11. On Linux, use GCC 4.8 or newer. Check which version of g++ is installed:

	$ g++ --version

If it is not installed, install it:

	$ sudo apt-get install g++
	
It is possible that you have a g++ version that doesn't support C++11 (i. e., a version number below 4.8). In this case you may have to install
g++ 4.8 or newer on your operation system. Please check the web on how to do this.
You may also have to change make.sh and make-ftp.sh to use your specific compiler version.

To successfully compile you may also have to install libssl-dev:

	$ sudo apt-get install libssl-dev
	
Testing the hello-world example
-------------------------------
	
Issue the Arducom version request (command 0). Your serial port may be different from /dev/ttyACM0 (check this first).

    $ ./arducom -d /dev/ttyACM0 -c 0

arducom will try to interpret the reply. The correct output is something like:

    Arducom slave version: 1; Uptime: 175696 ms; Flags: 0 (debug off); Free RAM: 289 bytes; Info: HelloWorld

Some Arduinos exhibit flaky behavior over their serial USB connection resulting in dropped or corrupted bytes.
If it doesn't work right away try again several times and it might eventually work.

To see what is going on under the hood you can switch to verbose mode using '-v',
or to extra verbose mode using '-vv'.

Accessing a Real Time Clock
---------------------------

If you have a DS1307 Real Time Clock connected you can query its current time:

    $ date -d @`./arducom -d /dev/ttyACM0 -c 21 -o Int32`

The output should be something like:

    Di 1. Sep 13:21:34 CEST 2015

If the RTC has not yet been set you can use the following command to set it:

    $ date +"%s" | ./arducom -d /dev/ttyACM0 -c 22 -i Int32 -r
	
This command transfers the current Unix timestamp (UTC) to the Arduino and sets the RTC time from it.
This means that the RTC runs on UTC, not the local time of your machine as set by the time zone.
When querying the date it is automatically converted back by the date command.

Accessing EEPROM and RAM
------------------------

You can experiment with the EEPROM (commands 1 - 10) and the RAM variables (commands 11 - 20). These commands allow you to access EEPROM and RAM. 
Some of them take parameters that you can supply on the command line using -p xx with xx being a sequence of hexadecimal byte values.

The following commands are defined:

| Command | Parameters | Meaning                                                            | Result |
|---------|------------|--------------------------------------------------------------------|--------|
| 1       | 2 bytes    | Read byte at EEPROM address                                        | byte   |
| 2       | 3 bytes    | Write byte at EEPROM address                                       | -      |
| 3       | 2 bytes    | Read Int16 at EEPROM address                                       | Int16  |
| 4       | 4 bytes    | Write Int16 at EEPROM address                                      | -      |
| 5       | 2 bytes    | Read Int32 at EEPROM address                                       | Int32  |
| 6       | 6 bytes    | Write Int32 at EEPROM address                                      | -      |
| 7       | 2 bytes    | Read Int64 at EEPROM address                                       | Int64  |
| 8       | 10 bytes   | Write Int64 at EEPROM address                                      | -      |
| 9       | 3 bytes    | Read block at EEPROM address; third byte is the length of data     | data   |
| 10      | n bytes    | Write EEPROM block data; the first two bytes are the start address | -      |
| 11      | -          | Read byte variable                                                 | byte   |
| 12      | 1 byte     | Write byte variable                                                | -      |
| 13      | -          | Read Int16 variable                                                | Int16  |
| 14      | 2 bytes    | Write Int16 variable                                               | -      |
| 15      | -          | Read Int32 variable                                                | Int32  |
| 16      | 4 bytes    | Write Int32 variable                                               | -      |
| 17      | -          | Read Int64  variable                                               | Int64  |
| 18      | 8 bytes    | Write Int64 variable                                               | -      |
| 19      | 3 bytes    | Read from RAM array; specify offset (two bytes) and length         | data   |
| 20      | n bytes    | Write to RAM array; the first two bytes are the start offset       | -      |

Multi-byte values are transferred LSB first. I. e. a value of 0x12345678 would have to be sent as 78563412.
Addresses are interpreted as 16-bit values, i. e. you have to specify the lower byte first.

For example, to read the 8 bytes at EEPROM address 0x0A issue the following command:

    $ ./arducom -d /dev/ttyACM0 -c 7 -p 0A00

To write four bytes to address 02 of the (predefined) RAM array issue the following command:

    $ ./arducom -d /dev/ttyACM0 -c 20 -p 020001020304

To read them out again use:

    $ ./arducom -d /dev/ttyACM0 -c 19 -p 020004
	
To avoid having to reverse bytes you can combine multiple input formats. For example, the last command could
also be written as:

    $ ./arducom -d /dev/ttyACM0 -c 19 -i Int16 -p 2 -i Byte -p 4
	
This causes arducom to construct a payload of three bytes length: first a 16-bit value of 2 (0002 hex) and a byte value of 4 (04 hex).
	
Accessing Arduino pins
----------------------

The hello-world sketch exposes the upper six pins of port D via Arducom (pins 0 and 1 are reserved for RX and TX).
You can set pin directions (input/output) and pin state (low/high for output and pullup resistor off/on for input).
The command numbers are 30 (read pin directions), 31 (set pin directions), 32 (read pin state), and 33 (set pin state).
The set commands accept two bytes: a bit mask and a pin pattern to set. The mask specifies which pins should actually
be changed; if a bit is 1 the pin is modified, otherwise it is left unchanged. For example:

	mask = 10000000, pattern = 11000000
	
with both values being binary, only modifies the topmost pin because its mask bit is set. The second highest pin is not
changed even though its pattern bit is set because the mask specifies that this pin is to be left untouched.

As the lowest two bits are internally masked out in this example sketch it doesn't matter what values you put into them.

Arducom supports binary for input and output to give you easy visual feedback about binary values. To switch to binary
input or output, use '-i Bin' or '-o Bin' respectively.

To set all exposed pins to output use:

	$ ./arducom -d /dev/ttyACM0 -c 31 -i Bin -p 11111100,11111100 -o Bin
	
This command will respond with the current directional state. To query this state, use:

	$  ./arducom -d /dev/ttyACM0 -c 30 -o Bin
	
To set or query the state of the pins use commands 33 and 32 in a similar fashion.

To read the value of an analog pin you can use command 35. It expects the channel number (one byte) and returns
an Int16 containing a 10-bit value. Example (reads channel 2):

	$  ./arducom -d /dev/ttyACM0 -c 35 -p 02 -o Int16
	
Accessing files on an SD card
-----------------------------

If you have a FAT-formatted SD card connected you can access it using the arducom-ftp command:

    $ ./arducom-ftp -d /dev/ttyACM0

You should get an output similar to this:

    Connected. SD card type: SD1  FAT16 Size: 127 MB
    />

Use the "dir" or "ls" commands to list the current directory. Use "get _file_" to retrieve _file_. Use "cd _folder_" to descend into a folder and "cd .." to change one level up.
Use "help" to display a list of supported commands.

It is possible that you are getting errors like this:

    Error requesting data: Timeout reading from serial device
	
or:

	No data (not enough data sent or command not yet processed, try to increase delay -l or number of retries -x)

This may be due to SD card operations requiring more time than expected.
In these cases try to increase the number of retries using the command line option -x _number_ with _number_ being something around 3 (default). 
You can also set a delay time which is applied after each command to give the device some time to prepare the reply, 
using the option -l _delay_ with _delay_ in milliseconds. Sensible values are from 10 to 50. The default is 25.
Be aware that this will slow down FTP file transfers because these use lots of requests. It is better to increase the number of retries.

You can also set these options interactively in arducom-ftp by using "set retries _number_" and "set delay _delay_" respectively.

If you're swapping SD cards you have to reset the Arduino before connecting again.

More information
----------------

Help can be obtained by starting arducom or arducom-ftp with parameter '-h' or '-?'.

arducom-ftp supports a 'help' command.

Some serial over USB drivers use the DTR line when opening the serial port, and this causes the Arduino to reset.
If arducom sends too early the command will be lost and you will receive error messages of type "No data".
Unfortunately there is no easy software fix for this. You can either disable the reset in hardware, but this also disables automatic uploading of new code. The behavior is driver specific: an Arduino connected to one PC may exhibit this problem while connected to a different machine it may not.

Arducom attempts to detect these devices by checking for the strings "ttyACM" and "ttyUSB" and if found, sets an
initialization delay of 2000 milliseconds which can be overridden using the parameter "--initDelay <milliseconds>".
Arducom will then wait for the specified time until it issues its command to give the Arduino time to reset itself.
Naturally, for a serious data logger you can't use this because you would not want the device to reset on connecting.
Alternatively you can try a hardware serial port or a different USB to serial converter.

You can find out whether the Arduino resets by checking whether the reported uptime of command 0 does not increase from call to call.
If it does not reset you can safely disable the initialization delay by specifying "--initDelay 0".
