/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/


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
#include <signal.h> 
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
#define NAME_SIZE 100 //TODO: ile to ma byc?
#define RETRANSMIT_LIMIT 10 
#define DATAGRAM_SIZE 10000 

#define TRUE 1
#define FALSE 0

#define DEBUG 0 

int port_num = PORT;
char server_name[NAME_SIZE];
int retransfer_lim = RETRANSMIT_LIMIT;
int sock_udp;
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
struct bufferevent *bev;

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

void wait_for(pthread_t * thread) {
    if (*thread != 0) {
        void* ret = NULL;
        if ((pthread_join(*thread, &ret)) != 0) {
            syserr("pthread_join in wait_for");
        }
        else {
            if (DEBUG) printf("thread joined\n");
        }
    }
}

void reboot() {
    if (DEBUG) printf("REBOOT\n");
    finish = TRUE; 
    wait_for(&keepalive_thread);
    wait_for(&event_thread);

    //wait 500ms
    struct timespec tim, tim2;
    tim.tv_sec = 0;
    tim.tv_nsec = 500 * 1000000; 
    nanosleep(&tim, &tim2); 
    //reopen
    main_loop();
}


void send_datagram(char *datagram, int len) {
    fprintf(stderr, "datagram: %s\n",datagram );

    ssize_t snd_len;
    int flags = 0;
//    snd_len = sendto(sock_udp, datagram, strlen(datagram), flags,
//            (struct sockaddr *) &my_address, rcva_len); 

    //DEBUG:
   /* int nr; char data[BUF_SIZE+1] = {0};
    if (sscanf(datagram, "UPLOAD %d %[^\n]", &nr, data) >= 1) {
        write(1,data,150);
    }*/


    snd_len = sendto(sock_udp, datagram, len, flags,
            (struct sockaddr *) &my_address, rcva_len);              
    
    if (snd_len != len) {    
//    if (snd_len != strlen(datagram)) {
        perror("partial / failed sendto");
        free(datagram);
        
        if (pthread_self() == main_thread) 
            reboot();
        else if (pthread_self() == keepalive_thread) {
            finish = TRUE;
            keepalive_thread = 0;
            void* ret = NULL;
            pthread_exit(&ret);
        }
        else if (pthread_self() == event_thread)
        {
            finish = TRUE;
            wait_for(&keepalive_thread);
            event_thread = 0;
            void* ret = NULL;
            pthread_exit(&ret);
        } 
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
    fprintf(stderr, "data: %*s\n",data_size,data);
    


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

void * send_KEEEPALIVE_datagram(void * arg) {
    keepalive_thread = pthread_self();
    while (!finish) {
        struct timespec tim, tim2;
        tim.tv_sec = 0; //0s
        tim.tv_nsec = 100000000; //0.1s
        nanosleep(&tim, &tim2);    
        char *datagram = "KEEPALIVE\n";
        send_datagram(datagram, strlen(datagram));
    }
    keepalive_thread = 0;
    return 0;    
}


/* Obsługa sygnału kończenia */
static void catch_int (int sig) {
    /* zwalniam zasoby */
    //ogarnic te watki i zwalnianie pamieci po mallocu
    finish = TRUE;
    if (DEBUG) {
        printf("Exit() due to Ctrl+C\n");
    }
    /* Assuming 2.6 posix threads, and the OS sending SIGTERM or SIGHUP,
     the signal is sent to process, which is received by and handled by root thread*/
    wait_for(&keepalive_thread);
    wait_for(&event_thread);
    wait_for(&stdin_thread);
    exit(EXIT_SUCCESS);
}


void read_from_stdin() {
    //printf("Czytanie z stdin\n");
    stdin_thread = pthread_self();
    char buf[BUF_SIZE+1];
    memset(buf, 0, sizeof(buf));
    
    while (!finish) { //TODO: to jest troche slabe tak swoją drogą, baaardzo aktywne czekanie
        fprintf(stderr,"sprawdza czy moze wejsc do czytanie w stdin\n");
        if (ack > last_sent && win > 0) { 
            fprintf(stderr,"(ack, last, win) = (%d, %d, %d)\n",ack, last_sent,win);           
            if (DEBUG) printf("read\n");
            int to_read = min(win, BUF_SIZE);
            

            int r = read(0, buf, to_read);
            fprintf(stderr, "Przeczytalem %d bajtow\n",r);
            if(r < 0) {
                perror("w evencie: read (from stdin)");
                finish = TRUE;
                stdin_thread = 0;
                void* ret = NULL;
                pthread_exit(&ret);
            }    
            if(r == 0) {
                perror("stdin closed. Exiting event loop.");
                finish = TRUE;
                stdin_thread = 0;
                void* ret = NULL;
                pthread_exit(&ret);
                    
            }
            if (r > 0) {
                //sprawdzam, czy dobrze dzwiek wczytuje i wypluwam go od razu na stdin
                //fprintf(stderr, "DATA: %*s\n",r,buf);
                //write(1,buf,r);
                
                //fprintf(stderr, "r = %d",r); // 150
                //fprintf(stderr, "strlen(buf) = %d",strlen); // 4199616
                //DZIALA!!

                //fprintf(stderr, "buf: %s\n",buf );
                last_sent++;
               // printf("send_UPLOAD_datagram(%s, dl: %d,nr: %d)\n", buf, strlen(buf),last_sent);
                //printf("send_UPLOAD_datagram( dl: %zu,nr: %d)\n", strlen(buf),last_sent);
                //if (DEBUG) printf("send_UPLOAD_datagram(buf, last_sent, r): (buf, %d, %d) + ack = %d\n",last_sent,r,ack );
                send_UPLOAD_datagram(buf, last_sent, r); 
                //fprintf(stderr, "r = %d\n",r); 
                //fprintf(stderr, "strlen = %d\n",strlen(buf));
                //write(1, buf, r);
                
                             
            }
            memset(buf, 0, sizeof(buf));
        } 
    } 
    fprintf(stderr, "FINISH = TRUE w stdin\n");
    stdin_thread = 0;
    void* ret = NULL;
    pthread_exit(&ret);       
}

void create_thread(void * (*func)(void *)) {
    pthread_t r; /*wynik*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    pthread_create(&r,&attr,*func,NULL);        
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
    send_CLIENT_datagram(clientid);  
    create_thread(&send_KEEEPALIVE_datagram); 
}

/* Funkcja czyta z TCP, wyrzuca raporty na stder */
void a_read_cb(struct bufferevent *bev, void *arg)
{  
    // TODO: kontrola, czy jest caly czas polaczenie, czyli pewnie jakiś timeout trzeba ustawic!

    if (clientid < 0) {
        read_CLIENT_datagram(bev, arg);
    }

    char buf[BUF_SIZE+1];
    while(evbuffer_get_length(bufferevent_get_input(bev)) && !finish) {

        int r = bufferevent_read(bev, buf, BUF_SIZE);
        if(r == -1) syserr("bufferevent_read");
        buf[r] = 0;
        fprintf(stderr, "%s", buf);
    
    }    
    if (finish) {
        wait_for(&keepalive_thread);
        event_thread = 0;
        void* ret = NULL;
        pthread_exit(&ret);
    } 
    return;
}

void an_event_cb(struct bufferevent *bev, short what, void *arg) {
    if (DEBUG) printf("An event cb\n");
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

void * event_loop(void * arg) { //TODO: ustawic, zeby w tej petli sie jakos to konczylo, bo nie exituje nigdy1 i te free tam dopisac!
    event_thread = pthread_self();

    if (DEBUG) printf("Entering dispatch loop.\n");
    if(event_base_dispatch(base) == -1) syserr("event_base_dispatch");
    if (DEBUG) printf("Dispatch loop finished.\n");

    bufferevent_free(bev);
    event_base_free(base); 
    finish = TRUE; 
    void* ret = NULL;
    wait_for(&keepalive_thread);
    event_thread = 0;
    pthread_exit(&ret); 
}



void match_and_execute(char *datagram, int len) {
    int nr;
    char data[BUF_SIZE+1];
    memset(data, 0, BUF_SIZE);
    //zakladam, ze komunikaty sa poprawne z protokolem, wiec 3. pierwsze argumenty musza byc intami, 4. moze byc pusty
    if (sscanf(datagram, "DATA %d %d %d %[^\n]", &nr, &ack, &win, data) >= 3) {
        DATAs_since_last_datagram++;
        // TODO: malloc, free, blad na retransmisji
        /* *** Error in `./client': free(): invalid next size (fast): 0x0000000000cbf0f0 ***
           *** Error in `./client': malloc(): memory corruption: 0x0000000000cbf110 ***
        */
        if (DATAs_since_last_datagram > 1 && last_sent >= 0 && last_sent == ack) {
            if (DEBUG) printf("RETRANSMISJA KLIENT -> SERWER\n");
            //send_UPLOAD_datagram(last_datagram, last_sent); //TODO: odkomentowac i dopisac rozmiar do parametrow
            DATAs_since_last_datagram = 0;
        }
        if (DEBUG) {
            printf("Zmatchowano do DATA, nr = %d, ack = %d, win = %d, dane = %s\n", nr, ack, win, data); 
        }
        //dlugosc naglowka
        int header_size = 4 + 4;

        if (!nr) header_size++;
        else header_size += (int) ((floor(log10(nr))+1)*sizeof(char));

        if (!ack) header_size++;
        else header_size += (int) ((floor(log10(ack))+1)*sizeof(char));
        
        if (!win)  header_size++; 
        else header_size += (int) ((floor(log10(win))+1)*sizeof(char));

        int data_len = len - header_size;
        //gdy jest to nasz pierwszy DATA datagram 
        if (nr_expected == -1) {
            nr_expected = nr + 1;
            //write(1,data,data_len); //TODO!
            //printf("%s\n",data); // TODO! odkomentowac
        }
        else if (nr > nr_expected) {
            if (nr_expected >= nr - retransfer_lim && nr_expected > nr_max_seen) {                
                send_RETRANSMIT_datagram(nr_expected);
            }
            else {
                nr_expected = nr + 1;
                //write(1,data,data_len);
                //przyjmuje dane -> stdout
               // printf("%s",data); //TODO: odkom
            }
        }
        else if (nr == nr_expected) {
           // write(1,data,data_len);
            //przyjmij dane
           // printf("%s\n",data);//TODO: odkom
        }
        nr_max_seen = max(nr, nr_max_seen);

    }
    else if (sscanf(datagram, "ACK %d %d", &ack, &win) == 2) {
        if (ack == last_sent + 1) 
            DATAs_since_last_datagram = 0;
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

void read_from_UDP() {
    // TODO: cala komunikacja jako odbiorca po UDP (powinien wystarczyc jeden watek)
    // czytanie w petli z UDP (jaka dlugosc? czy na pewno dobrze wczyta? czy moze nie zdazyc wczytac i np. beda juz dw w srodku? na pewno! co wtedy ?)
    // matchowanie komunikatow
    // obsluga komunikatow
    ssize_t len;
    char datagram[BUF_SIZE+1];
    
    int flags = 0; 
    struct sockaddr_in6 server_udp;
    socklen_t rcva_len = (ssize_t) sizeof(server_udp);
            
    while (!finish) {
        do {         
            memset(datagram, 0, sizeof(datagram)); 
            len = recvfrom(sock_udp, datagram, sizeof(datagram), flags,
                    (struct sockaddr *) &server_udp, &rcva_len); 

            if (len < 0) {
                //klopotliwe polaczenie z serwerem
                perror("error on datagram from server socket/ timeout reached");
                reboot();
            }    
            else {
                if (DEBUG) {
                    //(void) printf("read through UDP from [adres:%d]: %zd bytes: %.*s\n", ntohs(server_udp.sin6_port), len,
                    //    (int) len, datagram); //*s oznacza odczytaj z buffer tyle bajtów ile jest podanych w (int) len (do oczytywania stringow, ktore nie sa zakonczona znakiem konca 0
                    //printf("Otrzymano: %s\n", datagram);
                }    
                match_and_execute(datagram, len);    
            }
        } while (len > 0); 
    }
    reboot();
}

void set_event_TCP_stdin() {
    base = event_base_new();
    if(!base) syserr("event_base_new");
    bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
    if(!bev) syserr("bufferevent_socket_new");   


    /* Funkcje, które mają zostać wywołane po wystąpieniu zdarzenia, ustalamy w wywołaniu bufferevent_setcb() */
    bufferevent_setcb(bev, a_read_cb, NULL, an_event_cb, (void *)bev);

    struct addrinfo *addr;

    int port_size = (int) ((floor(log10(port_num))+1)*sizeof(char));    
    char str_port[port_size];
    sprintf(str_port, "%d", port_num);

    if(getaddrinfo(server_name, str_port, &addr_hints, &addr)) syserr("getaddrinfo");

    if(bufferevent_socket_connect(bev, addr->ai_addr, addr->ai_addrlen) == -1)
        syserr("bufferevent_socket_connect");
    freeaddrinfo(addr);

    /* Samo podanie wskaźników do funkcji nie aktywuje ich, do tego używa się funkcji bufferevent_enable() */
    if(bufferevent_enable(bev, EV_READ | EV_WRITE) == -1)
        syserr("bufferevent_enable");
  
    /*struct event *stdin_event =
        event_new(base, 0, EV_READ|EV_PERSIST, stdin_cb, NULL); // 0 - standardwowe wejscie
    if(!stdin_event) syserr("event_new");
    if(event_add(stdin_event,NULL) == -1) syserr("event_add"); */   
}

void main_loop() {
    if (DEBUG) printf("MAIN LOOP\n");
    finish = FALSE;
    main_thread = pthread_self();

    last_datagram = malloc(DATAGRAM_SIZE);
    //sock_udp = create_UDP_socket(); 
    set_event_TCP_stdin();    
    create_thread(&event_loop); //przejmie czytanie z stdin oraz z TCP    
    read_from_UDP();
    free(last_datagram); // TODO: do tego nie powinien dojsc, wrzucic to do zwalniania zasobow
}

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
    main_thread = pthread_self();

    last_datagram = malloc(DATAGRAM_SIZE);
    sock_udp = create_UDP_socket(); 
    set_event_TCP_stdin();    
    create_thread(&event_loop); //przejmie czytanie z TCP   
    create_thread(&read_from_stdin); 
    read_from_UDP();
    free(last_datagram); // TODO: do tego nie powinien dojsc, wrzucic to do zwalniania zasobow    
    return 0;
}

/* 

TODO:

5) zamiast czekać pół minuty mżna trzymać czas od przedostatniego reboota (zapisywać zawsze dwa ostatnie)
 i czkeać max( 500ms - (obecny_czas - czas_przedostatniego_reboota), 0) w milisekundach


*/