/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/

#ifndef HEADER
#define HEADER

#include <event2/event.h>
#include <event2/util.h>

#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h> 
#include <pthread.h> 
#include <netinet/tcp.h> //nagle

// snprintf
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include <sys/syscall.h>

#include <string.h> /* strcmp */

#include "err.h"

#include <time.h>
#include <math.h> 
#include <sys/types.h> 

#define TRUE 1
#define FALSE 0


#ifndef max
#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#ifndef min
#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )
#endif 


#endif 