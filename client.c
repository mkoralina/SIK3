/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/


#include <event2/event.h>
#include <event2/buffer.h>
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
#include <signal.h> 
#include <netinet/tcp.h> //nagle
#include "inttypes.h" 


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

#ifndef max
#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#ifndef min
#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )
#endif 


#define PORT 14666
#define BUF_SIZE 200
#define RCV_SIZE 64000 
#define NAME_SIZE 100 //TODO: ile to ma byc?
#define RETRANSMIT_LIMIT 10 
#define DATAGRAM_SIZE 10000 

#define TRUE 1
#define FALSE 0

#define DEBUG 1

int port_num = PORT;
char server_name[NAME_SIZE];
int retransfer_lim = RETRANSMIT_LIMIT;
int sock_udp;
evutil_socket_t sock_tcp;
struct sockaddr_in6 my_address;
socklen_t rcva_len = (socklen_t) sizeof(my_address);
int last_sent = -1; /* nr z polecen */
int ack = -1;
int win = 0;
int clientid = -1; /* -1 to kleint niezidentyfikowany */
int nr_expected = -1;
int nr_max_seen = -1;
int connection_lost = 0;
char * last_datagram;
int DATAs_since_last_datagram = 0;
int finish = 0;
time_t last_reboot;
time_t last_to_one_reboot;



pthread_t keepalive_thread = 0;
pthread_t event_thread = 0;
pthread_t main_thread = 0;
pthread_t stdin_thread = 0;


struct event_base *base;
struct event *stdin_event;

struct addrinfo addr_hints = {
    .ai_flags = AI_V4MAPPED,  // ew.  AI_V4MAPPED | AI_ALL  
    .ai_family = AF_UNSPEC,
    .ai_socktype = SOCK_STREAM,
    .ai_protocol = 0,
    .ai_addrlen = 0,
    .ai_addr = NULL,
    .ai_canonname = NULL,
    .ai_next = NULL
};

void get_parameters(int argc, char *argv[]) {
    
    int server_name_set = 0;
    int j;
    for (j = 1; j < argc; j++)  
    {
        if (strcmp(argv[j], "-p") == 0)  
        {
            port_num = atoi(argv[j+1]); // to jest w ogóle opcjonalne -> PSRAWDZ!!!
        }
        else if (strcmp(argv[j], "-s") == 0)
        {    
            strcpy(server_name, argv[j+1]);
            server_name_set = 1;
            addr_hints.ai_family = AF_INET6;
        }
        else if (strcmp(argv[j], "-X") == 0)
        {
            retransfer_lim = atoi(argv[j+1]); // TODO: wywali blad, jesli X nie jest intem?
        }
    }
    
    if (!server_name_set) {
        syserr("Client usage: -s [server_name](obligatory) -p [port_num] -X [retransfer_limit]\n");
    }

    if (DEBUG) {            
        printf("port_num: %d\n", port_num);
        printf("server_name: %s\n", server_name);
        printf("retransfer_lim: %d\n", retransfer_lim); 
    }
}

void read_from_stdin(evutil_socket_t descriptor, short ev, void *arg) {
    //printf("Czytanie z stdin\n");
    char buf[BUF_SIZE+1];
    memset(buf, 0, sizeof(buf));
    
    if (ack > last_sent && win > 0) { 

        //TODO: blokowanie wejscia - usuniecie zupelnie tego eventu
        //TODO: blokowanie wejscia w ronzyhc sytuacjach

        //fprintf(stderr,"(ack, last, win) = (%d, %d, %d)\n",ack, last_sent,win);           
        if (DEBUG) printf("read\n");
        int to_read = min(win, BUF_SIZE);
        

        int r = read(0, buf, to_read);
        //fprintf(stderr, "Przeczytalem %d bajtow\n",r);
        if(r < 0) {
            fprintf(stderr, "w evencie: read (from stdin)\n");
            syserr("DEBUG: blad");
            finish = TRUE;
            stdin_thread = 0;
            void* ret = NULL;
            pthread_exit(&ret);
        }    
        if(r == 0) {
            fprintf(stderr,"stdin closed. Exiting event loop.\n");
            syserr("DEBUG: blad");
            finish = TRUE;
            stdin_thread = 0;
            void* ret = NULL;
            pthread_exit(&ret);
                
        }
        if (r > 0) {
            //write(1,buf,r);
            last_sent++;
            send_UPLOAD_datagram(buf, last_sent, r); 
            //write(1, buf, r);                            
        }
        memset(buf, 0, sizeof(buf));
    } 
    else {
        event_del(stdin_event);   
    }
      
}

void set_stdin_event() {
    int stdin = 0;
    stdin_event =
        event_new(base, stdin, EV_READ|EV_PERSIST, read_from_stdin, NULL); 
    if(!stdin_event) syserr("event_new");
    if(event_add(stdin_event,NULL) == -1) syserr("event_add"); 
}

/* Funkcja czyta z TCP, wyrzuca raporty na stderr */
void read_from_tcp(evutil_socket_t descriptor, short ev, void *arg)
{  
    // TODO: kontrola, czy jest caly czas polaczenie, czyli pewnie jakiś timeout trzeba ustawic!

    if (clientid < 0) {
        read_CLIENT_datagram();
    }

    //fprintf(stderr, "RAPORT RAPORT\n");

    char buf[BUF_SIZE+1];
    int r = read(sock_tcp, buf, BUF_SIZE);
    //fprintf(stderr, "PRZECZYTALEN RAPORT dlugosci %d\n",r );
    if(r == -1) syserr("fail on read_from_tcp"); //TODO: reboot
    buf[r] = 0; //znak konca na koniec
    fprintf(stderr, "%s", buf); //wypisuje raport   
 
    return;
}

void set_tcp_event() {
    struct event *tcp_event =
        event_new(base, sock_tcp, EV_READ|EV_PERSIST, read_from_tcp, NULL); 
    if(!tcp_event) syserr("event_new");
    if(event_add(tcp_event,NULL) == -1) syserr("event_add");    
}

void read_from_udp(evutil_socket_t descriptor, short ev, void *arg) {
    // TODO: cala komunikacja jako odbiorca po UDP (powinien wystarczyc jeden watek)
    // czytanie w petli z UDP (jaka dlugosc? czy na pewno dobrze wczyta? czy moze nie zdazyc wczytac i np. beda juz dw w srodku? na pewno! co wtedy ?)
    // matchowanie komunikatow
    // obsluga komunikatow

    fprintf(stderr, "PYK PYK\n");

    ssize_t len;
    char datagram[RCV_SIZE+1];
    
    int flags = 0; 
    struct sockaddr_in6 server_udp;
    socklen_t rcva_len = (ssize_t) sizeof(server_udp);
            
        
    memset(datagram, 0, sizeof(datagram)); 
    len = recvfrom(sock_udp, datagram, RCV_SIZE, flags,
            (struct sockaddr *) &server_udp, &rcva_len); 
   // fprintf(stderr, "read_from_udp len = %d\n",len);
    //write(1,datagram, len);
    //fprintf(stderr, "datagram: %s\n",datagram );


    if (len < 0) {
        //klopotliwe polaczenie z serwerem
        perror("error on datagram from server socket/ timeout reached");
        if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
        //reboot(); //TODO
    }    
    else {
        if (DEBUG) {
            //(void) printf("read through UDP from [adres:%d]: %zd bytes: %.*s\n", ntohs(server_udp.sin6_port), len,
            //    (int) len, datagram); //*s oznacza odczytaj z buffer tyle bajtów ile jest podanych w (int) len (do oczytywania stringow, ktore nie sa zakonczona znakiem konca 0
            //printf("Otrzymano: %s\n", datagram);
        }    
        match_and_execute(datagram, len);    
    }

    
}


void set_udp_event() {
    struct event *udp_event =
        event_new(base, sock_udp, EV_READ|EV_PERSIST, read_from_udp, NULL); 
    if(!udp_event) syserr("event_new");
    if(event_add(udp_event,NULL) == -1) syserr("event_add");    
}

void send_KEEPALIVE_datagram(evutil_socket_t descriptor, short ev, void *arg) {
    //TODO: warunek stopu
    char *datagram = "KEEPALIVE\n";
    send_datagram(datagram, strlen(datagram));
    return 0;    
}

void set_keepalive_event() {
    struct timeval wait_time = { 0, 100000 }; // { s, micro_s}
    struct event *keepalive_event =
        event_new(base, sock_udp, EV_PERSIST, send_KEEPALIVE_datagram, NULL); 
    if(!keepalive_event) syserr("event_new");
    if(event_add(keepalive_event,&wait_time) == -1) syserr("event_add");  
}

void set_events() {
    set_stdin_event();
    set_tcp_event(); // po przeczytaniu CLIENT datagram set_keepalive_event();
    set_udp_event();
}



void reboot() {
    if (DEBUG) printf("REBOOT\n");
    finish = TRUE; 

    //wait 500ms
    struct timespec tim, tim2;
    tim.tv_sec = 0;
    tim.tv_nsec = 500 * 1000000; 
    nanosleep(&tim, &tim2); 
    //reopen
    set_events(); //TODO: o ile blad nie wystapil gdzies wczesniej przy tworzeniu gniazda albo bazy
}


void send_datagram(char *datagram, int len) {
    //fprintf(stderr, "datagram: %s\n",datagram );

    ssize_t snd_len;
    int flags = 0;

    snd_len = sendto(sock_udp, datagram, len, flags,
            (struct sockaddr *) &my_address, rcva_len);              
    
    if (snd_len != len) {    
        perror("partial / failed sendto");
        free(datagram);
        //TODO: na pewno to?
        if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
    }        
}

void send_CLIENT_datagram(uint32_t id) {       

    char clientid[11]; /* 11 bytes: 10 for the digits, 1 for the null character */
    snprintf(clientid, sizeof(clientid), "%" PRIu32, id); 

    char* type= "CLIENT";
    char* datagram = malloc(strlen(type) + strlen(clientid) + 3);

    sprintf(datagram, "%s %s\n", type, clientid); 
    send_datagram(datagram, strlen(datagram));  
    free(datagram);
}


void send_UPLOAD_datagram(void *data, int no, int data_size) {    
    //printf("WcHOZE\n");
    //fprintf(stderr, "data UPLOAD: %*s\n",data_size,data);

    int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str[str_size];
    sprintf(str, "%d", no);

    char* type= "UPLOAD";


    char* datagram = malloc(strlen(type) + strlen(str) + 2 + data_size); // na spacje, \n i \0

    int len = strlen(type) + strlen(str) + 2 + data_size; //dlugosc calego datagramu 
   
    //fprintf(stderr, "data: %s\n",data );

    char * header = malloc(strlen(type) + strlen(str) + 3);
    sprintf(header, "%s %s\n", type, str);
    memcpy(datagram, header, strlen(header));
    memcpy(datagram + strlen(header), data, data_size);

    //fprintf(stderr, "datagram %s\n",datagram );
    send_datagram(datagram, len); //TODO 

//write(1,data,data_size); //brzmi dokładnie tak jak w serwerze na razie
    
    memset(last_datagram, 0, len);
    memcpy(last_datagram, data, len);

//    memset(last_datagram, 0, strlen(last_datagram));
//    memcpy(last_datagram, data, strlen(data));
    //write(1, data, data_size); dziala tutaj, ale juz duzo wiecej trzaskow, dzwiek wolniej, trzaski duzo szybciej
    free(header);
    free(datagram);    
}

void send_RETRANSMIT_datagram(int no) {
    int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_size = (int) ((floor(log10(num))+1)*sizeof(char));

    char str[str_size];
    sprintf(str, "%d", no);

    char* type= "RETRANSMIT";
    char* datagram = malloc(strlen(type) + strlen(str) + 2);

    sprintf(datagram, "%s %s\n", type, str);
    send_datagram(datagram, strlen(datagram));
    free(datagram);
}




/* Obsługa sygnału kończenia */
static void catch_int (int sig) {
    /* zwalniam zasoby */
    //ogarnic te watki i zwalnianie pamieci po mallocu
    finish = TRUE;
    if (DEBUG) {
        printf("Exit() due to Ctrl+C\n");
    }
    //nie wiem, czy nie musi czasem sprawdzac, czy juz cos nie zamknelo
    if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
    exit(EXIT_SUCCESS);
}





void read_CLIENT_datagram() {
    if (DEBUG) {
        printf("read_CLIENT_datagram\n");
    }
    char buf[BUF_SIZE+1];

    int r = read(sock_tcp, buf, BUF_SIZE);
    if(r == -1) syserr("bufferevent_read");

    if (sscanf(buf, "CLIENT %d\n", &clientid) == 1) {
        if (DEBUG) {
            printf("Otrzymano: %s",buf);
            printf("Zidentyfikowano: clientid = %d\n",clientid);
        }
    }
    
    send_CLIENT_datagram(clientid);  
    set_keepalive_event();
}




int create_UDP_socket() {
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        syserr("socket");
    }

    //timeout do wykrywania klopotow z polaczeniem
    //"Jeśli klient przez sekundę nie otrzyma żadnych danych 
    //od serwera (choćby pustego datagramu DATA), uznaje połączenie za kłopotliwe."
    struct timeval tv;
    tv.tv_sec = 30; //TODO: zmien na 1s
    tv.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("Error");
        finish = TRUE;
    }

    my_address.sin6_family = AF_INET6; 
    my_address.sin6_addr = in6addr_any; 
    my_address.sin6_port = htons((uint16_t) port_num);
    if (DEBUG) printf("Stworzyl gniazdo UDP\n");
    return sock;
}  

void match_and_execute(char *datagram, int len) {
    int nr;
    //char data[BUF_SIZE+1];
    //memset(data, 0, BUF_SIZE);
    //zakladam, ze komunikaty sa poprawne z protokolem, wiec 3. pierwsze argumenty musza byc intami, 4. moze byc pusty
    if (sscanf(datagram, "DATA %d %d %d", &nr, &ack, &win) == 3) {
        DATAs_since_last_datagram++;
        fprintf(stderr, "DATAs = %d\n",DATAs_since_last_datagram );
        if (ack == last_sent+1 && win > 0) {
            if(event_add(stdin_event,NULL) == -1) syserr("event_add");
        }
        // TODO: malloc, free, blad na retransmisji
        /* *** Error in `./client': free(): invalid next size (fast): 0x0000000000cbf0f0 ***
           *** Error in `./client': malloc(): memory corruption: 0x0000000000cbf110 ***
        */
        if (DATAs_since_last_datagram > 1 && last_sent >= 0 && last_sent == ack) {
            if (DEBUG) printf("RETRANSMISJA KLIENT -> SERWER\n");
            //send_UPLOAD_datagram(last_datagram, last_sent); //TODO: odkomentowac i dopisac rozmiar do parametrow
            DATAs_since_last_datagram = 0;
        }
        fprintf(stderr, "Zmatchowano do DATA, nr = %d, ack = %d, win = %d\n", nr, ack, win);
        

        

        char * ptr = memchr(datagram, '\n', len);
        int header_len = ptr - datagram + 1;
        int data_len = len - header_len; 
        if (DEBUG) printf("header_size = %d\n", header_len);
        //fprintf(stderr, "data_len = %d\n",data_len);
        //fprintf(stderr, "data z DATA: %s\n", datagram+header_len);
        //gdy jest to nasz pierwszy DATA datagram 
        //fprintf(stderr, "data: %s\n", datagram+header_len);

        if (nr_expected == -1) {
            nr_expected = nr + 1;
            write(1,datagram+header_len,data_len); //TODO!
            //printf("%s\n",data); // TODO! odkomentowac
        }
        else if (nr > nr_expected) {
            if (nr_expected >= nr - retransfer_lim && nr_expected > nr_max_seen) {                
                send_RETRANSMIT_datagram(nr_expected);
            }
            else {
                nr_expected = nr + 1;
                write(1,datagram+header_len,data_len);
                //przyjmuje dane -> stdout
               // printf("%s",data); //TODO: odkom
            }
        }
        else if (nr == nr_expected) {
            write(1,datagram+header_len,data_len);
            //przyjmij dane
           // printf("%s\n",data);//TODO: odkom
        }
        nr_max_seen = max(nr, nr_max_seen);

    }
    else if (sscanf(datagram, "ACK %d %d", &ack, &win) == 2) {
        if (ack == last_sent + 1) 
            DATAs_since_last_datagram = 0;
        if (ack == last_sent+1 && win > 0) {
            if(event_add(stdin_event,NULL) == -1) syserr("event_add");
        }

        if (DEBUG) printf("Zmatchowano do ACK, ack = %d, win = %d\n", ack, win);        
    }
    else 
        //syserr("Niewlasciwy format datagramu");
        if (DEBUG) printf("Niewlasciwy format datagramu\n");
} 

char * addr_to_str(struct sockaddr_in6 *addr) {
    char * str = malloc(sizeof(char) * INET6_ADDRSTRLEN);
    inet_ntop(AF_INET6, &(addr->sin6_addr), str, INET6_ADDRSTRLEN);
    //if (DEBUG) printf("adres klienta: %s\n", str);
    return str;
}  



void set_base() {
    base = event_base_new();
    if (!base) {
        fprintf(stderr, "ERROR: event_base_new\n");
        reboot();
    }    
}


evutil_socket_t create_TCP_socket() {
    evutil_socket_t sock;    
    struct addrinfo *addr;

    int port_size = (int) ((floor(log10(port_num))+1)*sizeof(char));    
    char str_port[port_size];
    sprintf(str_port, "%d", port_num);

    if(getaddrinfo(server_name, str_port, &addr_hints, &addr)) syserr("getaddrinfo");

    sock = socket(addr->ai_family, addr->ai_socktype, 0);
    if (sock < 0) {
        syserr("socket tcp");
        fprintf(stderr, "socket tcp\n");
        //TODO: reboot
    }

    if (connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
        fprintf(stderr, "connect tcp\n");
        //TODO: reboot
    }

    //zablokowanie Nagle'a
    int flag = 1;
    int result = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)); 
    if (result < 0) {
        fprintf(stderr, "setsockopt tcp\n");
        //TODO: reboot
    }
    return sock;
}        






  
       




//TODO: do poprawy
/*
void main_loop() {
    if (DEBUG) printf("MAIN LOOP\n");
    finish = FALSE;

    last_datagram = malloc(DATAGRAM_SIZE);
    //sock_udp = create_UDP_socket(); 
    set_event_TCP_stdin();    
    create_thread(&event_loop); //przejmie czytanie  z TCP    
    create_thread(&read_from_stdin); 
    read_from_UDP();
    free(last_datagram); // TODO: do tego nie powinien dojsc, wrzucic to do zwalniania zasobow
}*/

int main (int argc, char *argv[]) {

    if (DEBUG && argc == 1) {
        printf("Client run with parameters: -s [server_name](obligatory) -p [port_num] -X [retransfer_limit]\n");
    }    

    /* Ctrl-C konczy porogram */
    if (signal(SIGINT, catch_int) == SIG_ERR) {
        syserr("Unable to change signal handler\n");
    }

    get_parameters(argc, argv);
    finish = FALSE;
    last_datagram = malloc(DATAGRAM_SIZE);

    
    sock_tcp = create_TCP_socket();
    sock_udp = create_UDP_socket(); 
    set_base();
    set_events();

    if (DEBUG) printf("Entering dispatch loop.\n");
    if(event_base_dispatch(base) == -1) syserr("event_base_dispatch");
    if (DEBUG) printf("Dispatch loop finished.\n");

    event_base_free(base);

    free(last_datagram); // TODO: do tego nie powinien dojsc, wrzucic to do zwalniania zasobow    
    return 0;
}

/* 

TODO:

5) zamiast czekać pół minuty mżna trzymać czas od przedostatniego reboota (zapisywać zawsze dwa ostatnie)
 i czkeać max( 500ms - (obecny_czas - czas_przedostatniego_reboota), 0) w milisekundach


*/