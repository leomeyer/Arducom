#!/bin/bash

# This script is used to periodically download sensor data from a data logger.

while :
do
	./get_data.sh
	sleep 5
done


