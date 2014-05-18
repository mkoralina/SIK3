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
#include <pthread.h> 


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
#include <time.h>
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
int last_sent = 0; /* nr z polecen */
int ack = -1;
int win = 0;
int clientid = -1; /* -1 to kleint niezidentyfikowany */

struct event_base *base;
struct bufferevent *bev;

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

void stdin_cb(evutil_socket_t descriptor, short ev, void *arg)
{
    printf("Czytanie z stdin\n");
    unsigned char buf[BUF_SIZE+1];
    memset(buf, 0, sizeof(buf));
    socklen_t rcva_len = (socklen_t) sizeof(my_address);
    ssize_t snd_len;
    int flags = 0;

    // TODO: czytanie wielkosci okna podanego przez serwer: win
    // doczytaj jeszcze jak to jest z tymi numerami
    // if (ack > last_sent && win > 0) { czytaj dane i wysylaj datagram UPLOAD}
    int r = read(descriptor, buf, BUF_SIZE);
    if(r < 0) syserr("w evencie: read (from stdin)");
    if(r == 0) {
        fprintf(stderr, "stdin closed. Exiting event loop.\n");
        if(event_base_loopbreak(base) == -1) 
            syserr("event_base_loopbreak");
        return;
    }

    last_sent++;
    send_UPLOAD_datagram(buf, last_sent);      
}

/* Funkcja czyta z TCP, wyrzuca raporty na stdout */
void a_read_cb(struct bufferevent *bev, void *arg)
{  
    // TODO: kontrola, czy jest caly czas polaczenie, czyli pewnie jakiś timeout trzeba ustawic!

    if (clientid < 0) {
        read_CLIENT_datagram(bev, arg);
    }

    char buf[BUF_SIZE+1];
    while(evbuffer_get_length(bufferevent_get_input(bev))) {

        int r = bufferevent_read(bev, buf, BUF_SIZE);
        if(r == -1) syserr("bufferevent_read");
        buf[r] = 0;
        printf("%s\n", buf);

        // TODO: do usuniecia to!
        if (DEBUG) {
            //send_CLIENT_datagram(15); //<- dziala, trzeba wydobyć client id tylko 
            char bzdury[] = "nikt tego nie zda"; 
            send_UPLOAD_datagram(bzdury, last_sent);
            send_RETRANSMIT_datagram(last_sent);
            send_KEEEPALIVE_datagram();
        }    
    }
}

void an_event_cb(struct bufferevent *bev, short what, void *arg) {
    printf("An event cb\n");
    if(what & BEV_EVENT_CONNECTED) {
        fprintf(stderr, "Connection made.\n");
        return;
    }
    if(what & BEV_EVENT_EOF) {
        fprintf(stderr, "EOF encountered.\n");
    } 
    else if(what & BEV_EVENT_ERROR) {
        fprintf(stderr, "Unrecoverable error.\n");
    } 
    else if(what & BEV_EVENT_TIMEOUT) {
        fprintf(stderr, "A timeout occured.\n");
    }
    if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
}


void read_CLIENT_datagram(struct bufferevent *bev, void *arg) {
    if (DEBUG) {
        printf("read_CLIENT_datagram\n");
    }
    char buf[BUF_SIZE+1];
    while(evbuffer_get_length(bufferevent_get_input(bev))) {
        int r = bufferevent_read(bev, buf, BUF_SIZE);
        if(r == -1) syserr("bufferevent_read");
        buf[r] = 0;
        if (sscanf(buf, "CLIENT %d\n", &clientid) == 1) {
            if (DEBUG) {
                printf("Otrzymano: %s",buf);
                printf("Zidentyfikowano: clientid = %d\n",clientid);
            }
        }
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
    for (;;) {
        struct timespec tim, tim2;
        tim.tv_sec = 0; //0s
        tim.tv_nsec = 100000000; //0.1s
        nanosleep(&tim, &tim2);    
        char *datagram = "KEEPALIVE\n";
        send_datagram(datagram);
    }    
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

void event_loop() {

    printf("Entering dispatch loop.\n");
    if(event_base_dispatch(base) == -1) syserr("event_base_dispatch");
    printf("Dispatch loop finished.\n");

    bufferevent_free(bev);
    event_base_free(base);   
}

void create_event_thread() {
    pthread_t r; /*wynik*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);-
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED); 

    pthread_create(&r,&attr,event_loop,NULL);    
}

void create_keepalive_thread() {
    pthread_t r; /*wynik*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);-
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED); 

    pthread_create(&r,&attr,send_KEEEPALIVE_datagram,NULL);    
}


void match_and_execute(char *datagram) {
    int nr;
    char data[BUF_SIZE+1];
    if (sscanf(datagram, "DATA %d %d %d %[^\n]", &nr, &ack, &win, data) >= 4) {
        printf("Zmatchowano do DATA, nr = %d, ack = %d, win = %d, dane = %s\n", nr, ack, win, data);  
        // TODO: i co? jak sie teraz tu zmieni ACK, to tam na gorze sie wlaczy petla do czytania z stdin?
        //obsluz DATA
        //wpisuejsz [dane] do kolejki zwiazanej z klientem
        //if client_info[clientid].ack == nr
        //int ack = client_info[clientid].ack
        //send_ACK_datagram()
    }
    else if (sscanf(datagram, "ACK %d %d", &ack, &win) == 2) {
        printf("Zmatchowano do ACK, ack = %d, win = %d\n", ack, win);
    }
    else 
        //syserr("Niewlasciwy format datagramu");
        printf("Niewlasciwy format datagramu\n");
}   


int main (int argc, char *argv[]) {

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

    // TODO: do poprawki cala ta komunikacja

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
  
    sock_udp = create_UDP_socket();  

    struct event *stdin_event =
        event_new(base, 0, EV_READ|EV_PERSIST, stdin_cb, NULL); // 0 - standardwowe wejscie
    if(!stdin_event) syserr("event_new");
    if(event_add(stdin_event,NULL) == -1) syserr("event_add");

    create_event_thread(); //przejmie czytanie z stdin oraz z TCP
    create_keepalive_thread();

    // TODO: cala komunikacja jako odbiorca po UDP (powinien wystarczyc jeden watek)
    // czytanie w petli z UDP (jaka dlugosc? czy na pewno dobrze wczyta? czy moze nie zdazyc wczytac i np. beda juz dw w srodku? na pewno! co wtedy ?)
    // matchowanie komunikatow
    // obsluga komunikatow
    ssize_t len;
    char datagram[BUF_SIZE+1];
    memset(datagram, 0, sizeof(datagram)); 
    int flags = 0; 
    struct sockaddr_in server_udp;
    socklen_t rcva_len = (ssize_t) sizeof(server_udp);
            
    for (;;) {
        do {         

            len = recvfrom(sock_udp, datagram, sizeof(datagram), flags,
                    (struct sockaddr *) &server_udp, &rcva_len); 

            if (len < 0)
                  syserr("error on datagram from server socket");
            else {
                if (DEBUG) {
                    (void) printf("read through UDP from [%s:%d]: %zd bytes: %.*s\n", inet_ntoa(server_udp.sin_addr), ntohs(server_udp.sin_port), len,
                        (int) len, datagram); //*s oznacza odczytaj z buffer tyle bajtów ile jest podanych w (int) len (do oczytywania stringow, ktore nie sa zakonczona znakiem konca 0
                    printf("Otrzymano: %s\n", datagram);
                }    
                match_and_execute(datagram);    
            }
        } while (len > 0); 
    }

    return 0;
}


