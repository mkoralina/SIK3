/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/

#ifndef HEADER
#define HEADER

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include <errno.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h> 
#include <netinet/tcp.h> //nagle
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/syscall.h>
#include <time.h>
#include <math.h> 
#include <signal.h> 

#include "err.h"
#include "inttypes.h"

#define TRUE 1
#define FALSE 0

#ifndef max
#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#ifndef min
#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )
#endif 

char * addr_to_str(struct sockaddr_in6 *addr) {
    char * str = malloc(INET6_ADDRSTRLEN);
    memset(str, 0, INET6_ADDRSTRLEN);
    inet_ntop(AF_INET6, &(addr->sin6_addr), str, INET6_ADDRSTRLEN);
    return str;
}


#endif 