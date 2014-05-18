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

// snprintf
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>


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
#include <regex.h>  
#include <math.h> 

//DO USUNIECIA!!!!!!
#define PORT 14666


#define BUF_SIZE 40 

 
#define BUFFER_SIZE 1024
#define NAME_SIZE 1000 //ile to ma byc?

#define RETRANSMIT_LIMIT 10 

#define DEBUG 1 

static const char bye_string[] = "BYE";

//uzgodnic typy!
int port_num = PORT;
char server_name[NAME_SIZE];
int retransfer_lim = RETRANSMIT_LIMIT;
int sock_udp;
struct sockaddr_in my_address;
socklen_t rcva_len = (socklen_t) sizeof(my_address);
int nr = 0;

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
    printf("Czytanie z stdin\n");
    //my_address.sin_family = AF_INET; 
    //my_address.sin_addr.s_addr = htonl(INADDR_ANY); //to trzeba wziac z argumentow jakos!!!!!
    //my_address.sin_port = htons((uint16_t) port_num);

    unsigned char buf[BUF_SIZE+1];
    memset(buf, 0, sizeof(buf));
    socklen_t rcva_len = (socklen_t) sizeof(my_address);
    ssize_t snd_len;
    //int snd_len;
    int flags = 0;

    int r = read(descriptor, buf, BUF_SIZE);
    if(r < 0) syserr("w evencie: read (from stdin)");
    if(r == 0) {
        fprintf(stderr, "stdin closed. Exiting event loop.\n");
        if(event_base_loopbreak(base) == -1) 
            syserr("event_base_loopbreak");
        return;
    }

    printf("Wysylanie po UDP komunikatu: %s o sizeof: %zu\n",buf, sizeof(buf));
    snd_len = sendto(sock_udp, buf, strlen(buf), flags,
            (struct sockaddr *) &my_address, rcva_len);    

    //potem poprawic na wysylanie w petli , a tak naprawde tylko partiami wielkosci WIN
    if (snd_len < 0) { // != sizeof(buf)
            syserr("partial / failed sendto"); 
    }    
    else printf("wyslano\n");       

    //if(bufferevent_write(bev, buf, r) == -1) syserr("bufferevent_write");
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


    if (DEBUG) {
        //send_CLIENT_datagram(15); //<- dziala, trzeba wydobyć client id tylko 
        char bzdury[] = "nikt tego nie zda"; 
        //send_UPLOAD_datagram(bzdury, nr);
        //send_RETRANSMIT_datagram(nr);
        send_KEEEPALIVE_datagram();
    }    
  }

}

void an_event_cb(struct bufferevent *bev, short what, void *arg)
{
  printf("An event cb\n");
  if(what & BEV_EVENT_CONNECTED) {
    fprintf(stderr, "Connection made.\n");
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


int create_UDP_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        syserr("socket");
    }
    my_address.sin_family = AF_INET; 
    //my_address.sin_addr.s_addr = htonl(INADDR_ANY); //to trzeba wziac z argumentow jakos!!!!!
    my_address.sin_port = htons((uint16_t) port_num);
    printf("Stworzyl gniazdo UDP\n");
    return sock;
}  

void send_CLIENT_datagram(uint32_t id) {       

    char clientid[11]; /* 11 bytes: 10 for the digits, 1 for the null character */
    snprintf(clientid, sizeof(clientid), "%" PRIu32, id); /* Method 2 */

    char* type= "CLIENT";
    char* datagram = malloc(strlen(type) + strlen(clientid) + 2);

    sprintf(datagram, "%s %s\n", type, clientid); 
    send_datagram(datagram);  
}

void send_UPLOAD_datagram(char *data, int no) {
    int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_size = (int) ((ceil(log10(num))+1)*sizeof(char));
    
    char str[str_size];
    sprintf(str, "%d", no);

    char* type= "UPLOAD";
    char* datagram = malloc(strlen(type) + strlen(str) + 2 + strlen(data));

    sprintf(datagram, "%s %s\n%s", type, str, data);
    send_datagram(datagram);
}

void send_RETRANSMIT_datagram(int no) {
    int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_size = (int) ((ceil(log10(num))+1)*sizeof(char));

    char str[str_size];
    sprintf(str, "%d", no);

    char* type= "RETRANSMIT";
    char* datagram = malloc(strlen(type) + strlen(str) + 2);

    sprintf(datagram, "%s %s\n", type, str);
    send_datagram(datagram);
}

void send_KEEEPALIVE_datagram() {
    char *datagram = "KEEPALIVE\n";
    send_datagram(datagram);
}

void send_datagram(char *datagram) {
    ssize_t snd_len;
    int flags = 0;
    snd_len = sendto(sock_udp, datagram, strlen(datagram), flags,
            (struct sockaddr *) &my_address, rcva_len);    
    
    if (snd_len != strlen(datagram)) {
            syserr("partial / failed sendto");
    }        
}

int main (int argc, char *argv[]) {
    //int rc;
    //int sock;

    //struct addrinfo addr_hints_poll, *addr_result;
    //char line[BUFFER_SIZE];

    if (DEBUG && argc == 1) {
        printf("Client run with parameters: -s [server_name](obligatory) -p [port_num] -X [retransfer_limit]\n");
    }


    get_parameters(argc, argv);

    base = event_base_new();
    if(!base) syserr("event_base_new");
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

    //read_CLIENT_datagram(&clientid);  
    sock_udp = create_UDP_socket();  

    struct event *stdin_event =
        event_new(base, 0, EV_READ|EV_PERSIST, stdin_cb, NULL); // 0 - standardwowe wejscie
    if(!stdin_event) syserr("event_new");
    if(event_add(stdin_event,NULL) == -1) syserr("event_add");
  

 
/*
    pid_t pid;

    switch (pid = fork()) {
        case -1:
            syserr("fork()");
        case 0: 
            if (!DEBUG) {
                printf("[PID: %d] Jestem procesem potomnym, to ja zajme sie przesylem po UDP\n",getpid());
            }
            //cos tu bedzie
            if (!DEBUG) { 
                printf("[PID: %d] Jestem procesem potomnym, i zaraz sie skoncze\n",getpid());
            }
            exit(0);

        default:
            break;        
    }

*/

    if (DEBUG) {
        printf("[PID: %d] Jestem procesem macierzystym, to ja zajme sie obsluga TCP\n",getpid());
    }

    printf("Entering dispatch loop.\n");
    if(event_base_dispatch(base) == -1) syserr("event_base_dispatch");
    printf("Dispatch loop finished.\n");

    bufferevent_free(bev);
    event_base_free(base);


    return 0;
}


