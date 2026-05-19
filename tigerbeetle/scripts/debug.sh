#!/bin/sh

gdb -batch -ex run -ex 'bt' --args .build/service
