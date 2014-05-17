/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/


/*
 Program uruchamiamy z dwoma parametrami: nazwa serwera i numer jego portu.
 Program spróbuje połączyć się z serwerem, po czym będzie od nas pobierał
 linie tekstu i wysyłał je do serwera.  Wpisanie BYE kończy pracę.
*/

//LIBEVENT

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/util.h>

#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

//LIBEVENT - END


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h> /* strcmp */
#include "err.h"




//LIBEVENT

#define BUF_SIZE 40 

 // END: LIBEVENT


#define BUFFER_SIZE 1024
#define NAME_SIZE 1000 //ile to ma byc?

#define RETRANSMIT_LIMIT 10 

#define DEBUG 1 

static const char bye_string[] = "BYE";

//uzgodnic typy!
int port_num;
char server_name[NAME_SIZE];
int retransfer_lim = RETRANSMIT_LIMIT;

void get_parameters(int argc, char *argv[]) {
    
    int server_name_set = 0;
    int j;
    for (j = 1; j < argc; j++)  
    {
        if (strcmp(argv[j], "-p") == 0)  
        {
            port_num = atoi(argv[j]); // to jest w ogóle opcjonalne -> PSRAWDZ!!!
        }
        else if (strcmp(argv[j], "-s") == 0)
        {    
            strcpy(server_name, argv[j]);
            server_name_set = 1;
        }
        else if (strcmp(argv[j], "-X") == 0)
        {
            retransfer_lim = atoi(argv[j]);
        }
    }
    //chwilowo
    //if (!server_name_set) {
    //    syserr("Client usage: -s [server_name](obligatory) -p [port_num] -X [retransfer_limit]\n");
    //}
    if (DEBUG) {            
        printf("port_num: %d\n", port_num);
        printf("server_name: %s\n", server_name);
        printf("retransfer_lim: %d\n", retransfer_lim); 
    }
}


//LIBEVENT 

struct event_base *base;
struct bufferevent *bev;

void stdin_cb(evutil_socket_t descriptor, short ev, void *arg)
{
  //printf("Czytanie z stdin\n");
  unsigned char buf[BUF_SIZE+1];


  int r = read(descriptor, buf, BUF_SIZE);
  if(r < 0) syserr("read (from stdin)");
  if(r == 0) {
    fprintf(stderr, "stdin closed. Exiting event loop.\n");
    if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
    return;
  }
  if(bufferevent_write(bev, buf, r) == -1) syserr("bufferevent_write");

}

void a_read_cb(struct bufferevent *bev, void *arg)
{  
  //printf("Sprawdza, czy jest cos do przeczytania w buforze\n");
  char buf[BUF_SIZE+1];
  while(evbuffer_get_length(bufferevent_get_input(bev))) {
    //printf("wchodzi w petle czytajaca INPUT\n");
    int r = bufferevent_read(bev, buf, BUF_SIZE);
    if(r == -1) syserr("bufferevent_read");
    buf[r] = 0;
    printf("\n--> %s\n", buf);
    printf("wielkosc = %d\n",r);
  }

}

void an_event_cb(struct bufferevent *bev, short what, void *arg)
{
  printf("An event cb\n");
  if(what & BEV_EVENT_CONNECTED) {
    fprintf(stderr, "Connection made.\n");
   /* unsigned char buf2[BUF_SIZE+1];
    while(evbuffer_get_length(bufferevent_get_input(bev))) {
        printf("wchodzi w petle czytajaca INPUT od servera\n");
        int r = bufferevent_read(bev, buf2, BUF_SIZE);
        if(r == -1) syserr("bufferevent_read");
        buf2[r] = 0;
        printf("cliendid: %s\n", buf2);
    }*/
    return;
  }
  if(what & BEV_EVENT_EOF) {
    fprintf(stderr, "EOF encountered.\n");
  } else if(what & BEV_EVENT_ERROR) {
    fprintf(stderr, "Unrecoverable error.\n");
  } else if(what & BEV_EVENT_TIMEOUT) {
    fprintf(stderr, "A timeout occured.\n");
  }
  if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
}


void read_CLIENT_datagram(uint32_t *clientid) {
    printf("read_CLIENT_datagram\n");
    char buf[BUF_SIZE+1];
    while(evbuffer_get_length(bufferevent_get_input(bev))) {
    //printf("wchodzi w petle czytajaca INPUT\n");
    int r = bufferevent_read(bev, buf, BUF_SIZE);
    if(r == -1) syserr("bufferevent_read");
    buf[r] = 0;
    printf("cliendid: %s\n", buf);
    //przypisanie na clientid
  }   
}

// END: LIBEVENT




int main (int argc, char *argv[]) {
    int rc;
    int sock;
    struct addrinfo addr_hints_poll, *addr_result;
    char line[BUFFER_SIZE];

    if (DEBUG && argc == 1) {
        printf("Client run with parameters: -s [server_name](obligatory) -p [port_num] -X [retransfer_limit]\n");
    }


    get_parameters(argc, argv);


// LIBEVENT

    uint32_t clientid;

//M: tworze kontekst
  base = event_base_new();
  if(!base) syserr("event_base_new");

  //M: tworze zdarzenie
  // Gdy zamiast deskryptora pliku przekazujemy -1, oznacza to, że deskryptor pliku zostanie podany później

  /* Wywołanie tej funkcji powoduje stworzenie struktury opisującej zdarzenia powiązanej
   z deskryptorem i dwoma buforami - jednym do przechowywania danych wejściowych, drugim 
   - wyjściowych. Bufory te są typu struct evbuffer, 
  a wskaźniki do nich możemy uzyskać, korzystając z funkcji bufferevent_get_input(struct bufferevent *) 
  i bufferevent_get_output(struct bufferevent *) */
  bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
  if(!bev) syserr("bufferevent_socket_new");

  /* Funkcje, które mają zostać wywołane po wystąpieniu zdarzenia, ustalamy w wywołaniu bufferevent_setcb() */
  bufferevent_setcb(bev, a_read_cb, NULL, an_event_cb, (void *)bev);

  //TODO

  struct addrinfo addr_hints = {
    .ai_flags = 0,
    .ai_family = AF_INET,
    .ai_socktype = SOCK_STREAM,
    .ai_protocol = 0,
    .ai_addrlen = 0,
    .ai_addr = NULL,
    .ai_canonname = NULL,
    .ai_next = NULL
  };
  struct addrinfo *addr;

  if(getaddrinfo("localhost", "4242", &addr_hints, &addr)) syserr("getaddrinfo");

/* Pierwszym parametrem jest zdarzenie związane z buforem, następne dwa są takie 
   jak w systemowym connect(). Jeśli przy tworzeniu zdarzenia nie podaliśmy gniazda,
   to ta funkcja stworzy je dla nas. <- DYNAMICZNE TWORZENIE GNIAZD!*/
  if(bufferevent_socket_connect(bev, addr->ai_addr, addr->ai_addrlen) == -1)
    syserr("bufferevent_socket_connect");
  freeaddrinfo(addr);

  /* Samo podanie wskaźników do funkcji nie aktywuje ich, do tego używa się funkcji bufferevent_enable() */
  if(bufferevent_enable(bev, EV_READ | EV_WRITE) == -1)
    syserr("bufferevent_enable");

  read_CLIENT_datagram(&clientid);  

  struct event *stdin_event =
    event_new(base, 0, EV_READ|EV_PERSIST, stdin_cb, NULL); // 0 - standardwowe wejscie
  if(!stdin_event) syserr("event_new");
  if(event_add(stdin_event,NULL) == -1) syserr("event_add");
  

    printf("[PID: %d] Jestem procesem potomnym, to ja zajme sie obsluga TCP\n",getpid());
    printf("Entering dispatch loop.\n");
    if(event_base_dispatch(base) == -1) syserr("event_base_dispatch");
    printf("Dispatch loop finished.\n");

    bufferevent_free(bev);
    event_base_free(base);




 

// END: LIBEVENT



































    sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        syserr("socket");
    }

  /* Trzeba się dowiedzieć o adres internetowy serwera. */
/*  memset(&addr_hints_poll, 0, sizeof(struct addrinfo));
  addr_hints_poll.ai_flags = 0;
  addr_hints_poll.ai_family = AF_INET;
  addr_hints_poll.ai_socktype = SOCK_STREAM;
  addr_hints_poll.ai_protocol = IPPROTO_TCP;
*/
  /* I tak wyzerowane, ale warto poznać pola. */
/*  addr_hints_poll.ai_addrlen = 0;
  addr_hints_poll.ai_addr = NULL;
  addr_hints_poll.ai_canonname = NULL;
  addr_hints_poll.ai_next = NULL;
  rc =  getaddrinfo(argv[1], argv[2], &addr_hints_poll, &addr_result);
  if (rc != 0) {
    printf("rc=%d\n", rc);
    syserr("getaddrinfo: %s\n", gai_strerror(rc));
  }

  if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) != 0) {
    syserr("connect");
  }
  do {
    printf("line:");
    fgets(line, sizeof line, stdin);
    if (write(sock, line, strlen (line)) < 0)
      perror("writing on stream socket");
  }
  while (strncmp(line, bye_string, sizeof bye_string - 1));
  if (close(sock) < 0)
    perror("closing stream socket");
*/
  return 0;
}

/*EOF*/
