#! /bin/bash

g++ ArducomMaster.cpp ArducomMasterI2C.cpp ArducomMasterSerial.cpp arducom-ftp.cpp -o arducom-ftp -W -Wall -Wextra -std=c++11 -Wno-unused-parameter -lrt -pthread
