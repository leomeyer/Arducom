#! /bin/bash

# requires package libssl-devel for the crypto functions
g++ ArducomMaster.cpp ArducomMasterSerial.cpp ArducomMasterTCPIP.cpp arducom.cpp -o arducom -W -Wall -Wextra -std=c++11 -Wno-unused-parameter -lrt -lcrypto -pthread
