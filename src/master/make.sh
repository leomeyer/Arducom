#! /bin/bash

g++ ArducomMaster.cpp ArducomMasterI2C.cpp ArducomMasterSerial.cpp arducom.cpp -o arducom -W -Wall -Wextra -std=c++11 -Wno-unused-parameter -lrt -pthread