A selection of possible Arducom setups
======================================

Connecting using a USB cable
----------------------------

The simplest setup would be:

![Raspberry Pi - USB - Arduino Uno](Raspberry-USB-Arduino.png)

Of course, you can also use a normal PC or laptop instead of a Raspberry Pi.

This setup is recommended for getting started.

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

On older Raspberries such as Model A and Model B, this I2C port is accessible as device /dev/i2c-0.
On more recent Raspberries the device is /dev/i2c-1. Please check which one applies to your setup.

Arducom uses a modified Wire library that does not enable the Arduino's pullup resistors. This means
that it is safe to connect those two devices in this way.

WLAN connection (using an ESP8266)
----------------------------------

![ESP8266 - Arduino Uno](ESP8266-Arduino.png)

The ESP8266-01 requires a slightly more complicated setup due to the necessary pullup resistors.
Do not use the Arduino's 3.3 volts for powering the WLAN module but an external power supply.
If you need to program the ESP8266 you can add a programming switch that you have to close during startup.
By loading the "BareMinimum" sketch to the Arduino you can then program the ESP8266 via the Arduino's USB port.
You can power the Arduino via the USB port or using an external power source.

Arducom provides a test sketch in the esp8266 folder (TODO).
This test sketch works with the standard ESP8266 firmware (using AT commands).
Please modify the test sketch by setting your WLAN SSID and password for the ESP8266 to connect.

Real Time Clock and SD card
---------------------------

For additional features, use a data logging shield such as this:

![Keyes Data Logging Shield](Keyes-Data-Logging-Shield.png)

If you connect to this shield from a Raspberry Pi via standard hardware I2C you have to remove the two I2C pullup resistors.
Not doing so can damage your Raspberry's IO ports! To avoid this problem you can use software I2C on two different pins.
