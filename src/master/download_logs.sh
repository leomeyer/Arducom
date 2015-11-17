#!/bin/bash

TRANSPORT=i2c
DEVICE=/dev/i2c-1
ADDRESS=5
BAUDRATE=57600
DELAY=20
RETRIES=5

set -e

# get list of files to download
echo "dir" | ./arducom-ftp -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -x $RETRIES -l $DELAY > ftpdir

# go through directory file line by line, extract filenames

while IFS= read -r line; do

	# take only the file name
	FILENAME=`echo $line | cut -c 1-12`
	if [[ $FILENAME == *.log ]]; then
		echo "Preparing download: $FILENAME"
		echo "get $FILENAME" | ./arducom-ftp -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -x $RETRIES -l $DELAY
	fi

done < ftpdir

rm ftpdir
