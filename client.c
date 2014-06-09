/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/

#include "header.h"

#define PORT  14666
#define BUF_SIZE 8000
#define RCV_SIZE 64000 
#define NAME_SIZE 100 
#define RETRANSMIT_LIMIT 10 
#define DATAGRAM_SIZE 10000 //wiekszy niz BUF_SIZE
#define RETRAN_SIZE 40 
#define MAX_DATAS 10 //TODO: docelowo 1 

#define DEBUG 0

int port_num = PORT;
char server_name[NAME_SIZE];
int retransfer_lim = RETRANSMIT_LIMIT;
evutil_socket_t sock_udp = -1;
evutil_socket_t sock_tcp = -1;
struct sockaddr_in6 server_address;
socklen_t rcva_len = (socklen_t) sizeof(server_address);
long int last_sent = -1; //numer ostatnio wyslanego datagramu DATA
long int ack = -1;
long int win = 0;
int clientid = -1; //nr = -1 oznacza brak identyfikacji
long int nr_expected = -1;
long int nr_max_seen = -1;
char last_UPLOAD[DATAGRAM_SIZE] = {0};
int last_UPLOAD_len;
//char last_RETRANSMIT[RETRAN_SIZE] = {0};
//int last_RETRANSMIT_len;
int UPLOAD_pending = 0;
//int RETRANSMIT_pending = 0;
int DATAs_since_last_datagram = -1; //licznik rusza po otrzymaniu 1. DATA
int events_set = 0;
int eof = 0;


struct event_base *base;
struct event *stdin_event;
struct event *send_event;
struct event *keepalive_event;
struct event *udp_event;
struct event *tcp_event;


void free_events();
void get_parameters(int argc, char *argv[]);
static void catch_int (int sig);
void send_to_server(evutil_socket_t descriptor, short ev, void *arg);
void send_KEEPALIVE_datagram(evutil_socket_t descriptor, short ev, void *arg);
void set_keepalive_event();
void set_send_event();
void set_events();
void send_datagram(char *datagram, int len);
void send_CLIENT_datagram(uint32_t id);
void send_UPLOAD_datagram(void *data, long int no, int data_size);
void read_from_stdin(evutil_socket_t descriptor, short ev, void *arg);
void set_stdin_event();
void send_RETRANSMIT_datagram(long int no);
void match_and_execute(char *datagram, int len);
void read_from_udp(evutil_socket_t descriptor, short ev, void *arg);
void set_udp_event();
void read_CLIENT_datagram();
void read_from_tcp(evutil_socket_t descriptor, short ev, void *arg);
void set_tcp_event();
void set_base();
void clear_data();
evutil_socket_t create_TCP_socket();
evutil_socket_t create_UDP_socket();   
void clear_data();
void reboot();



//usuwa wszystkie wydarzenia
void free_events() {
    if (event_del(stdin_event) == -1) syserr("Can't delete stdin_event"); 
    event_free(stdin_event); 
    
    if (event_del(send_event) == -1) syserr("Can't delete send_event"); 
    event_free(send_event); 
    
    if (event_del(keepalive_event) == -1) syserr("Can't delete keepalive_event"); 
    event_free(keepalive_event);

    if (event_del(udp_event) == -1) syserr("Can't delete udp_event"); 
    event_free(udp_event);

    if (event_del(tcp_event) == -1) syserr("Can't delete tcp_event"); 
    event_free(tcp_event);
    events_set = 0;
}

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

//wczytuje wartosci parametrow z wejscia
void get_parameters(int argc, char *argv[]) {    
    int server_name_set = 0;
    int j;
    for (j = 1; j < argc; j++)  
    {
        if (strcmp(argv[j], "-p") == 0)  
        {
            port_num = atoi(argv[j+1]); 
        }
        else if (strcmp(argv[j], "-s") == 0)
        {    
            strcpy(server_name, argv[j+1]);
            server_name_set = 1;
            addr_hints.ai_family = AF_INET6;
        }
        else if (strcmp(argv[j], "-X") == 0)
        {
            retransfer_lim = atoi(argv[j+1]); 
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

//obsluguje Ctrl+C
static void catch_int (int sig) {
    fprintf(stderr, "INFO: Closing...\n");
    if (events_set) free_events();
    if (event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
    event_base_free(base);
    exit(EXIT_SUCCESS);
}

//wysyla po UDP datagramy z retransmisji
void send_to_server(evutil_socket_t descriptor, short ev, void *arg) {

    ssize_t snd_len;
    int flags = 0;
    
    if (UPLOAD_pending) {
        snd_len = send(sock_udp, last_UPLOAD, last_UPLOAD_len, flags);            
        if (snd_len != last_UPLOAD_len) {  
            fprintf(stderr, "ERROR: send send_to_server\n");
            if (event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
            reboot(); 
        }
        else
            UPLOAD_pending = 0;
    }
    /* (gdyby calosc przesylu po UDP szla tym eventem)
    else if (RETRANSMIT_pending) {
        snd_len = send(sock_udp, last_RETRANSMIT, last_RETRANSMIT_len, flags);
        if (snd_len != last_RETRANSMIT_len) {  
            fprintf(stderr, "ERROR: send send_to_server\n");
            if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak"); 
        }
        else
            RETRANSMIT_pending = 0;
    }    
    //event_del(send_event);
    */  
}

//przesyla po UDP datagram KEEPALIVE 
void send_KEEPALIVE_datagram(evutil_socket_t descriptor, short ev, void *arg) {
    char *datagram = "KEEPALIVE\n";
    send_datagram(datagram, strlen(datagram));   
}

//wydarzenie przesylajace co 0,1s datagram KEEPALIVE
void set_keepalive_event() {
    struct timeval wait_time = { 0, 100000 }; // { s, micro_s}
    keepalive_event =
        event_new(base, sock_udp, EV_PERSIST, send_KEEPALIVE_datagram, NULL); 
    if(!keepalive_event) {
        fprintf(stderr, "ERROR: event_new keepalive_event\n");
        reboot();
    }
    if(event_add(keepalive_event,&wait_time) == -1) {
        fprintf(stderr, "ERROR: event_add keepalive_event\n");
        reboot();
    }
}

//wydarzenie przesylajace datagramy UPLOAD z retransmisji
void set_send_event() {
    send_event =
        event_new(base, sock_udp, EV_WRITE | EV_PERSIST, send_to_server, NULL); 
    if(!send_event) {
        fprintf(stderr, "ERROR: event_new send_event\n");
        reboot();
    }
    if(event_add(send_event,NULL) == -1) {
        fprintf(stderr, "ERROR: event_add send_event\n");
        reboot();
    }
}

//tworzy wydarzenia w systemie
void set_events() {
    set_stdin_event();
    set_tcp_event(); // po przeczytaniu datagramu CLIENT nastepuje set_keepalive_event();
    set_udp_event();
    set_send_event(); //wysyla datagramy UPLOAD i RETRANSMIT po UDP
    events_set = 1;
}

//wysyla datagram o dlugosci len do serwera po UDP
void send_datagram(char *datagram, int len) {
    //fprintf(stderr, "datagram: %s\n",datagram );

    ssize_t snd_len;
    int flags = 0;

    snd_len = send(sock_udp, datagram, len, flags);                     
    
    if (snd_len != len) {  
        fprintf(stderr, "ERROR: send send_datagram\n");
        free(datagram);
        if (event_base_loopbreak(base) == -1) syserr("event_base_loopbreak"); 
        reboot();
    }        
}

//wysyla datagram identyfikacyjny CLIENT ze swoim numerem id
void send_CLIENT_datagram(uint32_t id) { 
    char clientid[11]; /* 11 bytes: 10 for the digits, 1 for the null character */
    snprintf(clientid, sizeof(clientid), "%" PRIu32, id); 

    char* type= "CLIENT";
    char* datagram = malloc(strlen(type) + strlen(clientid) + 3);

    sprintf(datagram, "%s %s\n", type, clientid); 
    send_datagram(datagram, strlen(datagram));  
    free(datagram);
}

//wysyla no-ty datagram typu UPLOAD z danymi data o dlugosci data_size
void send_UPLOAD_datagram(void *data, long int no, int data_size) { 
    long int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str[str_size];
    sprintf(str, "%li", no);

    char* type= "UPLOAD";
    char* datagram = malloc(strlen(type) + strlen(str) + 2 + data_size);
    int len = strlen(type) + strlen(str) + 2 + data_size; //dlugosc calego datagramu 

    char * header = malloc(strlen(type) + strlen(str) + 3);
    sprintf(header, "%s %s\n", type, str);
    memcpy(datagram, header, strlen(header));
    memcpy(datagram + strlen(header), data, data_size);
    
    //kopiuje tresc datagramu do zmiennej trzymajacej ostatnio wyslany datagram
    memset(last_UPLOAD, 0, DATAGRAM_SIZE);
    memcpy(last_UPLOAD, datagram, len);
    send_datagram(datagram, len);

    last_UPLOAD_len = len;
    //UPLOAD_pending = 1; //gdyby calosc przesylu po UDP szla eventen send_to_server

    free(header);
    free(datagram);    
}

//czyta dane ze standardowego wejscia i przesyla do serwera po UDP
void read_from_stdin(evutil_socket_t descriptor, short ev, void *arg) {
    char buf[BUF_SIZE+1];
    memset(buf, 0, sizeof(buf));
    
    if (ack > last_sent && win > 0) { ;           
        if (DEBUG) printf("read\n");
        int to_read = min(win, BUF_SIZE); 
        int r = read(0, buf, to_read);
        if(r < 0) {
            fprintf(stderr, "ERROR: read < 0 read_from_stdin\n");
            if (event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
        }    
        if(r == 0) {            
            fprintf(stderr, "INFO: read stdin reached EOF\n");
            catch_int(SIGINT);                           
        }
        if (r > 0) {
            last_sent++;
            send_UPLOAD_datagram(buf, last_sent, r);                            
        }
        memset(buf, 0, sizeof(buf));
    } 
    else {
        //blokuje stdin
        if (event_del(stdin_event) == -1) syserr("Can't close stdin_event");   
    }      
}

//wydarzenie czytajace dane od klienta z stdin
void set_stdin_event() {
    int stdin = 0;
    stdin_event =
        event_new(base, stdin, EV_READ|EV_PERSIST, read_from_stdin, NULL); 
    if(!stdin_event) {
        fprintf(stderr, "ERROR: event_new stdin_event\n");
        reboot();
    }
    if(event_add(stdin_event,NULL) == -1) {
        fprintf(stderr, "ERROR: event_add stdin_event\n");
        reboot();
    } 
}

//wysyla prosbe o retransmisje serwer->klient datagramow DATA o numerach <= no 
void send_RETRANSMIT_datagram(long int no) {
    long int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 i log(0)
    int str_size = (int) ((floor(log10(num))+1)*sizeof(char));

    char str[str_size];
    sprintf(str, "%li", no);

    char* type= "RETRANSMIT";
    char* datagram = malloc(strlen(type) + strlen(str) + 2);

    sprintf(datagram, "%s %s\n", type, str);
    send_datagram(datagram, strlen(datagram));
    free(datagram);
}

//dopasowuje datagram o dlugosci len do wzorca, obsluguje jego odbior
void match_and_execute(char *datagram, int len) {
    long int nr;
    //DATA
    if (sscanf(datagram, "DATA %li %li %li", &nr, &ack, &win) == 3) {
        DATAs_since_last_datagram++;

        if (ack == last_sent+1 && win > 0) {
            if(event_add(stdin_event,NULL) == -1) {
                fprintf(stderr, "ERROR: event_add stdin_event\n");
                if (event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
            }    
        }
        if (DATAs_since_last_datagram > MAX_DATAS && last_sent >= 0 && last_sent == ack) { 
            fprintf(stderr, "RETRANSMISJA KLIENT -> SERWER\n");
            UPLOAD_pending = 1;
            DATAs_since_last_datagram = 0;
            //if(event_add(send_event,NULL) == -1) syserr("event_add"); 
        }
        //fprintf(stderr, "Zmatchowano do DATA, nr = %d, ack = %d, win = %d\n", nr, ack, win);
        
        char * ptr = memchr(datagram, '\n', len);
        int header_len = ptr - datagram + 1;
        int data_len = len - header_len; 
        if (DEBUG) printf("header_size = %d\n", header_len);

        if (nr_expected == -1) {
            nr_expected = nr + 1;
            write(1,datagram+header_len,data_len); 
        }
        else if (nr > nr_expected) {
            if (nr_expected >= nr - retransfer_lim && nr_expected > nr_max_seen) {                
                send_RETRANSMIT_datagram(nr_expected);
            }
            else {
                nr_expected = nr + 1;
                write(1,datagram+header_len,data_len);
            }
        }
        else if (nr == nr_expected) {
            write(1,datagram+header_len,data_len);
        }
        nr_max_seen = max(nr, nr_max_seen);

    }
    //ACK
    else if (sscanf(datagram, "ACK %li %li", &ack, &win) == 2) {
        if (ack == last_sent + 1) 
            DATAs_since_last_datagram = 0;
        //odblokowanie czytania z stdin
        if (ack == last_sent+1 && win > 0) {
            if (event_add(stdin_event,NULL) == -1) syserr("event_add");
        }
        //fprintf(stderr, "Zmatchowano do ACK, ack = %d, win = %d\n", ack, win);        
    }
    else 
        fprintf(stderr, "WARNING: Niewlasciwy format datagramu\n");
} 

//czyta datagramy od serwera przesylane po UDP
void read_from_udp(evutil_socket_t descriptor, short ev, void *arg) {

    ssize_t len;
    char datagram[RCV_SIZE+1];
    
    int flags = 0; 
        
    memset(datagram, 0, sizeof(datagram)); 
    len = recv(sock_udp, datagram, RCV_SIZE, flags);

    if (len < 0) {
        fprintf(stderr, "ERROR: recv < 0 read_from_udp\n");
        if (event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
    }    
    else { 
        //matchowanie datagramu do wzorcow i dalsza obsluga   
        match_and_execute(datagram, len);    
    }    
}

//wydarzenie czytajace z UDP
void set_udp_event() {
    udp_event =
        event_new(base, sock_udp, EV_READ|EV_PERSIST, read_from_udp, NULL); 
    if(!udp_event) {
        fprintf(stderr, "ERROR: event_new udp_event\n");
        reboot();
    }
    if(event_add(udp_event,NULL) == -1) {
        fprintf(stderr, "ERROR: event_add udp_event\n");
        reboot();
    }    
}

//potwierdza nawiazanie polaczenia TCP z serwerem i odbiera numer identyfikacyjny
void read_CLIENT_datagram() {
    if (DEBUG) {
        printf("read_CLIENT_datagram\n");
    }
    char buf[BUF_SIZE+1];

    int r = read(sock_tcp, buf, BUF_SIZE);
    if (r < 0) {
        fprintf(stderr, "ERROR: read < 0 read_CLIENT_datagram\n");
        if (event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
    }

    if (sscanf(buf, "CLIENT %d\n", &clientid) == 1) {
        if (DEBUG) {
            printf("Otrzymano: %s",buf);
            printf("Zidentyfikowano: clientid = %d\n",clientid);
        }
    }
    
    //przesyla UDP potwierdzenie odebrania datagramu
    send_CLIENT_datagram(clientid);  
    set_keepalive_event();
}

//czyta po TCP 1. datagram CLIENT oraz raporty, ktore wyswietla na stderr
void read_from_tcp(evutil_socket_t descriptor, short ev, void *arg)
{  
    if (clientid < 0) {
        read_CLIENT_datagram();
    }

    char buf[BUF_SIZE+1];
    int r = read(sock_tcp, buf, BUF_SIZE);
    if(r == -1) {
        fprintf(stderr, "ERROR: read<0 read_from_tcp\n");
        reboot();
    }
    else {    
        buf[r] = 0;  
        fprintf(stderr, "%s\n",buf);
    }    
    return;
}

//wydarzenie czytajace z TCP
void set_tcp_event() {
    tcp_event =
        event_new(base, sock_tcp, EV_READ|EV_PERSIST, read_from_tcp, NULL); 
    if(!tcp_event) {
        fprintf(stderr, "ERROR: event_new tcp_event\n");
        reboot();
    }    
    if(event_add(tcp_event,NULL) == -1) {
        fprintf(stderr, "ERROR: event_add tcp_event\n");
        reboot();
    }    
}

//otwiera gniazdo UDP
evutil_socket_t create_UDP_socket() {    
    evutil_socket_t sock;
    struct addrinfo *addr;
    struct addrinfo addr_h = {
        .ai_flags = 0,
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = 0,
        .ai_addrlen = 0,
        .ai_addr = NULL,
        .ai_canonname = NULL,
        .ai_next = NULL
    };

    int port_size = (int) ((floor(log10(port_num))+1)*sizeof(char));    
    char str_port[port_size];
    sprintf(str_port, "%d", port_num);


    if(getaddrinfo(server_name, str_port, &addr_h, &addr)) {
        fprintf(stderr, "ERROR: getaddrinfo UDP");
        reboot();
    }
    sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if(!sock) {
        fprintf(stderr, "ERROR: socket UDP\n");
        reboot();
    }

    struct timeval tv;
    tv.tv_sec = 1; 
    tv.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        fprintf(stderr, "ERROR: brak datagramu przez 1 s\n");
        if (event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
        reboot();
    }

    if(connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
        fprintf(stderr, "ERROR: connect UDP\n");
        reboot();
    }
    if(evutil_make_socket_nonblocking(sock)) {
        fprintf(stderr, "ERROR: evutil_make_socket_nonblocking UDP\n");
        reboot();
    }

    server_address.sin6_family = AF_INET6; 
    server_address.sin6_addr = in6addr_any; //a nie ten wybrany?
    server_address.sin6_port = htons((uint16_t) port_num);
    
    return sock;
}   

//tworzy kontekst dla wydarzen
void set_base() {
    base = event_base_new();
    if (!base) {
        fprintf(stderr, "ERROR: event_base_new\n");
        reboot();
    }    
}

//otwiera gniazdo TCP
evutil_socket_t create_TCP_socket() {
    evutil_socket_t sock;    
    struct addrinfo *addr;

    int port_size = (int) ((floor(log10(port_num))+1)*sizeof(char));    
    char str_port[port_size];
    sprintf(str_port, "%d", port_num);

    if(getaddrinfo(server_name, str_port, &addr_hints, &addr)) {
        fprintf(stderr, "ERROR: getaddrinfo\n");
        reboot();
    } 

    sock = socket(addr->ai_family, addr->ai_socktype, 0);
    if (sock < 0) {
        fprintf(stderr, "ERROR: socket TCP\n");
        reboot();
    }

    if (connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
        fprintf(stderr, "ERROR: connect TCP\n");
        reboot();
    }

    //zablokowanie Nagle'a
    int flag = 1;
    int result = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)); 
    if (result < 0) {
        fprintf(stderr, "ERROR: setsockopt tcp\n");
        reboot();
    }
    return sock;
}        

void clear_data() {
    clientid = -1;
    last_sent = -1; //numer ostatnio wyslanego datagramu DATA
    ack = -1;
    win = 0;
    nr_expected = -1;
    nr_max_seen = -1;
    //last_UPLOAD[DATAGRAM_SIZE] = {0};
    //last_UPLOAD_len;

    UPLOAD_pending = 0;
    DATAs_since_last_datagram = -1;
}

void reboot() {

    fprintf(stderr, "INFO: Rebooting...\n");
    clear_data();
    
    if (events_set) free_events();

    //czeka 500ms
    struct timespec tim, tim2;
    tim.tv_sec = 0;
    tim.tv_nsec = 500 * 1000000; 
    nanosleep(&tim, &tim2); 

    sock_tcp = create_TCP_socket();
    if (sock_udp < 0) sock_udp = create_UDP_socket(); 
    set_base();
    set_events();   

    if (DEBUG) printf("Entering dispatch loop.\n");
    if (event_base_dispatch(base) == -1) syserr("event_base_dispatch");
    if (DEBUG) printf("Dispatch loop finished.\n");

    event_base_free(base); 
    
    reboot();    
}


int main (int argc, char *argv[]) {

    if (DEBUG && argc == 1) 
        printf("Client run with parameters: -s [server_name](obligatory) -p [port_num] -X [retransfer_limit]\n");

    //wylapanie sygnalu konca (Ctrl+C)
    if (signal(SIGINT, catch_int) == SIG_ERR) 
        syserr("Unable to change signal handler\n");

    get_parameters(argc, argv);
    
    sock_tcp = create_TCP_socket();
    sock_udp = create_UDP_socket(); 
    set_base();
    set_events();

    if (DEBUG) printf("Entering dispatch loop.\n");
    if (event_base_dispatch(base) == -1) syserr("event_base_dispatch");
    if (DEBUG) printf("Dispatch loop finished.\n");

    event_base_free(base); 

    reboot(); 
    return 0;
}

