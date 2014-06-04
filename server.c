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
#define DATAGRAM_SIZE 10000 

#define ACTIVE 0
#define FILLING 1 

#define DEBUG 1 

#define BUF_SIZE 64000 //TODO: 64 000


//popraw typy jeszcze
int port_num = PORT;
int fifo_queue_size = FIFO_SIZE; 
int fifo_low = FIFO_LOW_WATERMARK;
int fifo_high;
int buf_length = BUF_LEN;
unsigned long interval = TX_INTERVAL;
unsigned long long last_nr = 0; //ostatnio nadany datagram po zmiksowaniu - TO TRZEBA MIEC, ZEBY IDENTYFIKOWAC WLASNE NADAWANE WIADOMOSCI
int sock_udp;
evutil_socket_t sock_tcp;
struct event_base *base;
int finish = 0;

int pierwszy_poszedl = 0;



struct event *report_event;
struct event *mix_send_event;
struct event *read_event;
struct event *client_event;
struct event *write_event;


struct connection_description {
    int id; 
    struct sockaddr_in6 address; //IPv4 + IPv6
    char * str_address;
    evutil_socket_t sock;
    struct event *ev;
};

// indeks w tabeli jest numerem id klienta
struct info {
    unsigned short port_TCP;
    unsigned short port_UDP;
    struct in6_addr addr_UDP;
    unsigned long  min_FIFO;
    unsigned long max_FIFO;
    int buf_state;
    long long nr; //ostatnio odebrany datagram
    long long ack; //oczekiwany od klienta
    long int buf_count; //stopien zapelnienia bufora buf_count = 10, 10 jest w srodku, czyli 0...9, od 10. pisze
};

typedef struct datagram_address {
    char * datagram;
    struct in6_addr sin_addr;
    unsigned short sin_port;
    int len;
} datagram_address;

typedef struct datagram_struct {
    char data[DATAGRAM_SIZE];
    int len;
} datagram_struct;

struct info client_info[MAX_CLIENTS];

struct connection_description clients[MAX_CLIENTS];

char * buf_FIFO[MAX_CLIENTS];

char * server_FIFO;
int server_count = 0; //ktory datagram jest ostatni w server_FIFO

int activated[MAX_CLIENTS];

struct datagram_struct d[MAX_CLIENTS];

//TODO: sprawdzenie poprawnosci podanych parametrow
void get_parameters(int argc, char *argv[]) {
	int fifo_high_set = 0;
	int j;
    for (j = 1; j < argc; j++)  
    {
        if (strcmp(argv[j], "-p") == 0) 
        {
        	port_num = atoi(argv[j+1]);  
        }
        else if (strcmp(argv[j], "-F") == 0)
    	{    
        	fifo_queue_size = atoi(argv[j+1]);
        }
        else if (strcmp(argv[j], "-L") == 0)
        {
        	fifo_low = atoi(argv[j+1]);
        }
        else if (strcmp(argv[j], "-H") == 0)
        {
        	fifo_high = atoi(argv[j+1]);
        	fifo_high_set = 1;
        }
        else if (strcmp(argv[j], "-X") == 0)
        {
        	buf_length = atoi(argv[j+1]);
        }
        else if (strcmp(argv[j], "-i") == 0)
        {
        	interval = atoi(argv[j+1]);
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


void free_a_client(int i) {
    if (clients[i].ev) {
        event_free(clients[i].ev);
        memset(buf_FIFO[i],0,fifo_queue_size);
    }
}

void free_clients() {
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        free_a_client(i);    
    }
        
}

void free_buf_FIFOs() {
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        free(buf_FIFO[i]);    
    }
}

void delete_events() {
    event_del(report_event);
    event_del(mix_send_event);
    event_del(read_event);
    event_del(client_event);
}

/* Obsługa sygnału kończenia */
static void catch_int (int sig) {
    //TODO: zwalnianie zasobów!!!
	finish = TRUE;
    if (DEBUG) printf("Czeka na zakonczenie watkow\n");

    int i;
    
    free_clients();
    free(server_FIFO);  
    free_buf_FIFOs();
    delete_events();
    event_base_free(base); 
    
  	
    if (DEBUG) {
  		printf("Exit() due to Ctrl+C\n");
  	}
  	exit(EXIT_SUCCESS);
}


void init_clients(void)
{
    memset(clients, 0, sizeof(clients));
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
  	    clients[i].id = i;
        clients[i].ev = NULL;
    }
}

char * addr_to_str(struct sockaddr_in6 *addr) {
    char * str = malloc(sizeof(char) * INET6_ADDRSTRLEN);
    memset(str, 0, INET6_ADDRSTRLEN);
    //char str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &(addr->sin6_addr), str, INET6_ADDRSTRLEN);
    //if (DEBUG) printf("adres klienta: %s\n", str);
    return str;
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


//TODO: wlasciwie to nie wiem, po co to jest... bo my nic od tego kleita nie czytamy na dobra sprawe...
void client_socket_cb(evutil_socket_t sock, short ev, void *arg)
{
    fprintf(stderr, "WCHODZI DO CLIENT SOCKET CB\n");

    struct connection_description *cl = (struct connection_description *)arg;
    char buf[BUF_SIZE+1] = { 0 };

    int r = read(sock, buf, BUF_SIZE);
    if(r <= 0) {
        if(r < 0) {
            fprintf(stderr, "Error (%s) while reading data from adres:%d. Closing connection.\n",
    	       strerror(errno), ntohs(cl->address.sin6_port));//inet_ntoa prostsza, starsza wersja ntop, ntop jest ogólna, podaje sie rodzine adresow
        } else {
            fprintf(stderr, "Connection from adres:%d closed.\n",
    	       ntohs(cl->address.sin6_port));
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
    if (DEBUG) 
        printf("[adres:%d] %s\n", ntohs(cl->address.sin6_port), buf);
}


void send_datagram(char *datagram, int clientid, int len) {
    //if (DEBUG) printf("Wysyla: %s\n",datagram);

    struct sockaddr_in6 client_address;
    client_address.sin6_family = AF_INET6; 
    client_address.sin6_addr = in6addr_any; //TODO: zakomentowac to?
    client_address.sin6_port = htons((uint16_t) client_info[clientid].port_UDP);
    client_address.sin6_flowinfo = 0; /* IPv6 flow information */
    client_address.sin6_scope_id = 0;

    ssize_t snd_len;
    int flags = 0;
    snd_len = sendto(sock_udp, datagram, len, flags,
            (struct sockaddr *) &client_address, sizeof(client_address));    
    
    fprintf(stderr, "send to len = %d =? %d = snd_len \n",len, snd_len);

    if (snd_len != len) {
            syserr("partial / failed sendto");
    }   
    if (DEBUG) printf("Wyslalo\n");     
}

void send_CLIENT_datagram(evutil_socket_t sock, uint32_t id) {
	if (DEBUG) {
        printf("tutaj clientid: %d\n", id);
	   printf("Cokolwiek za\n");
    }   
  	int w;

	char clientid[11]; /* 11 bytes: 10 for the digits, 1 for the null character */
  	//snprintf(str, sizeof str, "%lu", (unsigned long)n); /* Method 1 */
  	snprintf(clientid, sizeof(clientid), "%" PRIu32, id); /* Method 2 */

	char* data= "CLIENT";
	char* datagram = malloc(strlen(data) + strlen(clientid) + 3);

	sprintf(datagram, "%s %s\n", data, clientid);
	//printf("%s o strlen %d", datagram, strlen(datagram));

  	if ((w = write(sock, datagram, strlen(datagram))) == 0) {
  		syserr("write nieudany clientid\n");
  	}	
  	//printf("w = %d\n", w);
  	//printf("size of datagram = %d\n",sizeof(datagram));
  	if (w != strlen(datagram)) syserr("nie przeszlo\n"); 
  	if (DEBUG) printf("przeszlo\n");
    



    free(datagram);



}


void send_DATA_datagram(char *data, int no, int ack, int win, int clientid, int data_len) {
    //write(1,data,data_len); //nie gra jak wszedzie


    if (DEBUG) printf("send_DATA_datagram\n");
    int num = no;
    if (!no) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_no_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_no[str_no_size];
    sprintf(str_no, "%d", no);

    num = ack;
    if (!ack) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_ack_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_ack[str_ack_size];
    sprintf(str_ack, "%d", ack);

    num = win;
    if (!win) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_win_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_win[str_win_size];
    sprintf(str_win, "%d", win);

    char* type= "DATA";
    char * datagram = malloc(strlen(type) + strlen(str_no) + strlen(str_ack) + strlen(str_win) + 4 + data_len);    
    char * header = malloc(strlen(type) + strlen(str_no) + strlen(str_ack) + strlen(str_win) + 5);
    sprintf(header, "%s %s %s %s\n", type, str_no, str_ack, str_win); 
    //fprintf(stderr, "header = %s\n",header);
    //fprintf(stderr, "strlen(header) = %d\n",strlen(header));
    //fprintf(stderr, "strlen(type) + strlen(str_no) + strlen(str_ack) + strlen(str_win) + 5 = %d\n", strlen(type) + strlen(str_no) + strlen(str_ack) + strlen(str_win) + 5);


    memcpy(datagram, header, strlen(header));
    memcpy(datagram + strlen(header), data, data_len);
    
    //fprintf(stderr, "header: %s o dl = %d\n",header, strlen(header));
    //fprintf(stderr, "data_len = %d\n",data_len);
    send_datagram(datagram, clientid, data_len + strlen(header));
    
    //write(1,data,data_len); //tu juz nie dziala
    free(header);
    free(datagram);

}

void send_ACK_datagram(int ack, int win, int clientid) {
    int num = ack;
    if (!ack) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_ack_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_ack[str_ack_size];
    sprintf(str_ack, "%d", ack);

    num = win;
    if (!win) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
    int str_win_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_win[str_win_size];
    sprintf(str_win, "%d", win);

    char* type= "ACK";
    char* datagram = malloc(strlen(type) + strlen(str_ack) + strlen(str_win) + 4);

    sprintf(datagram, "%s %s %s\n", type, str_ack, str_win);
    send_datagram(datagram, clientid, strlen(datagram));
    free(datagram);
}

void send_a_report(evutil_socket_t descriptor, short ev, void *arg) {


    char report[BUF_SIZE];
    memset(report, 0, sizeof(report));
    memcpy(report, "\n", 1);

	int i;
  	for (i = 0; i < MAX_CLIENTS; i++)
    	//jesli klient jest w systemie i jego kolejka aktywna
    	
        //TODO:
    	//TU TRZEBA ODKOMENTOWac!
    	//if(clients[i].ev && client_info[i].buf_state == ACTIVE) {
    	if(clients[i].ev) {
            int offset = strlen(report);
            int report_line = 200;
            char tmp[report_line];
            sprintf(tmp, "[%s:%d] FIFO: %zu/%d (min. %lu, max. %lu)\n",
                     clients[i].str_address, 
                     ntohs(clients[i].address.sin6_port),
                     client_info[i].buf_count, 
                     fifo_queue_size,
                     client_info[i].min_FIFO,
                     client_info[i].max_FIFO
                     );
            memcpy(&report[offset], tmp, strlen(tmp));
    	}

    for (i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i].ev) {
            client_info[i].min_FIFO = fifo_queue_size;
            client_info[i].max_FIFO = 0;
            int w;
            fprintf(stderr, "WYSYLAM RAPORT: %s\n",report);
            if ((w = write(clients[i].sock, report, strlen(report))) == 0) {
                syserr("write: send a report\n");
            }        
        }    
    }   
  
}


//obsluguje polaczenie nowego klienta
void new_client_tcp(evutil_socket_t sock, short ev, void *arg) {

    struct sockaddr_in6 sin;
    socklen_t addr_size = sizeof(struct sockaddr_in6);
    evutil_socket_t connection_socket = accept(sock, (struct sockaddr *)&sin, &addr_size);

    if(connection_socket == -1) syserr("Error accepting connection.");
  
    getpeername(connection_socket, (struct sockaddr *)&sin, &addr_size);
    
    char str_addr[INET6_ADDRSTRLEN];


    if(inet_ntop(AF_INET6, &sin.sin6_addr, str_addr, sizeof(str_addr))) {
        if (DEBUG) printf("Adres klienta: %s\n", str_addr);
        if (DEBUG) printf("Port klienta: %d\n", ntohs(sin.sin6_port));
    }

    //chcemy zapisac tego kleinta	
    struct connection_description *cl = get_client_slot();
    if(!cl) {
        close(connection_socket);
        fprintf(stderr, "get_client_slot, too many clients"); // ew. TODO: wypisuj 
    }
  
    //kopiujemy adres kleinta do struktury
    memcpy(&(cl->address), &sin, sizeof(struct sockaddr_in6));
    cl->sock = connection_socket;
    cl->str_address = addr_to_str(&cl->address);
    //dodaj info o kliencie 
    client_info[cl->id].port_TCP = ntohs(sin.sin6_port);

    send_CLIENT_datagram(connection_socket,cl->id);

    //dla kazdego kleinta z osobna wywoujemy funkcje, rejestrujemy zdarzenie
    struct event *an_event =
        event_new(base, connection_socket, EV_READ|EV_PERSIST, client_socket_cb, (void *)cl);
    if(!an_event) syserr("Error creating event.");
    cl->ev = an_event;
    if(event_add(an_event, NULL) == -1) syserr("Error adding an event to a base.");
	
}

void init_buf_FIFOs() {
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        buf_FIFO[i] = malloc(fifo_queue_size * sizeof(char));
        memset(buf_FIFO[i], 0, fifo_queue_size);     
    }         
}


void init_client_info() {
	memset(client_info, 0, sizeof(client_info));
  	int i;
  	for(i = 0; i < MAX_CLIENTS; i++) {
  		client_info[i].min_FIFO = 0;
  		client_info[i].max_FIFO = 0;
        client_info[i].buf_count = 0;
  		if (fifo_high > 0)
  			client_info[i].buf_state = FILLING;  		
  		else client_info[i].buf_state = ACTIVE;  		
  	}
}


int create_UDP_socket() {
	struct sockaddr_in6 server;
    int on = 1;
	int sock = socket(AF_INET6, SOCK_DGRAM, 0); 
    if (sock < 0)
        syserr("socket"); 

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
        (char *)&on,sizeof(on)) < 0)
        syserr("setsockopt(SO_REUSEADDR)");

    struct timeval tv;
    tv.tv_sec = 20; //TODO: zmien na 1s i to dl akazdego kleinta
    tv.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("Error");
        finish = TRUE;
    }

	server.sin6_family = AF_INET6; 
  	server.sin6_addr = in6addr_any; //wszyskie interfejsy
  	server.sin6_port = htons(port_num); //port num podany na wejsciu 
    server.sin6_flowinfo = 0; /* IPv6 flow information */
    server.sin6_scope_id = 0;
	
    if (bind(sock, (struct sockaddr *) &server,
      (socklen_t) sizeof(server)) < 0)
        syserr("bind");

    if (DEBUG) {
  		//printf("UDP : Server.sin6_addr: %s\n", addr_to_str(&server));
  		printf("UDP : Accepting on port: %hu\n",ntohs(server.sin6_port));
  	}	
    return sock;
}


int get_clientid(struct in6_addr sin_addr, unsigned short sin_port) {
    if (DEBUG) printf("get_clientid\n");
    int id = -1;
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        // TODO:
        /*if (id < 0 && client_info[i].addr_UDP == sin_addr && client_info[i].port_UDP == sin_port) { 
        // TUTAJ SIE NIE KOMPILUJE - INACZEJ TRZEBA POROWNAC TE ADRESY:
            client_info[i].addr_UDP == sin_addr
           pomysl: zapisac do info adres jednak i wtedy
           http://stackoverflow.com/questions/22183561/how-to-compare-two-ip-address-in-c
        */ 
        //atrapa, zeby sie skompilowalo:
        if (id < 0 && client_info[i].port_UDP == sin_port) {   // <- TEMPORARY!!!!!
            if (DEBUG) printf("znaleziono, klient pod nr: %d\n",i);
            id = i;
        }        
    }
    return id;
}

void update_min_max(int i, long long int size) {
    client_info[i].min_FIFO = min(client_info[i].min_FIFO, size);
    client_info[i].max_FIFO = max(client_info[i].max_FIFO, size);
}




void add_new_client(char * datagram, struct in6_addr sin_addr, unsigned short sin_port) {
    if (DEBUG) printf("add_new_client\n");
    if (DEBUG) printf("add new client, datagram: %s\n",datagram);
    int id;
    if (sscanf(datagram, "CLIENT %d\n", &id) == 1) { //TODO: invalid read of size 1
        if (DEBUG) printf("sin port: %d, id: %d\n", sin_port, id);
        client_info[id].port_UDP = sin_port;
        client_info[id].addr_UDP = sin_addr;
        if (DEBUG) printf("Zmatchowano do CLIENT, client_info[id].port_UDP = %d\n", client_info[id].port_UDP);

        client_info[id].ack = 0;
        client_info[id].nr = -1;
        activated[id] = 1;

        //ATRAPA TODO usuń (do ropzoczecia przesyalnaia)
        //send_data(datagram, strlen(datagram));


    }
    else 
        syserr("Bledny datagram poczatkowy\n");
}


void read_from_udp(evutil_socket_t descriptor, short ev, void *arg) {


    char datagram[BUF_SIZE+1];
    ssize_t len;
                                 
    memset(datagram, 0, BUF_SIZE+1);
    int flags = 0; 
    
    struct sockaddr_in6 client_udp;
    socklen_t rcva_len = (ssize_t) sizeof(client_udp);

    len = recvfrom(sock_udp, datagram, BUF_SIZE, flags,
            (struct sockaddr *) &client_udp, &rcva_len);

    //fprintf(stderr, "len = %d",len); // TODO         

    if (len < 0) {
        //klopotliwe polaczenie z serwerem
        perror("error on datagram from client socket/ timeout reached");
    } 


    else {
        if (DEBUG) {
            (void) printf("read through UDP from [adres:%d]: %zd bytes: %.*s\n", ntohs(client_udp.sin6_port), len,
                    (int) len, datagram); //*s oznacza odczytaj z buffer tyle bajtów ile jest podanych w (int) len (do oczytywania stringow, ktore nie sa zakonczona znakiem konca 0
            printf("DATAGRAM: %s\n", datagram);
        }    

        int clientid = get_clientid(client_udp.sin6_addr, ntohs(client_udp.sin6_port));

        if (clientid < 0) { 
            add_new_client(datagram, client_udp.sin6_addr, ntohs(client_udp.sin6_port));
        } 
        else {

            int nr;    
            char data[BUF_SIZE+1] = { 0 };

            //KEEPALIVE
            if (strcmp(datagram, "KEEPALIVE\n") == 0) {
                if (DEBUG) printf("Zmatchowano do keepalive\n");
                //TODO: uaktulanij czas dla tego uzytkownika ??
            }
            //UPLOAD
            else if (sscanf(datagram, "UPLOAD %d", &nr) == 1) {
                fprintf(stderr, "Zmatchowano do UPLOAD, nr = %d\n",nr);

                if (DEBUG) printf("WG MEMCHR i odejmowania wskaznikow\n");
                char * ptr = memchr(datagram, '\n', len);
                int header_len = ptr - datagram + 1;
                int data_len = len - header_len; 
                if (DEBUG) printf("header_size = %d\n", header_len);
                
                //DEBUG: czy dziala przesylanie danych
                //write(1, datagram+header_len, data_len);// jak nie wlaczam miksera, to nawet pyk pyk nie ma
                //write(1, data, data_len); 

                //send_data(datagram+header_len, data_len); //jak tu odsyla to dziala ladnie

                //TODO: if data_len > BUF_SIZE -> zapisuje od poczatku koniec danychw cakym buforze + komunikat

                if (client_info[clientid].buf_count == fifo_queue_size) {
                    fprintf(stderr, "client_info[clientid].buf_count %d\n",client_info[clientid].buf_count);
                    fprintf(stderr, "size, ktory przewazyl: %d\n",data_len );
                    syserr("przepelnienie bufora");
                } 
                
                memcpy(buf_FIFO[clientid] + client_info[clientid].buf_count, datagram+header_len, data_len);
                client_info[clientid].buf_count += data_len;

                if (client_info[clientid].buf_count > BUF_SIZE) syserr("przepelnienie bufora klienta");
                fprintf(stderr, "client_info[clientid].buf_count = %d\n",client_info[clientid].buf_count);

                //write(1, buf_FIFO[clientid] + client_info[clientid].buf_count - data_len, data_len); //dziala tutaj bardzo ladnie nawwet jak przesylam
                //write(1, buf_FIFO[clientid] + client_info[clientid].buf_count - data_len, data_len/2); //dziala
                //write(1, buf_FIFO[clientid] + client_info[clientid].buf_count - data_len, 100); //dziala, odtwarza kawalki tylko


                update_min_max(clientid, client_info[clientid].buf_count);

                if (client_info[clientid].ack == nr) {
                    client_info[clientid].ack++;
                    client_info[clientid].nr = nr;
                }    
                
                int win = fifo_queue_size - client_info[clientid].buf_count;
               // if (win < 0) win = 0; // TODO: blad! nie powinno sie pojawic w ogole (wielowatkowosc?)
                if (DEBUG) printf("send_ACK_datagram(ack, win, clientid): (%d, %d, %d)\n",client_info[clientid].ack, win, clientid );
                if (DEBUG) printf(" client_info[clientid].buf_count: %d\n", client_info[clientid].buf_count);
                send_ACK_datagram(client_info[clientid].ack, win, clientid);

                //send_data(buf_FIFO[clientid] + client_info[clientid].buf_count - data_len, data_len);// ladnie odsyla
            } 
            else if (sscanf(datagram, "RETRANSMIT %d", &nr) == 1) {
                int win = fifo_queue_size - client_info[clientid].buf_count;
                if (DEBUG) printf("Zmachowano do RETRANSMIT\n");
                /*
                if (last_nr - nr >= BUF_LEN) {
                    //przesylam cala tablice
                    int beg = (last_nr + 1) % BUF_LEN;
                    int i;
                    //za last_sent w tablicy (najwczesniejsze)
                    //TODO: konflikt oznaczen
                    for (i = beg; i < BUF_LEN; i++) {
                        char * datagram = malloc(176 * interval);
                        memset(datagram, 0, 176 * interval);
                        memcpy(datagram, &server_FIFO[i * 176 * interval], 176 * interval);
                        send_DATA_datagram(datagram, (last_nr - beg + 1 + i - BUF_LEN) ,client_info[clientid].ack, win,clientid, 176 * interval);
                        
                    }
                    //od poczatku
                    int end = last_nr % BUF_LEN;
                    for (i = 0; i <= end; i++) {
                        char * datagram = malloc(176 * interval);
                        memset(datagram, 0, 176 * interval);
                        memcpy(datagram, &server_FIFO[i * 176 * interval], 176 * interval);
                        send_DATA_datagram(datagram, (last_nr - end + i) ,client_info[clientid].ack, win,clientid, 176 * interval);
                        
                    }
                }
                //nr lezy w tablicy przed last_nr
                else if ((last_nr % BUF_LEN) > (nr % BUF_LEN)) {
                    int i;
                    for (i = nr; i < last_nr ; i++) {
                        char * datagram = malloc(176 * interval);
                        memset(datagram, 0, 176 * interval);
                        memcpy(datagram, &server_FIFO[(i % BUF_LEN) * 176*interval], 176 * interval);
                        send_DATA_datagram(datagram, i, client_info[clientid].ack, win,clientid, 176 * interval);
                        
                    }        
                }
                else if ((nr % BUF_LEN) > (last_nr % BUF_LEN)) {
                    //petla od nr do konca
                    int i;
                    int beg = nr % BUF_LEN;
                    for (i = beg; i < BUF_LEN; i++) {
                        char * datagram = malloc(176 * interval);
                        memset(datagram, 0, 176 * interval);
                        memcpy(datagram, &server_FIFO[i * 176 * interval], 176 * interval);
                        send_DATA_datagram(datagram, (nr - beg + i) ,client_info[clientid].ack, win, clientid, 176 * interval);
                        
                    }
                    //petla od 0 do last
                    int end = last_nr % BUF_LEN;
                    for (i = 0; i <= end; i++) {
                        char * datagram = malloc(176 * interval);
                        memset(datagram, 0, 176 * interval);
                        memcpy(datagram, &server_FIFO[i * 176 * interval], 176 * interval);
                        send_DATA_datagram(datagram, (last_nr - end + i) ,client_info[clientid].ack, win,clientid, 176 * interval);
                        
                    }            
                }
                */        
            }
            else {
                //syserr("Niewlasciwy format datagramu"); //ew. TODO: wypisuj inaczej
                if (DEBUG) printf("niewlasciwy format, datagram: %s\n",datagram);
            } 

            
        }    
    }
}

void add_to_inputs(struct mixer_input* inputs, struct mixer_input input, size_t * position) {
    inputs[*position] = input;
    (*position)++;    
}

void write_to_udp(evutil_socket_t descriptor, short ev, void *arg) {

}

//wysylam dane do wszystkich klientow
void send_data(char * data, size_t size) {

    //DEBUG: sprawdzam, czy dzwiek jest dobry na tym etapie
    //if (size) write(1,data,size); //
    //fprintf(stderr, "size in send_data: %d",size);
    //if (size) printf("data w send_data: %*s\n",size,data);
    //write(1, data, size);
    //if (size) write(1,data,client_info[0].buf_count);

    //if (DEBUG) printf("send_data\n");    
    int i;
    int anyone_inside = 0;

    //przesylam do wszytskich klientow w systemie
    for(i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i].ev && activated[i]) {
            int win = fifo_queue_size - client_info[i].buf_count;
            if (DEBUG) printf("Z send_data:\n"); 
            
            send_DATA_datagram(data, last_nr, client_info[i].ack, win, i, size);   
            anyone_inside = 1;         
        }    
    } 
    
    if (anyone_inside) {
        //zapisuje datagram do kolejki FIFO serwera
        //wyszukuje koniec kolejki:
        int beg = (last_nr % BUF_LEN) * (176 * interval);
        memcpy(&server_FIFO[beg], data, 176 * interval); // TODO: tu jednak nie 176*interval tylko min z tego i tego bufora przekazywanego do miksera
        //pewnie gdzie indziej też to trzeba bezie pozmieniac
        //winno byc rowne sizeof(data) i w sumie strlen(data) tez
        //if (DEBUG) printf("server_FIFO: %s\n",&server_FIFO[beg] ); //dobrze, zapisuje sie gdzie trzeba, 
        //zwykle server_FIFO nie pokazuje calosci, bo zatrzymuje sie na pierwszym \0
        last_nr++;
    }
    //if (DEBUG) printf("Wychodzi z send_data\n");  
}

//taka funckja dziala, troche pyka, ale dzwiek jest czysty, 1% wolniej
/*
void mix_and_send(evutil_socket_t descriptor, short ev, void *arg) {
    if (activated[0]) {
        int ile = min(880, client_info[0].buf_count);
        send_DATA_datagram(buf_FIFO[0], last_nr, client_info[0].ack, fifo_queue_size - client_info[0].buf_count, 0, ile);
        last_nr++;

        //update bufora
        memmove(buf_FIFO[0], &buf_FIFO[0][ile], client_info[0].buf_count - ile);            
        client_info[0].buf_count -= ile;
        //if (DEBUG) printf("client_info[%d].buf_count: %li\n", i,client_info[i].buf_count);
        update_min_max(0, client_info[0].buf_count);
    }
    
} 

*/



void mix_and_send(evutil_socket_t descriptor, short ev, void *arg) {
    
    struct mixer_input inputs[MAX_CLIENTS];
    char output_buf[BUF_SIZE];
    memset(output_buf, 0, BUF_SIZE);
    size_t output_size = BUF_SIZE;
    

    int num_clients = 0;
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
    //jesli klient jest w systemie i jego kolejka aktywna
        if(activated[i]) { //TODO
        //if(clients[i].ev && client_info[i].buf_state == ACTIVE){       
            inputs[num_clients].data = buf_FIFO[i];
            inputs[num_clients].len = client_info[i].buf_count;
            num_clients++;
        }
    }

    //dotad dziala

    mixer(inputs, num_clients, output_buf, &output_size, interval);

    num_clients = 0;
    for(i = 0; i < MAX_CLIENTS; i++) {
    //jesli klient jest w systemie i jego kolejka aktywna
        if(activated[i]) { //TODO
        //if(clients[i].ev && client_info[i].buf_state == ACTIVE){       

            int ile = min(output_size, client_info[i].buf_count);
            fprintf(stderr, "ile = %d\n",ile );
            fprintf(stderr, "output_size = %d\n",output_size);
            //ile = output_size;
            send_DATA_datagram(output_buf, last_nr, client_info[i].ack, fifo_queue_size - client_info[i].buf_count, 0, ile); //TODO to nie powinno byc ile, ale output_size, ale wtedy nie dziala
            
            //update bufora
            memmove(buf_FIFO[i], &buf_FIFO[0][ile], client_info[i].buf_count - inputs[num_clients].consumed);            
            client_info[i].buf_count -= inputs[num_clients].consumed;
            //if (DEBUG) printf("client_info[%d].buf_count: %li\n", i,client_info[i].buf_count);
            update_min_max(0, client_info[i].buf_count);

            num_clients++;
        }
    }

    if (num_clients) last_nr++;
 
} 




void mix_and_send1(evutil_socket_t descriptor, short ev, void *arg) {
    //if (DEBUG) printf("mix_and_send\n");
    //struct mixer_input* inputs = malloc(MAX_CLIENTS * (sizeof(void *) + 2*sizeof(size_t))); //TODO: sprawdzic

    
    int target_size = 176 * interval;
    size_t num_of_clients = 0;

    char out[BUF_SIZE];
    memset(out, 0, BUF_SIZE);    
    struct mixer_input inputs[MAX_CLIENTS];
    size_t out_size = BUF_SIZE;
 
    int i;
    
    //for(i = 0; i < MAX_CLIENTS; i++) 
    //    inputs[i].data = malloc(sizeof(char*));


    for(i = 0; i < MAX_CLIENTS; i++) {
        //jesli klient jest w systemie i jego kolejka aktywna
        if(clients[i].ev) { //TODO
        //if(clients[i].ev && client_info[i].buf_state == ACTIVE){
            inputs[num_of_clients].data = (void *) buf_FIFO[i];
            inputs[num_of_clients].len = client_info[i].buf_count;
            //fprintf(stderr, "inputs[num_of_clients].data %s\n",(char*)inputs[num_of_clients].data ); // nie wystwietla niczego!
            //write(1,inputs[num_of_clients].data, inputs[num_of_clients].len);
            num_of_clients++;
        }
    }

    /*


    if (num_of_clients) {
        //fprintf(stderr, "out przed mikserem %p\n",out ); //te same out
        mixer(inputs, num_of_clients, out, &out_size, interval); 
        int ile = min(inputs[0].len, 880);
        
        //fprintf(stderr, "inputs[0].data %p\n",inputs[0].data );
        //fprintf(stderr, "out po mikserze %p\n",out );// te same out
        //send_data(inputs[0].data,ile);


        //out_size = 880;
        send_data(out, out_size);
    }
    else {
        out_size = 880;
        char * atrapa = malloc(880);
        memset(atrapa, 0, 880);
        send_data(atrapa, 880);
        free(atrapa);
    }       

    if (DEBUG) printf("ZA MIKSEREM\n");

    //output = (char *) output_buf;
*/

    /*update buforow*/
    num_of_clients = 0;
    for(i = 0; i < MAX_CLIENTS; i++) {
        //jesli klient jest w systemie i jego kolejka aktywna TODO
        if(clients[i].ev) {
            if (DEBUG) printf("WSZEDL DO PETLI\n");
            
            int offset = min(880, inputs[num_of_clients].len);
            //int offset = inputs[num_of_clients].consumed;
            fprintf(stderr, "offset = %d\n",offset );
            memmove(&buf_FIFO[i][0], &buf_FIFO[i][offset], fifo_queue_size - offset);            
            memset(&buf_FIFO[i][offset], 0, offset);
            client_info[i].buf_count -= offset;
            if (DEBUG) printf("client_info[%d].buf_count: %li\n", i,client_info[i].buf_count);
            update_min_max(i, client_info[i].buf_count);
            num_of_clients++;
        }
    }
}



void set_base() {
    base = event_base_new();
    //fprintf(stderr, "base %p\n", (void *)base);
    //event_base_priority_init(base, 2);
    if(!base) syserr("Error creating base."); //TODO: probuje dalej
}


evutil_socket_t create_TCP_socket() {
    evutil_socket_t sock = socket(AF_INET6, SOCK_STREAM, 0);

    if(sock == -1 ||
        evutil_make_listen_socket_reuseable(sock) ||
        evutil_make_socket_nonblocking(sock)) { 
        syserr("Error preparing socket.");
    }

    struct sockaddr_in6 sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin6_family = AF_INET6;
    sin.sin6_addr = in6addr_any;
    sin.sin6_port = htons(port_num);
    if(bind(sock, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
        syserr("bind");
    }

    if(listen(sock, 5) == -1) syserr("listen"); // 5 = backlog tells you how many connections may wait in the queue before you accept one

    //TODO: w obu plikach, dac do headera
    int flag = 1;
    int result = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));    
    if (result < 0) {
        fprintf(stderr, "setsockopt tcp\n");
        //TODO: reboot ???
    }    
    return sock;
}

void init_activated() {
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        activated[i] = 0;    
    }

}

void set_client_event() {
    client_event =
        event_new(base, sock_tcp, EV_READ|EV_PERSIST, new_client_tcp, NULL); 
    if(!client_event) syserr("event_new");
    if(event_add(client_event,NULL) == -1) syserr("event_add client");
}

void set_read_event() {
    read_event =
        event_new(base, sock_udp, EV_READ|EV_PERSIST, read_from_udp, NULL); 
    if(!read_event) syserr("event_new");
   // if (event_priority_set(read_event, 1) < 0)
    //    syserr("event priority set"); 
    if(event_add(read_event,NULL) == -1) syserr("event_add udp");
}

void set_mix_send_event() {
    mix_send_event =
        event_new(base, sock_udp, EV_PERSIST, mix_and_send, NULL); 
    if(!mix_send_event) syserr("event_new");

 //   if (event_priority_set(mix_send_event, 1) < 0) //TODO: wlasciwie to dziala i na 0, i na 1, na obu słabo, ale lepiej niz bez niczego
 //       syserr("event priority set");

    struct timeval wait_time = { 0, 1000*interval };
    if(event_add(mix_send_event,&wait_time) == -1) syserr("event_add mix");
}

void set_report_event() {
    struct timeval wait_time = { 1, 0 }; // { 1s, 0 micro_s}
    report_event =
        event_new(base, sock_tcp, EV_PERSIST, send_a_report, NULL); 
    if(!report_event) syserr("event_new");
    if(event_add(report_event,&wait_time) == -1) syserr("event_add report"); 
}

void set_write_event() {
    write_event =
        event_new(base, sock_udp, EV_PERSIST|EV_WRITE, write_to_udp, NULL); 
    if(!write_event) syserr("event_new");
    if(event_add(write_event, NULL) == -1) syserr("event_add write");
}

void set_events() {

    set_client_event(); //odbiera polaczenia od klientow
    set_read_event(); //czyta z udp
    //set_write_event(); //pisze po udp
    set_mix_send_event(); //miskuje i przesyla po udp TODO (jednak write przesyla)
    set_report_event(); //przesyla raporty po tcp

}


int main (int argc, char *argv[]) {

    if (DEBUG && argc == 1) {
        printf("Server run with parameters: -p [port_number] -F [fifo_size] -L [fifo_low_watermark] "
         "-H [fifo_high_watermark] -X [buffer_size] -i [tx_interval] \n");
    }

    /* Ctrl-C konczy porogram */
    if (signal(SIGINT, catch_int) == SIG_ERR) {
        syserr("Unable to change signal handler\n");
    }

    server_FIFO = malloc(176 * interval * BUF_LEN); //wsadzamy co 176 * interval miejsc nadane datargramy
    memset(server_FIFO, 0, 176 * interval * BUF_LEN);

	get_parameters(argc, argv);
    init_activated();
	init_client_info();
    init_buf_FIFOs();
  	init_clients(); //gniazda dla TCP

    sock_udp = create_UDP_socket();
    sock_tcp = create_TCP_socket();
    set_base();
    set_events();

    if (DEBUG) printf("Entering dispatch loop.\n");
    if(event_base_dispatch(base) == -1) syserr("Error running dispatch loop.");
    if (DEBUG) printf("Dispatch loop finished.\n");

    event_base_free(base);    

	return 0;
}

/* TODO:

- event_free wszedzie


1) W przypadku wykrycia kłopotów z połączeniem przez serwer, powinien on zwolnić wszystkie zasoby związane z tym klientem

-> sprawdz, czy sie cos tworzy zanim sie nawiaze polaczenie, jak tak, to zwolnij
uwaga! to sie tylko dotyczy TCP! 


2) Jeśli serwer przez sekundę nie odbierze żadnego datagramu UDP, uznaje połączenie za kłopotliwe. 
   Zezwala się jednakże na połączenia TCP bez przesyłania danych UDP, jeśli się chce zobaczyć tylko raporty.

   -> wiec tutaj chyba bede musiala trzymac czas dla kazdego kleinta 
   tablica czasow[MAX_CLIENTS]

   i sprawdzac dla kazdego, czy juz nie bylo timeoutu

   ale tez updateowac te czasy przy odbiorze keepalive (bo to wystarczy)


3) ucinanie adresow IPv4 do swojej dlugosci z IPv6

4) ustawianie wartosci kolejek dla klientow -> sprawdzanie ???

5) poprawić stałe wszytskie

6) czy na pewno inty mają być w wielu miejscah, a nie long long inty na przyklad

7) poprawic styl retransmisji

*/
