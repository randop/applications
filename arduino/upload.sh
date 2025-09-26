#!/bin/sh
avrdude -F -V -c arduino -p atmega328p -P /dev/ttyUSB1001 -b 115200 -U flash:w:/home/johnpaul/my_arduino_project/build/main.hex:i
