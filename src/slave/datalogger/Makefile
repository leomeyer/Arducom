
# set compile time as current UNIX timestamp
CPPFLAGS += -DCOMPILE_TIME=`date '+%s'`
CXXFLAGS += -fno-strict-aliasing
ARDUINO_DIR = /usr/share/arduino
BOARD_TAG    = uno
ARDUINO_PORT = /dev/ttyUSB*
ARDUINO_LIBS = SPI SoftwareSerial DHTlib WSRTClib SdFat SdFat/utility WSWire WSWire/utility EEPROM Arducom
include /usr/share/arduino/Arduino.mk
