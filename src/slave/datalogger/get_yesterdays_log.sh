#!/bin/bash

# This script can be used to download yesterday's log file from the logger.

# Arducom connection settings
ARDUCOM_PATH=~/Arducom/src/master
TRANSPORT=i2c
DEVICE=/dev/i2c-1
ADDRESS=5
BAUDRATE=57600
DELAY=20
RETRIES=10

# mail settings
MAILRECEIVER=$1

# target folder, must contain a trailing slash
TARGETDIR=~/Logfiles/

# grep validation regex; a log file is invalid if a line does not match this pattern
VALIDPATTERN='^\([0-9]*;\)\{11\}$'

# Upload settings for SCP
REMOTE=leo@fileserver
REMOTEDIR='~/extdata1/Data/opdidsrv1/Datalogger'

# download using arducom-ftp
# parameters:
# $1: remaining number of retries
# $2: filename to download
function download {
	TARGETFILE=$2
	# check retries
	if (( $1 > 0 )); then
		# download file
		echo "get $TARGETFILE" | $ARDUCOM_PATH/arducom-ftp -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -l $DELAY -x $RETRIES
		CODE=$?
		# if an error occurred, or the file is empty, retry recursively
		if (( $CODE == 0 )); then
			if [ -s $TARGETFILE ]; then
				# move file
				mv $TARGETFILE $TARGETDIR
				return
			fi	
		fi
		retries=`expr $1 - 1`

		download $retries $2
	fi
}

# send a mail with a message
# parameters:
# $1: subject
# $2: message
function send_mail {
	echo "Sending mail: $1"
	echo $2
	
	echo "$2" | mail -s "$1" $MAILRECEIVER
}

# main program

# cd to tempfs to reduce SD card wear
cd /var/tmp

FILENAME=`date --date="yesterday" +"%Y%m%d"`.log

download 1 $FILENAME

FILE=$TARGETDIR$FILENAME

# check whether the file could be downloaded
if [ -s $FILE ]; then

	# correct newlines (DOS to UNIX)
	sed -i -e's/\r//' $FILE
					
	# validate file
	grep -i -v $VALIDPATTERN $FILE
	
	# pattern found (invalid due to inversion)?
	if [ "$?" == "0" ]; then
		send_mail "Data logger file invalid" "The file $FILENAME is corrupt! Please check the SD card."
		exit
	fi		

else
	# file not downloaded (logger or connection problem)
	# inform per mail
	send_mail "Data logger download failed" "The file $FILENAME could not be downloaded! Please check the connection."
	exit
fi

send_mail "Data logger download succeeded" "The file $FILENAME has been downloaded successfully."

# upload data

scp $FILE $REMOTE:$REMOTEDIR
