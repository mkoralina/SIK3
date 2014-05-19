#!/bin/bash

sox -q $1 -r 44100 -b 16 -e signed-integer -c 2 -t raw - | \
   ./client -s localhost > /dev/null
