CC = gcc
CFLAGS = -Wall
TARGETS = client server 

all: $(TARGETS) 

client: client.o err.o err.h 
	$(CC) $(CFLAGS) $^ -o $@ -levent

server: server.o err.o err.h mixer.h
	$(CC) $(CFLAGS) $^ -o $@ -levent

clean:
	rm -f *.o $(TARGETS) 
