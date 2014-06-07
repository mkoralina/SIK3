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
#define BUF_SIZE 64000

#define ACTIVE 0
#define FILLING 1 

#define DEBUG 1 


//TODO: popraw typy jeszcze
int port_num = PORT;
int fifo_queue_size = FIFO_SIZE; 
int fifo_low = FIFO_LOW_WATERMARK;
int fifo_high;
int buf_length = BUF_LEN;
unsigned long interval = TX_INTERVAL;
unsigned long long last_nr = 0; //ostatnio nadany datagram po zmiksowaniu - TO TRZEBA MIEC, ZEBY IDENTYFIKOWAC WLASNE NADAWANE WIADOMOSCI

evutil_socket_t sock_udp;
evutil_socket_t sock_tcp;
struct event_base *base;
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


struct info client_info[MAX_CLIENTS]; //indeks w tabeli jest numerem id klienta

struct connection_description clients[MAX_CLIENTS];

char * buf_FIFO[MAX_CLIENTS];

char * server_FIFO;
int server_count = 0; //ktory datagram jest ostatni w server_FIFO

int activated[MAX_CLIENTS]; //czy klient podlaczyl sie juz przez TCP



//TODO: sprawdzenie poprawnosci podanych parametrow
//wczytanie parametrow
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
        fprintf(stderr, "WARNING: wartosc FIFO_HIGH_WATERMARK (-H) może być zbyt wysoka,"
            "żeby zapewnić odpowiednią jakość dźwięku. Zalecana wartość: 0-500\n");
    }

    if (DEBUG) {	    	
	    printf("port_num: %d\n", port_num);
	    printf("fifo_queue_size: %d\n", fifo_queue_size);
	    printf("fifo_low: %d\n", fifo_low);
	    printf("fifo_high: %d\n", fifo_high);
	    printf("buf_length: %d\n", buf_length);	
    }	
}

//uwolnienie gniazda, czyszczenie bufora i deaktywacja
void free_a_client(int i) {
    if (clients[i].ev) {
        event_free(clients[i].ev);
        memset(buf_FIFO[i],0,fifo_queue_size);
        activated[i] = 0;
    }
}

//czysci zasoby wszystkich klientow
void free_clients() {
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        free_a_client(i);    
    }        
}

//uwalnia pamiec z wszystkich buforow klientow
void free_buf_FIFOs() {
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        free(buf_FIFO[i]);    
    }
}

//usuwa wszystkie wydarzenia
void delete_events() {
    event_del(report_event);
    event_del(mix_send_event);
    event_del(read_event);
    event_del(client_event);
}

//obsluguje Ctrl+C
static void catch_int (int sig) {
    int i;    
    free_clients();
    free(server_FIFO);  
    free_buf_FIFOs();
    delete_events();
    event_base_free(base); 
  	exit(EXIT_SUCCESS);
}

//inicjalizuje tablice polaczen TCP
void init_clients(void)
{
    memset(clients, 0, sizeof(clients));
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
  	    clients[i].id = i;
        clients[i].ev = NULL;
    }
}

//wolne okienko dla nowego klienta
struct connection_description *get_client_slot(void)
{
    int i;
    for(i = 0; i < MAX_CLIENTS; i++)
        if(!clients[i].ev)
            return &clients[i];
    return NULL;
}

//TODO: cos tu dorobic moze jeszcze (te timeouty czy cos moze)
//kontroluje polaczenie TCP z klientem
void client_socket_cb(evutil_socket_t sock, short ev, void *arg)
{
    struct connection_description *cl = (struct connection_description *)arg;
    char buf[BUF_SIZE+1] = { 0 };

    int r = read(sock, buf, BUF_SIZE);
    if(r <= 0) {
        if(r < 0) {
            fprintf(stderr, "Error (%s) while reading data from adres:%d. Closing connection.\n",
    	       strerror(errno), ntohs(cl->address.sin6_port));
        } else {
            fprintf(stderr, "Connection from adres:%d closed.\n",
    	       ntohs(cl->address.sin6_port));
        }    
        if(event_del(cl->ev) == -1) syserr("Can't delete the event.");

        clear_client_info(cl->id);
        free_a_client(cl->id);
        

        if(close(sock) == -1) syserr("Error closing socket.");
        cl->ev = NULL;
        return;
    }
}

//wysyla datagram dlugosci len do klieta o id = clientid
void send_datagram(char *datagram, int clientid, int len) {

    struct sockaddr_in6 client_address;
    client_address.sin6_family = AF_INET6; 
    client_address.sin6_addr = client_info[clientid].addr_UDP; 
    client_address.sin6_port = htons(client_info[clientid].port_UDP);

    ssize_t snd_len;
    int flags = 0;
    snd_len = sendto(sock_udp, datagram, len, flags,
            (struct sockaddr *) &client_address, sizeof(client_address));    
    
    if (snd_len != len) {
        syserr("partial / failed sendto"); //TODO: syserr
    }      
}

//wysyla datagram typu CLIENT do klienta o zadanym id
void send_CLIENT_datagram(evutil_socket_t sock, uint32_t id) {
	if (DEBUG) {
        printf("tutaj clientid: %d\n", id);
	   printf("Cokolwiek za\n");
    }   
  	int w;

	char clientid[11];
  	snprintf(clientid, sizeof(clientid), "%" PRIu32, id); 

	char* data= "CLIENT";
	char* datagram = malloc(strlen(data) + strlen(clientid) + 3);

	sprintf(datagram, "%s %s\n", data, clientid);

  	if ((w = write(sock, datagram, strlen(datagram))) == 0) {
  		syserr("write nieudany clientid\n");
  	}	

  	if (w != strlen(datagram)) syserr("nie przeszlo\n"); 
    free(datagram);
}

//wysyla datagram typu DATA o numerze no, danych data o dlugosci data_len do klienta
//o numerze id = clientid, od ktorego oczekuje sie datagramu nr ack wielkosci <= win 
void send_DATA_datagram(char *data, int no, int ack, int win, int clientid, int data_len) {
    if (DEBUG) printf("send_DATA_datagram\n");
    int num = no;
    if (!no) num = 1; //na wypadek gdyby log(0)
    int str_no_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_no[str_no_size];
    sprintf(str_no, "%d", no);

    num = ack;
    if (!ack) num = 1; 
    int str_ack_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_ack[str_ack_size];
    sprintf(str_ack, "%d", ack);

    num = win;
    if (!win) num = 1; 
    int str_win_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_win[str_win_size];
    sprintf(str_win, "%d", win);

    char* type= "DATA";
    char * datagram = malloc(strlen(type) + strlen(str_no) + strlen(str_ack) + strlen(str_win) + 4 + data_len);    
    char * header = malloc(strlen(type) + strlen(str_no) + strlen(str_ack) + strlen(str_win) + 5);
    sprintf(header, "%s %s %s %s\n", type, str_no, str_ack, str_win);

    memcpy(datagram, header, strlen(header));
    memcpy(datagram + strlen(header), data, data_len);

    send_datagram(datagram, clientid, data_len + strlen(header));
    
    free(header);
    free(datagram);
}

//wysyla datagram typu ACK potwierdzajacy odbior datagramu ack-1
void send_ACK_datagram(int ack, int win, int clientid) {
    int num = ack;
    if (!ack) num = 1; 
    int str_ack_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_ack[str_ack_size];
    sprintf(str_ack, "%d", ack);

    num = win;
    if (!win) num = 1; 
    int str_win_size = (int) ((floor(log10(num))+1)*sizeof(char));
    
    char str_win[str_win_size];
    sprintf(str_win, "%d", win);

    char* type= "ACK";
    char* datagram = malloc(strlen(type) + strlen(str_ack) + strlen(str_win) + 4);

    sprintf(datagram, "%s %s %s\n", type, str_ack, str_win);
    send_datagram(datagram, clientid, strlen(datagram));
    free(datagram);
}

//wysyla do wszystkich raport o wszystkich aktywnych klientach w systemie
void send_a_report(evutil_socket_t descriptor, short ev, void *arg) {
    char report[BUF_SIZE];
    memset(report, 0, sizeof(report));
    memcpy(report, "\n", 1);

	int i;
  	for (i = 0; i < MAX_CLIENTS; i++)
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

    //tworzy okienko w tablicy klientow
    struct connection_description *cl = get_client_slot();
    if(!cl) {
        close(connection_socket);
        fprintf(stderr, "get_client_slot, too many clients"); // ew. TODO: wypisuj 
    }
  
    //kopiuje adres klienta do struktury
    memcpy(&(cl->address), &sin, sizeof(struct sockaddr_in6));
    cl->sock = connection_socket;
    cl->str_address = addr_to_str(&cl->address);
    client_info[cl->id].port_TCP = ntohs(sin.sin6_port);

    //potwierdza nawiazanie polaczenia wysylac datagram CLIENT
    send_CLIENT_datagram(connection_socket,cl->id);

    //dla kazdego klienta rejestruje wydarzenie
    struct event *an_event =
        event_new(base, connection_socket, EV_READ|EV_PERSIST, client_socket_cb, (void *)cl);
    if(!an_event) syserr("Error creating event.");
    cl->ev = an_event;
    if(event_add(an_event, NULL) == -1) syserr("Error adding an event to a base.");
}

//inicjuje kolejki FIFO klientow
void init_buf_FIFOs() {
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        buf_FIFO[i] = malloc(fifo_queue_size * sizeof(char));
        memset(buf_FIFO[i], 0, fifo_queue_size);     
    }         
}

//inicjuje tablice z informacjami klientow
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

//czysci tablice z informacjami o kliencie
clear_client_info(int i){
    client_info[i].min_FIFO = 0;
    client_info[i].max_FIFO = 0;
    client_info[i].buf_count = 0;
    if (fifo_high > 0)
        client_info[i].buf_state = FILLING;         
    else client_info[i].buf_state = ACTIVE;     
}

//tworzy gniazdo UDP
evutil_socket_t create_UDP_socket() {
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
  		printf("UDP : Server.sin6_addr: %s\n", addr_to_str(&server));
  		printf("UDP : Accepting on port: %hu\n",ntohs(server.sin6_port));
  	}	
    return sock;
}

//identyfikuje klienta na podstawie adresu i portu
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
        if (id < 0 && client_info[i].port_UDP == sin_port) {   // <- TEMPORARY!!!!! TODO
            if (DEBUG) printf("znaleziono, klient pod nr: %d\n",i);
            id = i;
        }        
    }
    return id;
}

//update'uje statystyki klienta
void update_min_max(int i, long long int size) {
    //update min i max
    client_info[i].min_FIFO = min(client_info[i].min_FIFO, size);
    client_info[i].max_FIFO = max(client_info[i].max_FIFO, size);
    //update buf_state
    if (client_info[i].buf_count >= fifo_high)
        client_info[i].buf_state = ACTIVE;
    else
        client_info[i].buf_state = FILLING;
}

//uzupelnia strukture client_info dla nowego klienta, od ktorego otrzymano
//1. datagram po UDP (zgodnie z protokolem: datagram CLIENT)
void add_new_client(char * datagram, struct in6_addr sin_addr, unsigned short sin_port) {
    if (DEBUG) printf("add_new_client\n");
    if (DEBUG) printf("add new client, datagram: %s\n",datagram);
    int id;
    if (sscanf(datagram, "CLIENT %d\n", &id) == 1) {
        if (DEBUG) printf("sin port: %d, id: %d\n", sin_port, id);
        client_info[id].port_UDP = sin_port;
        client_info[id].addr_UDP = sin_addr;
        if (DEBUG) printf("Zmatchowano do CLIENT, client_info[id].port_UDP = %d\n", client_info[id].port_UDP);

        client_info[id].ack = 0;
        client_info[id].nr = -1;
        activated[id] = 1;

    }
    else 
        syserr("Bledny datagram poczatkowy\n");
}

//przetwarza datagram UPLOAD o numerze nr i dlugosci len od uzytkownika clientid
void process_UPLOAD(char * datagram, int nr, int clientid, int len) {
    fprintf(stderr, "Zmatchowano do UPLOAD, nr = %d\n",nr);

    if (nr == client_info[clientid].ack) {

        if (DEBUG) printf("WG MEMCHR i odejmowania wskaznikow\n");
        char * ptr = memchr(datagram, '\n', len);
        int header_len = ptr - datagram + 1;
        int data_len = len - header_len; 
        if (DEBUG) printf("header_size = %d\n", header_len);
        
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

        update_min_max(clientid, client_info[clientid].buf_count);

        if (client_info[clientid].ack == nr) {
            client_info[clientid].ack++;
            client_info[clientid].nr = nr;
        }    
        
        int win = fifo_queue_size - client_info[clientid].buf_count;
        if (DEBUG) printf("send_ACK_datagram(ack, win, clientid): (%d, %d, %d)\n",client_info[clientid].ack, win, clientid );
        if (DEBUG) printf(" client_info[clientid].buf_count: %d\n", client_info[clientid].buf_count);
        send_ACK_datagram(client_info[clientid].ack, win, clientid);
    }
    else {
        if (DEBUG) printf("Ponownie otrzymano UPLOAD %d\n",nr);
        int win = fifo_queue_size - client_info[clientid].buf_count;
        send_ACK_datagram(client_info[clientid].ack, win, clientid); //w razie gdyby ACK sie zgubil
    } 
}

//przetwarza datagram KEEPALIVE od klietna clientid
void process_KEEPALIVE(int clientid) {
    if (DEBUG) printf("Zmatchowano do keepalive\n");
    //TODO: uaktulanij czas dla tego uzytkownika ??
}

//przetwarza datagram RETRANSMIT od klienta clientid i parametrze nr 
void process_RETRANSMIT(int nr, int clientid){
    int win = fifo_queue_size - client_info[clientid].buf_count;
    if (DEBUG) printf("Zmachowano do RETRANSMIT\n");
    
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
}

//czyta datagramy przez UDP
void read_from_udp(evutil_socket_t descriptor, short ev, void *arg) {
    char datagram[BUF_SIZE+1];
    ssize_t len;                                 
    memset(datagram, 0, BUF_SIZE+1);
    int flags = 0;     
    struct sockaddr_in6 client_udp;
    socklen_t rcva_len = (ssize_t) sizeof(client_udp);

    len = recvfrom(sock_udp, datagram, BUF_SIZE, flags,
            (struct sockaddr *) &client_udp, &rcva_len);
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
            //nowy klient, osadz go w systemie
            add_new_client(datagram, client_udp.sin6_addr, ntohs(client_udp.sin6_port));
        } 
        else {
            //stary klient, obsluz jego datagram
            int nr;    
            char data[BUF_SIZE+1] = { 0 };

            //KEEPALIVE
            if (strcmp(datagram, "KEEPALIVE\n") == 0)                 
                process_KEEPALIVE(clientid);            
            //UPLOAD
            else if (sscanf(datagram, "UPLOAD %d", &nr) == 1)
                process_UPLOAD(datagram, nr, clientid, len);            
            //RETRANSMIT
            else if (sscanf(datagram, "RETRANSMIT %d", &nr) == 1) 
                process_RETRANSMIT(nr, clientid);            
            //BLAD
            else 
                fprintf(stderr, "WARNING: Niewlasciwy format datagramu\n");                         
        }    
    }
}

//miksuje dane i przesyla do klientow
void mix_and_send(evutil_socket_t descriptor, short ev, void *arg) {
    
    struct mixer_input inputs[MAX_CLIENTS];
    char output_buf[BUF_SIZE];
    memset(output_buf, 0, BUF_SIZE);
    size_t output_size = BUF_SIZE;
    
    //wyszukuje aktywnych klientow i zbiera ich dane
    int num_clients = 0;
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        if(activated[i] && client_info[i].buf_state == ACTIVE) { //TODO: zmienic na clients[i].ev ?      
            inputs[num_clients].data = buf_FIFO[i];
            inputs[num_clients].len = client_info[i].buf_count;
            num_clients++;
        }
    }

    mixer(inputs, num_clients, output_buf, &output_size, interval);

    num_clients = 0;
    int anyone_in = 0;
    for(i = 0; i < MAX_CLIENTS; i++) {
        //przesyla dane do wszystkich klientow w systemie
        int size = min(output_size, client_info[i].buf_count);
        if(activated[i]) { //TODO: zmienic na clients[i].ev?             
            if (DEBUG) {
                fprintf(stderr, "size = %d\n",size);
                fprintf(stderr, "output_size = %d\n",output_size);
            }    
            send_DATA_datagram(output_buf, last_nr, client_info[i].ack, fifo_queue_size - client_info[i].buf_count, i, size); //TODO to nie powinno byc ile, ale output_size, ale wtedy nie dziala
            anyone_in = 1;
        }

        //uaktualnia bufory klientow z aktywna kolejka
        if (activated[i] && client_info[i].buf_state == ACTIVE) {   
            memmove(buf_FIFO[i], &buf_FIFO[i][size], client_info[i].buf_count - inputs[num_clients].consumed);            
            client_info[i].buf_count -= inputs[num_clients].consumed;            
            update_min_max(i, client_info[i].buf_count);
            num_clients++;
        }
    }

    //jesli wyslal komukolwiek, zwieksza licznik, zapisuje dane
    if (anyone_in) {
        last_nr++;
        int position = (last_nr % BUF_LEN) * 176 * interval;
        memcpy(&server_FIFO[position],output_buf,output_size);
    }     

} 

//tworzy kontekst dla wydarzen
void set_base() {
    base = event_base_new();
    if(!base) syserr("Error creating base."); //TODO: probuje dalej
}

//tworzy gniazdo TCP
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

//inicjuje tablice aktywnych uzytkownikow (= tych, ktorzy podlaczyli sie po
//TCP, ale jeszcze nie przyslali nic po UDP), aby Ci mogli otrzymac pierwszy
//komunikat DATA 
void init_activated() {
    int i;
    for(i = 0; i < MAX_CLIENTS; i++) {
        activated[i] = 0;    
    }

}

//wydarzenie do obslugi klientow po TCP
void set_client_event() {
    client_event =
        event_new(base, sock_tcp, EV_READ|EV_PERSIST, new_client_tcp, NULL); 
    if(!client_event) syserr("event_new");
    if(event_add(client_event,NULL) == -1) syserr("event_add client");
}

//wydarzenie do czytania po UDP i wysylania datagramow (poza DATA)
void set_read_event() {
    read_event =
        event_new(base, sock_udp, EV_READ|EV_PERSIST, read_from_udp, NULL); 
    if(!read_event) syserr("event_new");
   // if (event_priority_set(read_event, 1) < 0)
    //    syserr("event priority set"); 
    if(event_add(read_event,NULL) == -1) syserr("event_add udp");
}

//wydarzenie miksujace i wysylajace datagramy DATA po UDP
void set_mix_send_event() {
    mix_send_event =
        event_new(base, sock_udp, EV_PERSIST, mix_and_send, NULL); 
    if(!mix_send_event) syserr("event_new");
    struct timeval wait_time = { 0, 1000*interval };
    if(event_add(mix_send_event,&wait_time) == -1) syserr("event_add mix");
}

//wysyla raporty po TCP
void set_report_event() {
    struct timeval wait_time = { 1, 0 }; // { 1s, 0 micro_s}
    report_event =
        event_new(base, sock_tcp, EV_PERSIST, send_a_report, NULL); 
    if(!report_event) syserr("event_new");
    if(event_add(report_event,&wait_time) == -1) syserr("event_add report"); 
}

//otwiera wydarzenia w systemie
void set_events() {
    set_client_event(); //odbiera polaczenia od klientow
    set_read_event(); //czyta z udp
    set_mix_send_event(); //miskuje i przesyla po udp 
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
