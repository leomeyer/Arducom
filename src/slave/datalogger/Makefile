
# set compile time as current UNIX timestamp
CPPFLAGS += -DCOMPILE_TIME=`date '+%s'`
CXXFLAGS += -fno-strict-aliasing
ARDUINO_DIR = /usr/share/arduino
BOARD_TAG    = uno
ARDUINO_PORT = /dev/ttyUSB*
USER_LIB_PATH = ../lib/
ARDUINO_LIBS = SPI SoftwareSerial DHTlib WSRTClib SdFat/SdFat WSWireLib/Library/WSWire EEPROM Arducom
include Arduino.mk
