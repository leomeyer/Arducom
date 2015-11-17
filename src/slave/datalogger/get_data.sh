#!/bin/bash

# This script can be used to read sensor data from the data logger.
# It queries the specified Arducom device for current values and stores them in a separate folder.
# The content of this folder is then uploaded to a target host via scp.
# scp requires that a key-based login has been setup for ssh. For instructions see:
# http://www.linuxproblem.org/art_9.html

# Arducom connection settings
ARDUCOM_PATH=~/Arducom/src/master
TRANSPORT=i2c
DEVICE=/dev/i2c-1
ADDRESS=5
BAUDRATE=57600
DELAY=10
RETRIES=10

# target folder
TARGETDIR=SensorData

# Upload settings for SCP
REMOTE=<remote_user>@<remote_host>
REMOTEDIR=<remote_path>

# queries using arducom
# parameters:
# $1: remaining number of retries
# $2: command to execute
# $3: parameters (in hex)
# $4: output format
# $5: target filename
function query {
	TARGETFILE=$TARGETDIR/$5
	# check retries
	if (( $1 > 0 )); then
		# query data
		$ARDUCOM_PATH/arducom -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -l $DELAY -x $RETRIES -c $2 -p $3 -o $4 >$TARGETFILE
		CODE=$?
		# if file is empty, or an error occurred, retry recursively
		if (( $CODE == 0 )); then
			return
		fi
		if [ -s $TARGETFILE ]; then
			return
		fi
		retries=`expr $1 - 1`

		query $retries $2 $3 $4 $5
	fi
}


# main program

# clear target directory
rm $TARGETDIR/*

# perform queries

query 1 20 000004 Int32 momPhase1
query 1 20 040004 Int32 momPhase2
query 1 20 080004 Int32 momPhase3
query 1 20 0C0004 Int32 momTotal
query 1 20 100008 Int64 totalKWh
query 1 20 180002 Int16 DHTAtemp
query 1 20 1A0002 Int16 DHTAhumid

# upload data

scp $TARGETDIR/* $REMOTE:$REMOTEDIR
