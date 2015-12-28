#!/bin/bash

# This script can be used to read sensor data from the data logger.
# It queries the specified Arducom device for current values and stores them in a separate folder.
# The content of this folder is then uploaded to a target host via scp.
# scp requires that a key-based login has been setup for ssh. For instructions see:
# http://www.linuxproblem.org/art_9.html
# Retrieved values are only uploaded if they validate. This is an important detail:
# Values being queried from a device may be periodically reset and then re-read. If the
# value is being queried in between an invalid value is returned. In this case the file
# may not be uploaded in order to not confuse the receiver. Each particular value is therefore
# validated against a number specifying the expected invalid value for the value.

# Arducom connection settings
ARDUCOM_PATH=~/Arducom/src/master
TRANSPORT=i2c
DEVICE=/dev/i2c-1
ADDRESS=5
BAUDRATE=57600
DELAY=20
RETRIES=10

# target folder
TARGETDIR=/var/tmp/SensorData

# upload settings for SCP
REMOTE=localhost
REMOTEDIR=/var/opdid

# queries using arducom
# parameters:
# $1: remaining number of retries
# $2: command to execute
# $3: parameters (in hex)
# $4: output format
# $5: target filename
# $6: validation (retrieved value must be greater than this value)
function query {
	TARGETFILE=$TARGETDIR/$5
	# check retries
	if (( $1 > 0 )); then
		# echo -n "Querying data... "
		# query data
		$ARDUCOM_PATH/arducom -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -l $DELAY -x $RETRIES -c $2 -p $3 -o $4 >$TARGETFILE
		CODE=$?
		# if an error occurred, or the file is empty, or contents do not validate, retry recursively
		if (( $CODE == 0 )); then
			if [ -s $TARGETFILE ]; then
				# check validation value
				if (( `cat $TARGETFILE` > $6 )); then
					# echo "OK."
					return				
				#else
					# no message (this is a common case)
				fi
			else
				echo "An error occurred at: `date` (File is empty)"
			fi	
		else
			echo "An error occurred at: `date` (Code: $CODE)"
		fi
		
		# not validated, do not keep the file
		rm $TARGETFILE
		
		retries=`expr $1 - 1`

		query $retries $2 $3 $4 $5 $6
	fi
}


# main program

# clear target directory
rm -rf $TARGETDIR
mkdir $TARGETDIR

# perform queries

query 1 20 000004 Int32 momPhase1 -1
query 1 20 040004 Int32 momPhase2 -1
query 1 20 080004 Int32 momPhase3 -1
query 1 20 0C0004 Int32 momTotal -1
query 1 20 100008 Int64 totalKWh -1
query 1 20 180002 Int16 DHTAtemp -9999
query 1 20 1A0002 Int16 DHTAhumid -9999
query 1 20 200008 Int64 GasCounter 0

# upload data using scp
#scp $TARGETDIR/* $REMOTE:$REMOTEDIR

# direct copy if on the same host
cp $TARGETDIR/* $REMOTEDIR
