#!/bin/sh

# Script for setting data logger time and zone
# To be executed e. g. once per hour to correct time drift and daylight saving time changes.
# It's probably best to automate this with cron like this:
# 1 * *  *   *     <path>/set_datalogger_time.sh

ARDUCOM=Arducom/src/master/arducom
TRANSPORT=i2c
DEVICE=/dev/i2c-1
ADDRESS=5
BAUDRATE=57600
TMPFOLDER=/var/tmp
CHECKFILE=$TMPFOLDER/timeset.txt

rm -rf $CHECKFILE

# fail on errors
set -e

# set current time
date +"%s" | $ARDUCOM -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -c 22 -o Int32 -l 10 -i Int32 -r

# determine current time zone offset
date +%z | cut -c 1-3 | sed s/+// > $TMPFOLDER/tzhours
date +%z | cut -c 4-5 > $TMPFOLDER/tzminutes

# the following calculations require calc (sudo apt-get install apcalc)

# calculate minutes to a fraction of an hour
calc "`cat $TMPFOLDER/tzminutes` / 60" > $TMPFOLDER/tzhourfrac

# calculate time zone offset in seconds
# if hours are negative, subtract fraction
if [ `cat $TMPFOLDER/tzhours` -lt "0" ]; then
	calc "3600 * `cat $TMPFOLDER/tzhours` - 3600 * `cat $TMPFOLDER/tzhourfrac`" > $TMPFOLDER/tzoffset
else
	calc "3600 * `cat $TMPFOLDER/tzhours` + 3600 * `cat $TMPFOLDER/tzhourfrac`" > $TMPFOLDER/tzoffset
fi

# send time zone offset to data logger
$ARDUCOM -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -l 10 -x 5 -c 10 -p 2000 -i Int16 -p `cat $TMPFOLDER/tzoffset`

# read current time and offset from data logger to the check file
date -d @`$ARDUCOM -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -c 21 -o Int32 -l 10` > $CHECKFILE
$ARDUCOM -t $TRANSPORT -d $DEVICE -a $ADDRESS -b $BAUDRATE -c 9 -p 200002 -o Int16 >> $CHECKFILE

# cleanup
rm $TMPFOLDER/tzhours
rm $TMPFOLDER/tzminutes
rm $TMPFOLDER/tzhourfrac
rm $TMPFOLDER/tzoffset
