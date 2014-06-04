#!/bin/bash

#sox -q $1 -r 44100 -b 16 -e signed-integer -c 2 -t raw - \
#	> sample.txt


# wypisuje wszytsko na stdout
sox -q $1 -r 44100 -b 16 -e signed-integer -c 2 -t raw - | \
   ./client -s localhost 

#sox -q $1 -r 44100 -b 16 -e signed-integer -c 2 -t raw - | \
#   ./client -s 193.0.96.129 

# oryginalny
#sox -q $1 -r 44100 -b 16 -e signed-integer -c 2 -t raw - | \
#   ./client -s localhost > /dev/null   
#