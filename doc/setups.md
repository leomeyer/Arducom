A selection of possible Arducom setups
======================================

Connecting using a USB cable
----------------------------

The simplest setup would be:

![Raspberry Pi - USB - Arduino Uno](Raspberry-USB-Arduino.png)

Of course, you can also use a normal PC or laptop instead of a Raspberry Pi.

Direct serial connection
------------------------

![Raspberry Pi - UART - Arduino Uno](Raspberry-UART-Arduino.png)

Note that for other serial ports, such as those of PCs, you need a voltage level converter.

On a Raspberry Pi with Raspbian, this serial port is the device /dev/ttyAMA0. Depending on your configuration
you will have to prevent the system from occupying this port at startup.
For an example how to do this see here: https://www.raspberrypi.org/forums/viewtopic.php?f=44&t=15683

I2C connection
--------------

![Raspberry Pi - I2C - Arduino Uno](Raspberry-I2C-Arduino.png)

I2C needs to be enabled for this setup to function. How to do this depends on your Raspberry Pi model
and your operating system. Please check the web on how to do this for your particular situation.

WLAN connection (using an ESP8266)
----------------------------------

![ESP8266 - Arduino Uno](ESP8266-Arduino.png)

The ESP9266-01 requires a slightly more complicated setup due to the required pullup resistors.
Do not use the Arduino's 3.3 volts for powering the WLAN module but an external power supply.
If you need to program the ESP8266 you can add a programming switch that you have to close during startup.
By loading the "BareMinimum" sketch to the Arduino you can then program the ESP8266 via the Arduino's USB port.

Arducom provides a test sketch in the esp8266 folder (TODO).

Real Time Clock and SD card
---------------------------

For additional features, use a data logging shield such as this:

![Keyes Data Logging Shield](Keyes-Data-Logging-Shield.png)

If you connect to this shield from a Raspberry Pi via I2C you have to remove the two I2C pullup resistors.
Not doing so can damage your Raspberry's IO ports!
