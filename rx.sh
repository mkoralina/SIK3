#!/bin/bash

./klient -s localhost "$@" < /dev/null | \
   aplay -t raw -f cd -B 5000 -v - -D sysdefault
