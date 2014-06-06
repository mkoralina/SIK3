/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/

#include "header.h"

#define PORT  14666
#define BUF_SIZE 10000
#define RCV_SIZE 64000 
#define NAME_SIZE 100 //TODO: ile to ma byc?
#define RETRANSMIT_LIMIT 10 
#define DATAGRAM_SIZE 10000 

#define DEBUG 0

int port_num = PORT;
char server_name[NAME_SIZE];
int retransfer_lim = RETRANSMIT_LIMIT;
evutil_socket_t sock_udp;
evutil_socket_t sock_tcp;
struct sockaddr_in6 server_address;
socklen_t rcva_len = (socklen_t) sizeof(server_address);
int last_sent = -1; //numer ostatnio wyslanego datagramu DATA
int ack = -1;
int win = 0;
int clientid = -1; //nr = -1 oznacza brak identyfikacji
int nr_expected = -1;
int nr_max_seen = -1;
int connection_lost = 0;
char * last_datagram;
int DATAs_since_last_datagram = 0;


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

//TODO: obsluga blednego formatu
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

//czyta muzyke ze standardowego wejscia i przesyla do serwera po UDP
void read_from_stdin(evutil_socket_t descriptor, short ev, void *arg) {
    char buf[BUF_SIZE+1];
    memset(buf, 0, sizeof(buf));
    
    if (ack > last_sent && win > 0) { ;           
        if (DEBUG) printf("read\n");
        int to_read = min(win, BUF_SIZE); 
        int r = read(0, buf, to_read);
        if(r < 0) {
            fprintf(stderr, "w evencie: read (from stdin)\n");
            syserr("DEBUG: blad");
            //TODO
        }    
        if(r == 0) {
            fprintf(stderr,"stdin closed. Exiting event loop.\n");
            syserr("DEBUG: blad");
            //TODO                
        }
        if (r > 0) {
            last_sent++;
            send_UPLOAD_datagram(buf, last_sent, r);                            
        }
        memset(buf, 0, sizeof(buf));
    } 
    else {
        //blokuje stdin
        event_del(stdin_event);   
    }      
}

//wydarzenie czytajace dane od klienta z stdin
void set_stdin_event() {
    int stdin = 0;
    stdin_event =
        event_new(base, stdin, EV_READ|EV_PERSIST, read_from_stdin, NULL); 
    if(!stdin_event) syserr("event_new");
    if(event_add(stdin_event,NULL) == -1) syserr("event_add"); 
}

//czyta po TCP 1. datagram CLIENT oraz raporty, ktore wyswietla na stderr
void read_from_tcp(evutil_socket_t descriptor, short ev, void *arg)
{  
    // TODO: kontrola, czy jest caly czas polaczenie, czyli pewnie jakiś timeout trzeba ustawic!
    if (clientid < 0) {
        read_CLIENT_datagram();
    }

    char buf[BUF_SIZE+1];
    int r = read(sock_tcp, buf, BUF_SIZE);
    if(r == -1) syserr("fail on read_from_tcp"); //TODO: reboot
    buf[r] = 0; //znak konca 
    fprintf(stderr, "%s", buf); 
    return;
}

//wydarzenie czytjace z TCP
void set_tcp_event() {
    struct event *tcp_event =
        event_new(base, sock_tcp, EV_READ|EV_PERSIST, read_from_tcp, NULL); 
    if(!tcp_event) syserr("event_new");
    if(event_add(tcp_event,NULL) == -1) syserr("event_add");    
}

//czyta datagramy od serwera przesylane po UDP
void read_from_udp(evutil_socket_t descriptor, short ev, void *arg) {

    ssize_t len;
    char datagram[RCV_SIZE+1];
    
    int flags = 0; 
    struct sockaddr_in6 server_udp;
    socklen_t rcva_len = (ssize_t) sizeof(server_udp);
        
    memset(datagram, 0, sizeof(datagram)); 
    len = recv(sock_udp, datagram, RCV_SIZE, flags);

    if (len < 0) {
        //klopotliwe polaczenie z serwerem TODO
        perror("error on datagram from server socket/ timeout reached");
        if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
        //reboot(); //TODO
    }    
    else { 
        //matchowanie datagramu do wzorcow i dalsza obsluga   
        match_and_execute(datagram, len);    
    }    
}

//wydarzenie czytajace z UDP
void set_udp_event() {
    struct event *udp_event =
        event_new(base, sock_udp, EV_READ|EV_PERSIST, read_from_udp, NULL); 
    if(!udp_event) syserr("event_new");
    if(event_add(udp_event,NULL) == -1) syserr("event_add");    
}

//przesyla po UDP datagram KEEPALIVE 
void send_KEEPALIVE_datagram(evutil_socket_t descriptor, short ev, void *arg) {
    char *datagram = "KEEPALIVE\n";
    send_datagram(datagram, strlen(datagram));
    return 0;    
}

//wydarzenie przesylajace co 0,1s datagram KEEPALIVE
void set_keepalive_event() {
    struct timeval wait_time = { 0, 100000 }; // { s, micro_s}
    struct event *keepalive_event =
        event_new(base, sock_udp, EV_PERSIST, send_KEEPALIVE_datagram, NULL); 
    if(!keepalive_event) syserr("event_new");
    if(event_add(keepalive_event,&wait_time) == -1) syserr("event_add");  
}

//tworzy wydarzenia w systemie
void set_events() {
    set_stdin_event();
    set_tcp_event(); // po przeczytaniu datagramu CLIENT nastepuje set_keepalive_event();
    set_udp_event();
}

//restartuje prace klienta
void reboot() {
    //zwalnia zasoby i zamyka zdarzenia
    //(event_base_loopbreak(base) == -1)
    event_base_free(base);
    //TODO

    //czeka 500ms
    struct timespec tim, tim2;
    tim.tv_sec = 0;
    tim.tv_nsec = 500 * 1000000; 
    nanosleep(&tim, &tim2); 
    
    //restartuje klienta
    // sprawdz czy jest polaczone udp, bo do tego nie musi sie podlaczac jesli tak, jesli nie, to musi
    //tcp zawsze musi
    //moze tez jakis main loopp po prostu
    set_events(); //TODO: o ile blad nie wystapil gdzies wczesniej przy tworzeniu gniazda albo bazy
}

//wysyla datagram o dlugosci len do serwera po UDP
void send_datagram(char *datagram, int len) {
    fprintf(stderr, "datagram: %s\n",datagram );

    ssize_t snd_len;
    int flags = 0;

    snd_len = send(sock_udp, datagram, len, flags);
    fprintf(stderr, "snd_len %d\n",snd_len);                      
    
    if (snd_len != len) {  
        //TODO: oblusga  
        perror("partial / failed sendto");
        free(datagram);
        if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak"); //TODO: to raczej przeniesc do gory
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
void send_UPLOAD_datagram(void *data, int no, int data_size) { 
    int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str[str_size];
    sprintf(str, "%d", no);

    char* type= "UPLOAD";
    char* datagram = malloc(strlen(type) + strlen(str) + 2 + data_size);
    int len = strlen(type) + strlen(str) + 2 + data_size; //dlugosc calego datagramu 

    char * header = malloc(strlen(type) + strlen(str) + 3);
    sprintf(header, "%s %s\n", type, str);
    memcpy(datagram, header, strlen(header));
    memcpy(datagram + strlen(header), data, data_size);

    send_datagram(datagram, len); 
    
    //kopiuje tresc datagramu do zmiennej trzymajacej ostatnio wyslany datagram
    memset(last_datagram, 0, len);
    memcpy(last_datagram, data, len);

    free(header);
    free(datagram);    
}

//wysyla prosbe o retransmisje serwer->klient datagramow DATA o numerach <= no 
void send_RETRANSMIT_datagram(int no) {
    int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 i log(0)
    int str_size = (int) ((floor(log10(num))+1)*sizeof(char));

    char str[str_size];
    sprintf(str, "%d", no);

    char* type= "RETRANSMIT";
    char* datagram = malloc(strlen(type) + strlen(str) + 2);

    sprintf(datagram, "%s %s\n", type, str);
    send_datagram(datagram, strlen(datagram));
    free(datagram);
}

//TODO
//obsluguje Ctrl+C
static void catch_int (int sig) {
    //zwalnia zasoby

    //zamyka wydarzenia

    free(last_datagram);
    if(event_base_loopbreak(base) == -1) syserr("event_base_loopbreak");
    exit(EXIT_SUCCESS);
}

//potwierdza nawiazanie polaczenia TCP z serwerem i odbiera numer identyfikacyjny
void read_CLIENT_datagram() {
    if (DEBUG) {
        printf("read_CLIENT_datagram\n");
    }
    char buf[BUF_SIZE+1];

    int r = read(sock_tcp, buf, BUF_SIZE);
    if(r == -1) syserr("read client datagram read");

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
        fprintf(stderr, "Error on getaddrinfo udp");
    }
    sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if(!sock) {
        //syserr("socket udp");
        syserr("Error socket udp\n");
    }
    if(connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
        //syserr("Can not connect to serwer udp");
        syserr("Can not connect to serwer udp\n");
    }
    if(evutil_make_socket_nonblocking(sock)) {
        //syserr("error in evutil_make_socket_nonblocking");
        syserr("Error in evutil_make_socket_nonblocking\n");
    }

    server_address.sin6_family = AF_INET6; 
    server_address.sin6_addr = in6addr_any; //a nie ten wybrany?
    server_address.sin6_port = htons((uint16_t) port_num);
    
    return sock;
}   

//dopasowuje datagram o dlugosci len do wzorca, obsluguje jego odbior
void match_and_execute(char *datagram, int len) {
    int nr;
    //DATA
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
    else if (sscanf(datagram, "ACK %d %d", &ack, &win) == 2) {
        if (ack == last_sent + 1) 
            DATAs_since_last_datagram = 0;
        //odblokowanie czytania z stdin
        if (ack == last_sent+1 && win > 0) {
            if(event_add(stdin_event,NULL) == -1) syserr("event_add");
        }
        if (DEBUG) printf("Zmatchowano do ACK, ack = %d, win = %d\n", ack, win);        
    }
    else 
        fprintf(stderr, "WARNING: Niewlasciwy format datagramu\n");
} 

//konwertuje adres sockaddr_in6 na char *
char * addr_to_str(struct sockaddr_in6 *addr) {
    char str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &(addr->sin6_addr), str, INET6_ADDRSTRLEN);
    return str;
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

    if(getaddrinfo(server_name, str_port, &addr_hints, &addr)) syserr("getaddrinfo");

    sock = socket(addr->ai_family, addr->ai_socktype, 0);
    if (sock < 0) {
        syserr("socket tcp");
        fprintf(stderr, "socket tcp\n");
        //TODO: reboot
    }

    if (connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
        fprintf(stderr, "ERROR: connect tcp\n");
        int jest = 0;
        while (!jest) {
            if (connect(sock, addr->ai_addr, addr->ai_addrlen) < 0)
                fprintf(stderr, "ERROR: connect tcp\n");
            else
                jest = 1;
        }
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

    if (DEBUG && argc == 1) 
        printf("Client run with parameters: -s [server_name](obligatory) -p [port_num] -X [retransfer_limit]\n");

    //wylapanie sygnalu konca (Ctrl+C)
    if (signal(SIGINT, catch_int) == SIG_ERR) 
        syserr("Unable to change signal handler\n");

    get_parameters(argc, argv);
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