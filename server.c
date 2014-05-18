/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/

#include "mixer.h"
#include "header.h" 

#define PORT 14666 //numer portu, z którego korzysta serwer do komunikacji (zarówno TCP, jak i UDP)
#define FIFO_SIZE 10560 //rozmiar w bajtach kolejki FIFO, którą serwer utrzymuje dla każdego z klientów; ustawiany parametrem -F serwera
#define FIFO_LOW_WATERMARK 0 //ustawiany parametrem -L serwera
#define BUF_LEN 10 //rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera
#define TX_INTERVAL 5 //czas (w ms) pomiędzy kolejnymi wywołaniami miksera, ustawiany parametrem -i serwera 
#define QUEUE_LENGTH 5 //liczba kleintow w kolejce do gniazda
#define MAX_CLIENTS 30 

#define ACTIVE 0
#define FILLING 1 

#define DEBUG 1 

#define BUF_SIZE 1024

static int finish = FALSE;

//popraw typy jeszcze
int port_num = PORT;
int fifo_queue_size = FIFO_SIZE; 
int fifo_low = FIFO_LOW_WATERMARK;
int fifo_high;
int buf_length = BUF_LEN;
int interval = TX_INTERVAL;
int nr; //ostatnio nadany datagram po zmiksowaniu - TO TRZEBA MIEC, ZEBY IDENTYFIKOWAC WLASNE NADAWANE WIADOMOSCI
int sock_udp;
evutil_socket_t listener_socket;
struct event_base *base;
struct event *listener_socket_event;


void get_parameters(int argc, char *argv[]) {
	int fifo_high_set = 0;
	int j;
    for (j = 1; j < argc; j++)  
    {
        if (strcmp(argv[j], "-p") == 0) 
        {
        	port_num = atoi(argv[j]);  
        }
        else if (strcmp(argv[j], "-F") == 0)
    	{    
        	fifo_queue_size = atoi(argv[j]);
        }
        else if (strcmp(argv[j], "-L") == 0)
        {
        	fifo_low = atoi(argv[j]);
        }
        else if (strcmp(argv[j], "-H") == 0)
        {
        	fifo_high = atoi(argv[j]);
        	fifo_high_set = 1;
        }
        else if (strcmp(argv[j], "-X") == 0)
        {
        	buf_length = atoi(argv[j]);
        }
        else if (strcmp(argv[j], "-i") == 0)
        {
        	interval = atoi(argv[j]);
        }
    }

    if (!fifo_high_set) {
    	fifo_high = fifo_queue_size;
    }

    if (DEBUG) {	    	
	    printf("port_num: %d\n", port_num);
	    printf("fifo_queue_size: %d\n", fifo_queue_size);
	    printf("fifo_low: %d\n", fifo_low);
	    printf("fifo_high: %d\n", fifo_high);
	    printf("buf_length: %d\n", buf_length);	
    }	
}

/* Obsługa sygnału kończenia */
static void catch_int (int sig) {
	/* zwalniam zasoby */
    //ogarnic te watki i zwalnianie pamieci po mallocu
	finish = TRUE;
  	if (DEBUG) {
  		printf("Exit() due to Ctrl+C\n");
  	}
  	//TODO
  	printf("Poczekaj jeszcze na procesy potomne albo zabij je\n");
  	exit(EXIT_SUCCESS);
}

// LIBEVENT

struct connection_description {
    int id;	
    struct sockaddr_in address;
    evutil_socket_t sock;
    struct event *ev;
};

// indeks w tabeli jest numerem id klienta
struct info {
	int port_TCP;
	int port_UDP;
    struct in_addr addr_UDP;
	int min_FIFO;
	int max_FIFO;
	char **buf_FIFO;
	int buf_state;
    int nr; //ostatnio odebrany datagram
    int ack; //oczekiwany od klienta
};

typedef struct datagram_address {
    char * datagram;
    struct in_addr sin_addr;
    unsigned short sin_port;
} datagram_address;

struct info client_info[MAX_CLIENTS];

struct connection_description clients[MAX_CLIENTS];

void init_clients(void)
{
    memset(clients, 0, sizeof(clients));
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
  	    clients[i].id = i;
    }
}

struct connection_description *get_client_slot(void)
{
    int i;
    //szukamy pustego slotu dla nowego klienta
    for(i = 0; i < MAX_CLIENTS; i++)
        if(!clients[i].ev)
            return &clients[i];
    return NULL;
}

void client_socket_cb(evutil_socket_t sock, short ev, void *arg)
{
    struct connection_description *cl = (struct connection_description *)arg;
    char buf[BUF_SIZE+1];

    int r = read(sock, buf, BUF_SIZE);
    if(r <= 0) {
        if(r < 0) {
            fprintf(stderr, "Error (%s) while reading data from %s:%d. Closing connection.\n",
    	       strerror(errno), inet_ntoa(cl->address.sin_addr), ntohs(cl->address.sin_port));//inet_ntoa prostsza, starsza wersja ntop, ntop jest ogólna, podaje sie rodzine adresow
        } else {
            fprintf(stderr, "Connection from %s:%d closed.\n",
    	       inet_ntoa(cl->address.sin_addr), ntohs(cl->address.sin_port));
        }
        if(event_del(cl->ev) == -1) syserr("Can't delete the event.");
    	//zwalniamy meijsce w tablicy
        event_free(cl->ev);
        if(close(sock) == -1) syserr("Error closing socket.");
        cl->ev = NULL;
        return;
    }
    buf[r] = 0;
    //wypisujemy adres i port klienta ladnie
    printf("[%s:%d] %s\n", inet_ntoa(cl->address.sin_addr), ntohs(cl->address.sin_port), buf);
}


void send_CLIENT_datagram(evutil_socket_t sock, uint32_t id) {
	printf("tutaj clientid: %d\n", id);
	printf("Cokolwiek za\n");
  	int w;

	char clientid[11]; /* 11 bytes: 10 for the digits, 1 for the null character */
  	//snprintf(str, sizeof str, "%lu", (unsigned long)n); /* Method 1 */
  	snprintf(clientid, sizeof(clientid), "%" PRIu32, id); /* Method 2 */

	char* data= "CLIENT";
	char* datagram = malloc(strlen(data) + strlen(clientid) + 2);

	sprintf(datagram, "%s %s\n", data, clientid);
	//printf("%s o strlen %d", datagram, strlen(datagram));
	//printf("I co>? jest w nowej linijce\n");

  	if ((w = write(sock, datagram, strlen(datagram))) == 0) {
  		syserr("write nieudany clientid\n");
  	}	
  	//printf("w = %d\n", w);
  	//printf("size of datagram = %d\n",sizeof(datagram));
  	if (w != strlen(datagram)) syserr("nie przeszlo\n"); 
  	printf("przeszlo\n");
}

//nieprzetestowane
void send_DATA_datagram(char *data, int no, int ack, int win, int clientid) {
    int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_no_size = (int) ((ceil(log10(num))+1)*sizeof(char));
    
    char str_no[str_no_size];
    sprintf(str_no, "%d", no);

    num = ack;
    if (!ack) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_ack_size = (int) ((ceil(log10(num))+1)*sizeof(char));
    
    char str_ack[str_ack_size];
    sprintf(str_ack, "%d", ack);

    num = win;
    if (!win) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_win_size = (int) ((ceil(log10(num))+1)*sizeof(char));
    
    char str_win[str_win_size];
    sprintf(str_win, "%d", ack);

    char* type= "DATA";
    char* datagram = malloc(strlen(type) + strlen(str_no) + strlen(str_ack) + strlen(str_win) + 2 + strlen(data));

    sprintf(datagram, "%s %s %s %s\n%s", type, str_no, str_ack, str_win, data);
    send_datagram(datagram, clientid);
}

void send_ACK_datagram(int ack, int win, int clientid) {
    int num = ack;
    if (!ack) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_ack_size = (int) ((ceil(log10(num))+1)*sizeof(char));
    
    char str_ack[str_ack_size];
    sprintf(str_ack, "%d", ack);

    num = win;
    if (!win) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_win_size = (int) ((ceil(log10(num))+1)*sizeof(char));
    
    char str_win[str_win_size];
    sprintf(str_win, "%d", ack);

    char* type= "ACK";
    char* datagram = malloc(strlen(type) + strlen(str_ack) + strlen(str_win) + 2);

    sprintf(datagram, "%s %s %s %s\n%s", type, str_ack, str_win);
    send_datagram(datagram, clientid);
}

//nieprzestestowane
void send_datagram(char *datagram, int clientid) {
    struct sockaddr_in client_address;
    client_address.sin_family = AF_INET; 
    //client_address.sin_addr.s_addr = htonl(INADDR_ANY); //DOPIPSAC TO DO STRUKTURY CLIENT INFO I WYCIAGAC STAMTAD !!!!
    client_address.sin_port = htons((uint16_t) client_info[clientid].port_UDP);

    ssize_t snd_len;
    int flags = 0;
    snd_len = sendto(sock_udp, datagram, strlen(datagram), flags,
            (struct sockaddr *) &client_address, sizeof(client_address));    
    
    if (snd_len != strlen(datagram)) {
            syserr("partial / failed sendto");
    }        
}

void send_a_report() {
	//wersja beta:
	//create and print a report
	printf("\n");
	int i;
  	for(i = 0; i < MAX_CLIENTS; i++)
    	//jesli klient jest w systemie i jego kolejka aktywna
    	
    	//TU TRZEBA ODKOMENTOWac!
    	//if(clients[i].ev && client_info[i].buf_state == ACTIVE) {
    	if(clients[i].ev) {	
    		printf("[PID:%d] ",getpid());
    		printf("[klient:%d] ",i);
    		printf("[%s:%d] FIFO: %zu/%d (min. %d, max. %d)\n",
    			 inet_ntoa(clients[i].address.sin_addr), 
    			 ntohs(clients[i].address.sin_port),
    			 strlen(client_info[i].buf_FIFO),
    			 fifo_queue_size,
    			 client_info[i].min_FIFO,
    			 client_info[i].max_FIFO
    			 );
    	}
	
    //normalnie powinno być:
    //create a report	
	//multisend a report
    //sleep(1);    
    struct timespec tim, tim2;
    tim.tv_sec = 1; //1s
    tim.tv_nsec = 0; //0
    nanosleep(&tim, &tim2);    
}


//obsluguje polaczenie nowego klienta
void listener_socket_cb(evutil_socket_t sock, short ev, void *arg)
{
    struct event_base *base = (struct event_base *)arg;

    struct sockaddr_in sin;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    evutil_socket_t connection_socket = accept(sock, (struct sockaddr *)&sin, &addr_size);

    if(connection_socket == -1) syserr("Error accepting connection.");
  
    //chcemy zapisac tego kleinta	
    struct connection_description *cl = get_client_slot();
    if(!cl) {
        close(connection_socket);
        fprintf(stderr, "Ignoring connection attempt from %s:%d due to lack of space.\n",
	       inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
        return;
    }
  
    //kopiujemy adres kleinta do struktury
    memcpy(&(cl->address), &sin, sizeof(struct sockaddr_in));
    cl->sock = connection_socket;
    //dodaj info o kliencie 
    client_info[cl->id].port_TCP = ntohs(sin.sin_port);

    send_CLIENT_datagram(connection_socket,cl->id);

    //dla kazdego kleinta z osobna wywoujemy funkcje, rejestrujemy zdarzenie
    struct event *an_event =
        event_new(base, connection_socket, EV_READ|EV_PERSIST, client_socket_cb, (void *)cl);
    if(!an_event) syserr("Error creating event.");
    cl->ev = an_event;
    if(event_add(an_event, NULL) == -1) syserr("Error adding an event to a base.");
	
    send_a_report();

}



void init_client_info(int fifo_queue_size) {
	memset(client_info, 0, sizeof(client_info));
  	int i;
  	for(i = 0; i < MAX_CLIENTS; i++) {
  		client_info[i].buf_FIFO = malloc(fifo_queue_size * sizeof(char*));
        memset(client_info[i].buf_FIFO, 0, fifo_queue_size * sizeof(char*)); 
  		client_info[i].min_FIFO = 0;
  		client_info[i].max_FIFO = 0;
  		if (fifo_high > 0)
  			client_info[i].buf_state = FILLING;  		
  		else client_info[i].buf_state = ACTIVE;  		
  	}
}


int create_udp_socket() {
	struct sockaddr_in server;

	int sock_udp = socket(AF_INET, SOCK_DGRAM, 0); 
    if (sock_udp < 0)
        syserr("socket"); 

	server.sin_family = AF_INET; 
  	server.sin_addr.s_addr = htonl(INADDR_ANY); //we listen on all interfaces
  	server.sin_port = htons(port_num); //port num podany na wejsciu 
	
    if (bind(sock_udp, (struct sockaddr *) &server,
      (socklen_t) sizeof(server)) < 0)
        syserr("bind");

    if (DEBUG) {
  		printf("UDP : Server.sin_addr.s_addr: %hu\n", server.sin_addr.s_addr);
  		printf("UDP : Accepting on port: %hu\n",ntohs(server.sin_port));
  	}
    return sock_udp; 	
}

//nieprzetestowane
int get_clientid(struct in_addr sin_addr, unsigned short sin_port) {
    printf("get_clientid\n");
    int id = -1;
    int i;
    int found = 0;
    for(i = 0; i < MAX_CLIENTS; i++) {
        /*if (!found && client_info[i].addr_UDP == sin_addr && client_info[i].port_UDP == sin_port) { 
        // TUTAJ SIE NIE KOMPILUJE - INACZEJ TRZEBA POROWNAC TE ADRESY:
            client_info[i].addr_UDP == sin_addr
           pomysl: zapisac do info adres jednak i wtedy
           http://stackoverflow.com/questions/22183561/how-to-compare-two-ip-address-in-c
        */ 
        //atrapa, zeby sie skompilowalo:
        printf("[%d] %d =? %d\n",i,client_info[i].port_UDP, sin_port);
        if (!found && client_info[i].port_UDP == sin_port) {   // <- TEMPORARY!!!!!
            printf("znaleziono\n");
            found = 1;
            id = i;
        }        
    }
    return id;
}

void match_and_execute(char *datagram, int clientid) {
    if (DEBUG) printf("[TID:%d] match_and_execute: %s\n",syscall(SYS_gettid),datagram);
    int nr;    
    //char *data;
    char data[BUF_SIZE];
    if (sscanf(datagram, "UPLOAD %d %[^\n]", &nr, data) >= 2) {
        printf("[TID:%d] Zmatchowano do UPLOAD, nr = %d, dane = %s\n", syscall(SYS_gettid), nr, data);
        //obsluz UPLOAD
        //wpisuejsz [dane] do kolejki zwiazanej z klientem
        //if client_info[clientid].ack == nr
        //int ack = client_info[clientid].ack
        //send_ACK_datagram()
    }
    else if (sscanf(datagram, "RETRANSMIT %d", &nr) == 1) {
        printf("[TID:%d] Zmachowano do RETRANSMIT\n",syscall(SYS_gettid));
    }
    else if (strcmp(datagram, "KEEPALIVE\n") == 0) {
        printf("[TID:%d] Zmatchowano do KEEPALIVE\n",syscall(SYS_gettid));
    }
    else 
        //syserr("Niewlasciwy format datagramu");
        printf("Niewlasciwy format datagramu\n");
    //jesli zmachowano do upload
}    //do tego pamietaj o updateowaniu wszystkich wskaxnikow (ack, nr, win itp.)
    

void add_new_client(char * datagram, struct in_addr sin_addr, unsigned short sin_port) {
    if (DEBUG) printf("[TID:%d] add_new_client\n",syscall(SYS_gettid));
    if (DEBUG) printf("datagram: %s\n",datagram);
    int id;
    if (sscanf(datagram, "CLIENT %d\n", &id) == 1) {
        printf("sin port: %d, id: %d\n", sin_port, id);
        client_info[id].port_UDP = sin_port;
        client_info[id].addr_UDP = sin_addr;
        printf("Zmatchowano do CLIENT, client_info[id].port_UDP = %d\n", client_info[id].port_UDP);
    }
    else 
        syserr("Bledny datagram poczatkowy\n");
}

void process_datagram(void *param) {
    datagram_address da = *(datagram_address*)param;
    char *datagram = da.datagram;
    struct in_addr sin_addr = da.sin_addr;
    unsigned short sin_port = da.sin_port;
    
   // match_and_execute(datagram, 2); //na sztywno do testow

/* NA RAZIE, ZEBY LATWO BYLO DEBUGOWAC, BO TU SIE WYWALA
    if (DEBUG) printf("[TID:%d] process_datagram\n",syscall(SYS_gettid));
    int clientid = get_clientid(sin_addr, sin_port);
    if (clientid < 0) {
        //klienta nie ma w tabeli, jesli datagram jest typu CLIENT, trzeba go dodac 
        add_new_client(datagram, sin_addr, sin_port);
    }
    else {
        match_and_execute(datagram, clientid);
    }   */
}

void read_from_udp() {
    ssize_t len;
    for (;;) {
        do {
            //char datagram[BUF_SIZE+1];
            int size = BUF_SIZE +1;
            char *datagram;

            datagram = (char *) malloc(sizeof(char) * size);
            //datagram = malloc(sizeof *datagram * (BUF_SIZE+1));
            if (!datagram) {
                syserr("malloc");
            }
            memset(datagram, 0, size); 
            //memset(datagram, 0, sizeof(datagram)); 

            //struct datagram_address *da = malloc(sizeof(struct datagram_address));
            // do tego jeszcze trzeba zaalokować dla da.datagram na pewno (moze i reszte)

            int flags = 0; 
            
            struct sockaddr_in client_udp;
            socklen_t rcva_len = (ssize_t) sizeof(client_udp);

            len = recvfrom(sock_udp, datagram, sizeof(datagram), flags,
                    (struct sockaddr *) &client_udp, &rcva_len); //rcva_len - to się zawsze na wszelki wypadek inicjuje

            if (len < 0)
                  syserr("error on datagram from client socket");
            else {
                (void) printf("read through UDP from [%s:%d]: %zd bytes: %.*s\n", inet_ntoa(client_udp.sin_addr), ntohs(client_udp.sin_port), len,
                        (int) len, datagram); //*s oznacza odczytaj z buffer tyle bajtów ile jest podanych w (int) len (do oczytywania stringow, ktore nie sa zakonczona znakiem konca 0
                printf("DATAGRAM: %s\n", datagram);
                struct datagram_address da;
                da.datagram = datagram;
                da.sin_addr = client_udp.sin_addr;
                da.sin_port = ntohs(client_udp.sin_port); //UWAGA BO TO ZMIENIAM, A TEGO NA GORZE NIE
                create_UDP_thread(&da);
                free(datagram);
            }
        } while (len > 0); //dlugosc 0 jest ciezko uzyskac
        (void) printf("finished exchange\n");
    }
}


void create_UDP_thread(datagram_address* arg) {
    /*przygotowaniu watku*/
    pthread_t r; /*wynik*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);-
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED); 
    
    /*stworzenie watku*/
    pthread_create(&r,&attr,process_datagram,(void*)arg);
}

//ZBIC WSZYTSKIE CREATE THREAD za pomoca pointerow do funkcji (1.arg - funkcja, 2. arg - arg dla funkcji, jesli jest, NULL wpp)
void create_reading_thread() {
    /*przygotowaniu watku*/
    pthread_t r; /*wynik*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED); 
    
    /*stworzenie watku*/
    pthread_create(&r,&attr,read_from_udp,NULL);    
}

void event_loop() {

    printf("Entering dispatch loop.\n");
    if(event_base_dispatch(base) == -1) syserr("Error running dispatch loop.");
    printf("Dispatch loop finished.\n");

    event_free(listener_socket_event);
    event_base_free(base);    
}

void create_event_thread() {
    pthread_t r; /*wynik*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);-
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED); 

    pthread_create(&r,&attr,event_loop,NULL);    
}

void create_report_thread() {
    pthread_t r; /*wynik*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);-
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED); 

    pthread_create(&r,&attr,send_a_report,NULL);     
}

void mix_data() {
    //miskowanie danych
}

void send_data() {
    //przesylanie do wszytskich klientow
}

int main (int argc, char *argv[]) {

    if (DEBUG && argc == 1) {
        printf("Server run with parameters: -p [port_number] -F [fifo_size] -L [fifo_low_watermark] "
         "-H [fifo_high_watermark] -X [buffer_size] -i [tx_interval] \n");
    }

	get_parameters(argc, argv);
	init_client_info(fifo_queue_size);

	/* Ctrl-C konczy porogram */
  	if (signal(SIGINT, catch_int) == SIG_ERR) {
    	syserr("Unable to change signal handler\n");
  	}

  	// <LIBEVENT>
	

  	init_clients();

  	base = event_base_new();
  	if(!base) syserr("Error creating base.");

  	
  	listener_socket = socket(AF_INET, SOCK_STREAM, 0);
  	if(listener_socket == -1 ||
     	evutil_make_listen_socket_reuseable(listener_socket) ||
     	evutil_make_socket_nonblocking(listener_socket)) { 
     	//gniazdfo zwroci blad, jesli nie ma danych do odczytu (dlatego, ze mamy pod tym funkcje poll ktora nam zwraca, kiedy sa dane, wiec powinny byc
    	syserr("Error preparing socket.");
  	}

  	struct sockaddr_in sin;
  	sin.sin_family = AF_INET;
  	sin.sin_addr.s_addr = htonl(INADDR_ANY);
  	sin.sin_port = htons(4242);
  	if(bind(listener_socket, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
    	syserr("bind");
  	}

  	if(listen(listener_socket, 5) == -1) syserr("listen");

  	listener_socket_event = 
    	event_new(base, listener_socket, EV_READ|EV_PERSIST, listener_socket_cb, (void *)base);
  	if(!listener_socket_event) syserr("Error creating event for a listener socket.");

  	if(event_add(listener_socket_event, NULL) == -1) syserr("Error adding listener_socket event.");


    create_event_thread();
    sock_udp = create_udp_socket();	

    create_report_thread();
    create_reading_thread();

    for (;;) {
        mix_data();
        send_data();
    }   
       
	return 0;
}