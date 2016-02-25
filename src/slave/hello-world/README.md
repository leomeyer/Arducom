Getting started with the Arducom hello-world example
====================================================

The recommended system for this sketch is an Arduino Uno with a DS 1307 Real Time Clock and an SD card shield.

The simplest setup would be:

![Raspberry Pi - USB - Arduino Uno](../../../doc/Raspberry-USB-Arduino (Small).png)

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

Change into the hello-world folder and check the Makefile. Make sure that you have set the correct BOARD_TAG ("uno" by default).
If RTC and/or SD card are not present, the sketch will automatically disable the respective functions.

Build the sketch:

    $ cd Arducom/src/slave/hello-world
    $ make

The make command should now produce output similar to this:

    ...

	Program:   22886 bytes (69.8% Full)
	(.text + .data + .bootloader)

	Data:       1277 bytes (62.4% Full)
	(.data + .bss + .noinit)

Check that the Arduino's serial port is correctly set in the Makefile.
Upload the compiled sketch to your Arduino:

    $ make upload

The default hello-world sketch uses serial communication at 57600 baud. To test the sketch go to the folder ~/Arducom/src/master. Build the programs:

    $ ./make.sh && ./make-ftp.sh

These programs require C++11. On Linux, use GCC 4.8 or newer. You may have to install libssl-dev:

	$ sudo apt-get install libssl-dev
	
Testing the hello-world example
-------------------------------
	
Issue the Arducom version request (command 0). Your serial port may be different from /dev/ttyACM0 (check this first).

    $ ./arducom -t serial -d /dev/ttyACM0 -c 0

arducom will try to interpret the reply. The correct output is something like:

    Arducom slave version: 1; Uptime: 175696 ms; Flags: 0 (debug off); Free RAM: 289 bytes; Info: HelloWorld

If you only receive error messages of type "No data" you may have a problem that occurs with some serial over USB drivers.
These drivers use the DTR line when opening the serial port, and this causes the Arduino to reset. The command will then be lost.
Unfortunately there is no easy software fix for this. You can either disable the reset in hardware, but this also prevents
automatically uploading new code. Or you can use the Arducom parameter "--initDelay 3000" with each command.
Arducom will then wait for 3000 milliseconds until it issues its command to give the Arduino time to reset itself.
Naturally, for a serious data logger you can't use this because you would not want the device to reset on connecting.
Alternatively you can try a different hardware serial port or USB to serial converter.

Some Arduinos exhibit flaky behavior over their serial USB connection resulting in dropped or corrupted bytes.
If it doesn't work right away try again several times and it might eventually work.

Accessing a Real Time Clock
---------------------------

If you have a DS1307 Real Time Clock connected you can query its current time:

    $ date -d @`./arducom -t serial -d /dev/ttyACM0 -c 21 -o Int32`

The output should be something like:

    Di 1. Sep 13:21:34 CEST 2015

If the RTC has not yet been set you can use the following command to set it:

    $ date +"%s" | ./arducom -t serial -d /dev/ttyACM0 -c 22 -i Int32 -r

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

    $ ./arducom -t serial -d /dev/ttyACM0 -c 7 -p 0A00

To write four bytes to address 02 of the (predefined) RAM array issue the following command:

    $ ./arducom -t serial -d /dev/ttyACM0 -c 20 -p 020001020304

To read them out again use:

    $ ./arducom -t serial -d /dev/ttyACM0 -c 19 -p 020004
	
To avoid having to reverse bytes you can combine multiple input formats. For example, the last command could
also be written as:

    $ ./arducom -t serial -d /dev/ttyACM0 -c 19 -i Int16 -p 2 -i Byte -p 4
	
This causes arducom to construct a payload of three bytes length: first the 16-bit value of 2 and the byte value 4.
	
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

	$ ./arducom -t serial -d /dev/ttyACM0 -c 31 -i Bin -p 11111100,11111100 -o Bin
	
This command will respond with the current directional state. To query this state, use:

	$  ./arducom -t serial -d /dev/ttyACM0 -c 30 -o Bin
	
To set or query the state of the pins use commands 33 and 32 in a similar fashion.
	
Accessing files on an SD card
-----------------------------

If you have a FAT-formatted SD card connected you can access it using the arducom-ftp command:

    $ ./arducom-ftp -t serial -d /dev/ttyACM0

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
In these cases try to increase the number of retries using the command line option -x _number_ with _number_ being something around 3. 
You can also set a delay time which is applied after each command to give the device some time to prepare the reply, 
using the option -l _delay_ with _delay_ in milliseconds. Sensible values are from 10 to 50. 
Be aware that this will slow down FTP file transfers because these use lots of requests. It is better to increase the number of retries.

You can also set these options interactively in arducom-ftp by using "set retries _number_" and "set delay _delay_" respectively.

If you're swapping SD cards you have to reset the Arduino before connecting again.

More information
----------------

Help can be obtained by starting arducom or arducom-ftp with parameter '-h' or '-?'.

arducom-ftp supports a 'help' command.

To see what is going on under the hood you can switch both programs to verbose mode using '-v',
or to extra verbose mode using '-vv'.
