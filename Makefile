# Monika Konopka, 334666,
# Data: 13.05.2014r. 

CC = gcc
CFLAGS = -Wall -g -pthread 
TARGETS = client server server1

all: $(TARGETS) 

client: client.o err.o err.h header.h
	$(CC) $(CFLAGS) $^ -o $@ -levent -lm 

server: server.o err.o err.h mixer.h header.h
	$(CC) $(CFLAGS) $^ -o $@ -levent -lm

server1: server1.o err.o err.h mixer.h header.h
	$(CC) $(CFLAGS) $^ -o $@ -levent -lm

clean:
	rm -f *.o $(TARGETS) 
