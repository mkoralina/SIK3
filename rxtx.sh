#!/bin/bash

sox -q "wild.mp3" -r 44100 -b 16 -e signed-integer -c 2 -t raw - | \
   ./client -s localhost "$@" | \
   aplay -t raw -f cd -B 5000 -v -D sysdefault -
