/* 
  Monika Konopka, 334666
  Data: 13.05.2014r. 
*/


// zrobić selekcje tego tutaj, co potrzeba tylko
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>  

#define MAX_SHORT_INT 32767
#define MIN_SHORT_INT -32768

#ifndef max
#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#ifndef min
#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )
#endif 


struct mixer_input {
  void* data;       // Wskaźnik na dane w FIFO
  size_t len;       // Liczba dostępnych bajtów
  size_t consumed;  // Wartość ustawiana przez mikser, wskazująca, ile bajtów należy
                    // usunąć z FIFO.
}; 



//TODO
void mixer(struct mixer_input* inputs, size_t n, void* output_buf,                      
    size_t* output_size, unsigned long tx_interval_ms) {
    // n - liczba aktywnych klientow

   // printf("MIXER\n");

    // FAKTYCZNA TRESC MIKSERA : 

    int16_t num;
    int16_t sum;

    int16_t * int_input[n];

    int target_size = 176 * tx_interval_ms; //jeszcze sprawdzic, czy to nie jest wieksze od fifo_queue_size w ogóle
    //TODO: faktycznie, żeby robil te wielkosc, bo teraz nie robi jak jest za malo danych:/ (inna rzecz, ze nie powinno byc raczej)

    int i;
    long long int j;



    //inicjalizacja na zera?
    //for (i = 0; i < n; i++) {
    //    int_input[i] = {0};
    //}


    for (i = 0; i < n; i++) {
        //tutaj musze alokowac pamiec? jesli to jest tylko castowanie? to ma wskaxnik chyba tylko, nie?
        int_input[i] = (int16_t *) inputs[i].data;
    }

    int16_t * int_output_buf = (int16_t *) output_buf;

    for (j = 0; j < target_size/2; j++) {
        sum = 0;
        for (i = 0; i < n; i++) { 
            num = int_input[i][j];           
            if (num) { //TODO: nie wiem, cz to dziala
                if (num >= 0) 
                    sum = min(sum + num, MAX_SHORT_INT);                
                else 
                    sum = max(sum + num, MIN_SHORT_INT);                        
            }
        }
        int_output_buf[j] = sum;
    }

    //ustalam wartosci consumed 
    for (i = 0; i < n; i++) {
        inputs[i].consumed = min (strlen(inputs[i].data), target_size); 
    }



    //printf("output_buf w mixerze: %s\n",output_buf);

    *output_size  = strlen((char *)output_buf); 
}


/* BRUDNOPIS:

    //char * output = (char *) output_buf;

    //char * tmp = "moj nowy tekst";
    //memcpy(&output_buf[0], tmp, strlen(tmp));

    //printf("POINTERS in MIXER:\n");
    //printf("output_buf: %p\n",output_buf);
    //printf("inputs[1].data: %p\n",inputs[1].data);
    //printf("inputs[0].data: %p\n",inputs[0].data);
    //output_buf = inputs[0].data;

    /*
    printf("inputs[0].data w mixerze przed: %s\n",inputs[0].data);

    char * nowe = (char *) inputs[0].data;
    nowe = "babcia";
    memcpy((char*)inputs[0].data, "babcia", strlen("babcia"));
    
    printf("inputs[0].data w mixerze po: %s\n",inputs[0].data);    
    */

    //printf("Po zerach inputs[0] = %s\n", (char*)inputs[0].data);
    //printf("strlen(0): %d\n",strlen("0"));

    //output = inputs[0].data;
    //printf("output w mixerze: %s\n", output);




    //int num_of_nums = target_size/sizeof(num); 

    //void * pom = inputs[0].data;
    //inputs[0].data = inputs[1].data;
    //inputs[1].data = "ocniczy";

    //atrapa:
    //output = inputs[0].data;
    //printf("outpus w mixerze: %s\n", output);

    //char * nowe = (char *) inputs[0].data;
    //memset(inputs[0].data, 0, strlen((char*)inputs[0].data));
    //nowe = "babcia";
    //memcpy((char*)inputs[0].data, "babcia", strlen("babcia"));
    //printf("BABCIA\n");
    //printf("Po babci, inputs[0] = %s\n", (char*)inputs[0].data);



   //   
    /*    //TODO:
        int j;
        for (j = 0; j < n; j++) {
            //jesli jest ta liczba, to
            //value += inputs[j][i]; //przy czym to ma byc BINARNIE ! wiec moze jakies binarne dodawanie
            //jesli nie ma, to jest 0, czyli nie wplywa na sume
        }
        output[i] = "a";
    */
   // }
    //output_buf = (void *) output;
    //printf("output_buf w mixerze: %s\n", (char *) output_buf);
     //ALE JESZCZE SA PRZENIESIENIA, NO GENERALNIE TO JEST XLE NAPISANE