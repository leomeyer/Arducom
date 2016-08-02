#!/bin/bash

# This script can be used to download yesterday's log file from the logger.

# global number of retries (not Arducom connection retries)
FAILOVER_RETRIES=3

# Arducom connection settings
ARDUCOM_PATH=~/Arducom/src/master
TRANSPORT=i2c
DEVICE=/dev/i2c-1
ADDRESS=5
BAUDRATE=57600
DELAY=40
RETRIES=10

# mail settings (mail address must be passed as a parameter)
MAILRECEIVER=$1
DOWNLOADDATE=$2

# validate email
echo $MAILRECEIVER | egrep '^[A-Za-z0-9](([_\.\-]?[a-zA-Z0-9]+)*)@([A-Za-z0-9]+)(([\.\-]?[a-zA-Z0-9]+)*)\.([A-Za-z]{2,})$'

if (( $? > 0 )); then
	echo "Invalid mail address. Usage: $0 <mail_address> [<file-date>]"
	exit
fi

if [[ -z "$DOWNLOADDATE" ]]; then
	DOWNLOADDATE="yesterday"
fi

# target folder, must contain a trailing slash
TARGETDIR=~/Logfiles/

# grep validation regex; a log file is invalid if a line does not match this pattern
VALIDPATTERN='^\(-*[0-9]*;\)\{11\}$'

# Upload settings for SCP
REMOTE=leo@fileserver
REMOTEDIR='~/extdata1/Data/opdidsrv1/Datalogger'

# determine target filename
FILENAME=`date -d "$DOWNLOADDATE" +"%Y%m%d"`.log
if (( $? > 0 )); then
	echo "Invalid date. Usage: $0 <mail_address> [<file-date>]"
	exit
fi

# determine debug output filename
OUTPUT_FILENAME=`date -d "$DOWNLOADDATE" +"%Y%m%d"`.out

# download using arducom-ftp
# parameters:
# $1: remaining number of retries
# $2: filename to download
function download {
	TARGETFILE=$2
	# check retries
	if (( $1 > 0 )); then
		# download file
		echo "get $TARGETFILE" | $ARDUCOM_PATH/arducom-ftp -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -l $DELAY -x $RETRIES >>$OUTPUT_FILENAME 2>&1
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
		sleep 1

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

	echo "$2" | cat - $OUTPUT_FILENAME | mail -s "$1" $MAILRECEIVER
}

# main program

# cd to tempfs to reduce SD card wear
cd /var/tmp

# remove potentially existing debug output
rm -f $OUTPUT_FILENAME

download $FAILOVER_RETRIES $FILENAME

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
