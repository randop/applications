#!/bin/sh
[ ! -e /tmp/log.txt ] && touch /tmp/log.txt
chmod a+r /tmp/log.txt
stdbuf -oL journalctl -ef >>/tmp/log.txt
