/*
 Monika Konopka, 334666,
 Data: 13.05.2014r. 
*/

/* poll:
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
*/

#include <event2/event.h>
#include <event2/util.h>

#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h> 

// snprintf
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>


#include <string.h> /* strcmp */
#include "mixer.h"
#include "err.h"

 #include <time.h>


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


//stare stale
#define TRUE 1
#define FALSE 0
#define BUF_SIZE 1024

static int finish = FALSE;

//popraw typy jeszcze
int port_num = PORT;
int fifo_queue_size = FIFO_SIZE; 
int fifo_low = FIFO_LOW_WATERMARK;
int fifo_high;
int buf_length = BUF_LEN;
int interval = TX_INTERVAL;
struct pollfd client[_POSIX_OPEN_MAX]; //gniazda klientow


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

/* Zamyka deskryptory i gniazdo glowne klientow */
void close_descriptors() {
	int j;
	for (j = 1; j < _POSIX_OPEN_MAX; ++j) {
		if (client[j].fd >= 0)
			if (close(client[j].fd) < 0)
	            syserr("Closing client's descriptor");
	} 
	if (client[0].fd >= 0)
    	if (close(client[0].fd) < 0)
      		syserr("Closing main socket");
}

/* Inicjuje tablice z gniazdami klientew, client[0] to gniazdko centrali */
void initiate_client() {
	static int i;
  	for (i = 0; i < _POSIX_OPEN_MAX; ++i) {
    	client[i].fd = -1; //puste 
    	client[i].events = POLLIN; //
    	client[i].revents = 0; //zeruje przed wywolaniem
  	}
}

/* Tworzy gniazdo centrali */
void create_main_socket() {
  	client[0].fd = socket(PF_INET, SOCK_STREAM, 0); 
  	if (client[0].fd < 0) {
    	syserr("Opening stream socket");
  	}
}

/* Obsługa sygnału kończenia */
static void catch_int (int sig) {
	/* zwalniam zasoby */
	close_descriptors();
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
	int min_FIFO;
	int max_FIFO;
	char **buf_FIFO;
	int buf_state;
};

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

	char* data= "DATA";
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
    			 sizeof(client_info[i].buf_FIFO),
    			 fifo_queue_size,
    			 client_info[i].min_FIFO,
    			 client_info[i].max_FIFO
    			 );
    	}
      			
    //normalnie powinno być:
    //create a report	
	//multisend a report
	
}


//obsluguje polaczenie nowego klienta
void listener_socket_cb(evutil_socket_t sock, short ev, void *arg)
{
  struct event_base *base = (struct event_base *)arg;

  struct sockaddr_in sin;
  socklen_t addr_size = sizeof(struct sockaddr_in);
  //po accept dostajemy nowe gniazdo
  printf("Dostajemy nnowe gniazdo na accept zaraz\n");
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

  //zanim pozwolimy na czytanie od klienta: przesyłamy mu jego numer
  //identyfikacyjny ten numer to moze byc numer gniazda??????

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


// </LIBEVENT





void init_client_info(int fifo_queue_size) {
	memset(client_info, 0, sizeof(client_info));
  	int i;
  	for(i = 0; i < MAX_CLIENTS; i++) {
  		client_info[i].buf_FIFO = malloc(fifo_queue_size * sizeof(char*));
  		client_info[i].min_FIFO = 0;
  		client_info[i].max_FIFO = 0;
  		if (fifo_high > 0)
  			client_info[i].buf_state = FILLING;  		
  		else client_info[i].buf_state = ACTIVE;  		
  	}
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
	struct event_base *base;

  init_clients();

  base = event_base_new();
  if(!base) syserr("Error creating base.");

  evutil_socket_t listener_socket;
  listener_socket = socket(PF_INET, SOCK_STREAM, 0);
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

  struct event *listener_socket_event = 
    event_new(base, listener_socket, EV_READ|EV_PERSIST, listener_socket_cb, (void *)base);
  if(!listener_socket_event) syserr("Error creating event for a listener socket.");

  if(event_add(listener_socket_event, NULL) == -1) syserr("Error adding listener_socket event.");



pid_t pid;

switch (pid = fork()) {
	case -1:
        syserr("fork()");
	case 0: 
        //jestem w dziecku
        //ono dalej niech sie zajmuje obsluga TCP
		if (DEBUG) {
	    	printf("[PID: %d] Jestem procesem potomnym, to ja zajme sie obsluga TCP\n",getpid());
		}

		printf("Entering dispatch loop.\n");
		if(event_base_dispatch(base) == -1) syserr("Error running dispatch loop.");
		printf("Dispatch loop finished.\n");

		event_free(listener_socket_event);
		event_base_free(base);

		if (DEBUG) { 
			printf("[PID: %d] Jestem procesem potomnym, i zaraz sie skoncze\n",getpid());
        }
        exit(0);
	default:
        break;        
}

if (DEBUG) {
	printf("[PID: %d] Jestem procesem macierzystym, to ja zajme sie budowa i obsluga UDP\n",getpid());
}


// </LIBEVENT


// na razie dla ipv4 i na poll()
// UDP

	struct sockaddr_in server;
	struct sockaddr_in client_udp;
	char buf[BUF_SIZE];
	ssize_t rval;
	int msgsock, active_clients, i, ret;
	int changes;

	socklen_t rcva_len;
    ssize_t len_udp;


  	initiate_client();
  	active_clients = 0;


  	//create_main_socket();
  			//lub 
	//creating IPv4 UDP socket
	int sock_udp;
    sock_udp = socket(AF_INET, SOCK_DGRAM, 0); 
    if (sock_udp < 0)
        syserr("socket");  	




	server.sin_family = AF_INET; 
  	server.sin_addr.s_addr = htonl(INADDR_ANY); //we listen on all interfaces
  	server.sin_port = htons(port_num); //port num podany na wejsciu  	

 	/* Bindowanie centralnego gniazda do swojego adresu */
/*  	if (bind(client[0].fd, (struct sockaddr*)&server,
           (socklen_t)sizeof(server)) < 0) {
    	syserr("Binding stream socket");
  	}
*/	
  	//lub

  	//bind the socket to my address
    if (bind(sock_udp, (struct sockaddr *) &server,
      (socklen_t) sizeof(server)) < 0)
        syserr("bind");




  	if (DEBUG) {
  		printf("UDP : Server.sin_addr.s_addr: %hu\n", server.sin_addr.s_addr);
  		printf("UDP : Accepting on port: %hu\n",ntohs(server.sin_port));
  	}

  	/* Tryb nasluchiwania */
/*  	if (listen(client[0].fd, QUEUE_LENGTH) == -1) {
    	syserr("Starting to listen");
  	}
*/

switch (pid = fork()) {
	case -1:
        syserr("fork()");
	case 0: 
        //jestem w dziecku
        //ono dalej niech sie zajmuje obsluga TCP
		if (DEBUG) {
	    	printf("[PID: %d] Jestem kolejnym procesem potomnym, to ja zajme sie miksowaniem i przesylaniem datagramu wyjsciowego\n",getpid());
		}
		//czekam 500 ms 
		while (1) {
			struct timespec tim, tim2;
   			tim.tv_sec = 15; //powinno byc 0
   			tim.tv_nsec = 500000000L; //0.5 s
			nanosleep(&tim, &tim2);

			//changes = poll(client, _POSIX_OPEN_MAX, 100*interval);	
			//nzl od tego, czy sa zmiany na deskryptorach, czy nie
			printf("Miksuje wszystkie dane\n");
			printf("Wysylam je w petli do wszytskich kleintow\n");	
				//miksuj wszytskie dane
				//wyslij
		}	
        exit(0);
	default:
        break;        
}

if (DEBUG) {
	printf("[PID: %d] Jestem dalej procesem macierzystym, bede odbierac po UDP i zajmowac sie klientami\n",getpid());
}


    rcva_len = (ssize_t) sizeof(server);
    int flags = 0; 
    ssize_t len;

	for (;;) {
		do {
			flags = 0; // we do not request anything special
			len = recvfrom(sock_udp, buf, sizeof(buf), flags,
					(struct sockaddr *) &client_udp, &rcva_len); //rcva_len - to się zawsze na wszelki wypadek inicjuje
			printf("RECVFROM\n");
			if (len < 0)
				syserr("error on datagram from client socket");
			else {
				(void) printf("read from [%s:%d]: %zd bytes: %.*s\n", inet_ntoa(client_udp.sin_addr), ntohs(client_udp.sin_port), len,
						(int) len, buf); //*s oznacza odczytaj z buffer tyle bajtów ile jest podanych w (int) len (do oczytywania stringow, ktore nie sa zakonczona znakiem konca 0
						
			}
		} while (len > 0); //dlugosc 0 jest ciezko uzyskac
		(void) printf("finished exchange\n");
	}

/*
    
    while (1) {
        len_udp = recvfrom(sock_udp, &num_of_octets, sizeof(uint32_t), flags,
                    (struct sockaddr *) &client_address_udp, &rcva_len);
        if (len_udp < 0) 
            syserr("read from udp");
        //checking IP adress of the sender
        if (client_address_udp.sin_addr.s_addr == 
            ((struct sockaddr_in*) (addr_result->ai_addr))->sin_addr.s_addr) {
            received = 1;            
        }
    }

*/













 	/* Do pracy */
 /* 	do {
	  	//za kazdym razem msuimy to zrobic
    	for (i = 0; i < _POSIX_OPEN_MAX; ++i)
      		client[i].revents = 0;
	    	if (finish == TRUE && client[0].fd >= 0) {
	      		if (close(client[0].fd) < 0)
	        		perror("close");  //zamykamy gniazdo, jesli jest FINISH!!!
	      	client[0].fd = -1;
    	}
*/
	    /* Czekamy przez 5000 ms */
/*	    ret = poll(client, _POSIX_OPEN_MAX, 5000);
	    if (ret < 0)
	      perror("poll");
	    else if (ret > 0) {
	      
/*
	      if (finish == FALSE && (client[0].revents & POLLIN)) {
	        msgsock =
	          accept(client[0].fd, (struct sockaddr*)0, (socklen_t*)0);
	        if (msgsock == -1)
	          perror("accept");
	        else {
				//przechodzimy cala tablice, szukamy wolnego gniazda. w[osujemy aktywne gniazdo tam, i zwikeszamy liczbe klientow
	          for (i = 1; i < _POSIX_OPEN_MAX; ++i) {
	            if (client[i].fd == -1) {
	              client[i].fd = msgsock;
	              active_clients += 1;
	              break;
	            }
	          }
	          //jak juz jest przepellnione, to zamykamy gniazdo
	          //zakmniecie poaczenia praktycznie
	          if (i >= _POSIX_OPEN_MAX) {
	            fprintf(stderr, "Too many clients\n");
	            if (close(msgsock) < 0)
	              perror("close");
	          }
	        }
	      } */



	      //potem sprawdzamy (jednoczesnie moga zajsc, nie po elsie ;p)
/*	      for (i = 1; i < _POSIX_OPEN_MAX; ++i) {
	        if (client[i].fd != -1
	            && (client[i].revents & (POLLIN | POLLERR))) {
	          rval = read(client[i].fd, buf, BUF_SIZE);
	          if (rval < 0) {
	            perror("Reading stream message");
				// i zamykamy gniazdo
	            if (close(client[i].fd) < 0)
	              perror("close");
	            client[i].fd = -1;
	            active_clients -= 1;
	          }
	          else if (rval == 0) {
	            //kleint sie rozlaczyl
				fprintf(stderr, "Ending connection\n");
				//tez mu zamykamy despkryptory itp.
	            if (close(client[i].fd) < 0)
	              perror("close");
	            client[i].fd = -1;
	            active_clients -= 1;
	          }
	          else
	            printf("-->%.*s\n", (int)rval, buf);
	        }
	      }
	    }
	    else
	      fprintf(stderr, "Do something else\n");
  	} while (finish == FALSE || active_clients > 0);

  if (client[0].fd >= 0)
    if (close(client[0].fd) < 0)
      perror("Closing main socket");
  exit(EXIT_SUCCESS);
}
*/
	return 0;
}