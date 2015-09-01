Getting started with the Arducom hello-world example
====================================================

Suppose we start from a clean Linux installation (e. g. Ubuntu 14.04).

    $ sudo apt-get update
    $ sudo apt-get upgrade

Install the Arduino software:

    $ sudo apt-get install arduino

Connect your Arduino. Start the Arduino IDE:

    $ arduino &

Open the Blink example. Select the serial port
you use for the connection and the correct type of board.
Upload the sketch.
Do not continue until you get this step working.

Go to your user's home directory:

    $ cd ~

If git is not yet installed, install it:

    $ sudo apt-get install git

Clone the Arducom git repository:

    $ git clone https://github.com/leomeyer/Arducom

Building the hello-world example is best done from the command line.
Install arduino-mk:

    $ sudo apt-get install arduino-mk

Change into the hello-world folder and build the sketch:

    $ cd Arducom/src/slave/hello-world
    $ make

You will probably get an error message about missing libraries.
Something like: 

    /usr/share/arduino/Arduino.mk:743: *** The following libraries specified in ARDUINO_LIBS could not be found (searched USER_LIB_PATH and ARDUINO_LIB_PATH): RTClib SdFat SdFat/utility Arducom.  Stop.

To install the libraries, change to the ~/Arducom/src/slave/lib folder:

    $ cd ../lib

Download the libraries and install them one by one by creating symbolic links:

RTClib:

    $ git clone https://github.com/adafruit/RTClib
    $ sudo ln -s ~/Arducom/src/slave/lib/RTClib /usr/share/arduino/libraries

SdFat:

    $ git clone https://github.com/greiman/SdFat
    $ sudo ln -s ~/Arducom/src/slave/lib/SdFat/SdFat /usr/share/arduino/libraries

Arducom:

    $ sudo ln -s ~/Arducom/src/slave/lib/ /usr/share/arduino/libraries/Arducom

Go back to the hello-world folder and make again:

    $ cd -
    $ make

The make command should now produce output similar to this:

    ...

    Program:   22032 bytes (67.2% Full)
    (.text + .data + .bootloader)

    Data:       1429 bytes (69.8% Full)
    (.data + .bss + .noinit)

Check that the Arduino's serial port is correctly set in the Makefile.
Upload the compiled sketch to your Arduino:

    $ make upload

The default hello-world sketch uses serial communication at 57600 baud. To test the sketch go to the folder ~/Arducom/src/master. Build the programs:

    $ ./make && ./make-ftp

Issue the Arducom version request (command 0). Your serial port may be different from /dev/ttyACM0 (check this first).

    $ ./arducom -t serial -d /dev/ttyACM0 -b 57600 -c 0

arducom will try to interpret the reply. The output is something like:

    Arducom slave version: 1; Uptime: 175696 ms; Flags: 0 (debug off); Free RAM: 289 bytes; Info: HelloWorld

If you have a DS1307 Real Time Clock connected you can query its current time:

    $ date -d @`./arducom -t serial -d /dev/ttyACM0 -b 57600 -c 21 -o Int32 -l 10`

The output should be something like:

    Di 1. Sep 13:21:34 CEST 2015

If the RTC has not yet been set you can use the following command to set it:

    $ date +"%s" | ./arducom -t serial -d /dev/ttyACM0 -b 57600 -c 22 -i Int32 -r -l 10

You can experiment with the EEPROM (commands 1 - 10) and the RAM variables (commands 11 - 20). These commands allow you to read and set EEPROM and RAM space. Some of them take parameters that you can supply on the command line using -p xx with xx being a sequence of hexadecimal byte values. The following commands are defined:

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

For example, to read the 8 bytes at EEPROM address 0x0A issue the following command:

    $ ./arducom -t serial -d /dev/ttyACM0 -b 57600 -c 7 -p 0A00

To write four bytes into the (predefined) RAM array issue the following command:

    $ ./arducom -t serial -d /dev/ttyACM0 -b 57600 -c 20 -p 000001020304

To read them out again use:

    $ ./arducom -t serial -d /dev/ttyACM0 -b 57600 -c 19 -p 000004

If you have a FAT-formatted SD card connected you can access it using the arducom-ftp command:

    $ ./arducom-ftp -t serial -d /dev/ttyACM0 -b 57600

You should get an output similar to this:

    Connected. SD card type: SD1  FAT16 Size: 127 MB
    />

Use the "dir" or "ls" commands to list the current directory. Use "get _file_" to retrieve _file_. Use "cd _folder_" to descend into a folder and "cd .." to change one level up.

It is possible that you are getting errors like this:

    Error requesting data: Timeout reading from serial device

In these cases try to increase the number of retries using the command line option -x _number_ with _number_ being something from 3 to 10. You can also set a delay time which is applied after each command to give the device some time to prepare the reply, using the option -d _delay_ with _delay_ in milliseconds. Sensible values are from 10 to 50. Be aware that this will slow down FTP file transfers because those use many requests. It is better to increase the number of retries.

You can also set these options interactively in arducom-ftp by using "set retries _number_" and "set delay _delay_" respectively.

If you're swapping SD cards you have to reset the Arduino before connecting again.

