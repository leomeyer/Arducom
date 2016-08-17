#! /bin/bash

# requires package libssl-dev for the crypto functions (openssl-devel under Cygwin)
g++ ArducomMaster.cpp ArducomMasterSerial.cpp ArducomMasterTCPIP.cpp arducom-ftp.cpp -o arducom-ftp -W -Wall -Wextra -std=c++11 -Wno-unused-parameter -lrt -lcrypto -pthread
