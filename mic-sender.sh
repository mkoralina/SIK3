#!/bin/bash

arecord -v -t raw -f cd -B 100000 -D sysdefault | \
   ./client -s localhost "$@" > /dev/null
