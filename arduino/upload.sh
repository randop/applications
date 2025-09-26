#!/bin/sh
avrdude -F -V -c arduino -p atmega328p -P /dev/ttyUSB0 -b 115200 -U flash:w:/home/johnpaul/my_arduino_project/build/blink.hex:i
