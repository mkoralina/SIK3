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

    printf("MIXER\n");

    /* FAKTYCZNA TRESC MIKSERA : (mozliwe, ze do poprawy, jak zrozumiem, co z tymi danymi)

    int16_t num;
    int16_t sum;

    int target_size = 176 * tx_interval_ms;

    int i;
    int j;

    //uzupelniam brakujace dane zerami    
    for (i = 0; i < n; i++) {
        for (j = strlen(inputs[i].data); j < target_size; j++) {
            memcpy(&inputs[i].data[j], "0", strlen("0"));
        }
    }

    //przesuwam sie o 2 bajty (16 bitow)
    for (j = 0; j < (target_size/2); j++) {
        sum = 0;
        for (i = 0; i < n; i++) {            
            sscanf(&inputs[i].data[2*j], "%hd", &num);
            printf("Wczytano num: %hd\n",num);
            if (num >= 0) {
                sum = min(sum + num, MAX_SHORT_INT);
            }
            else {
                sum = max(sum + num, MIN_SHORT_INT);
            }        
        }
        memcpy(&output_buf[2*j], &sum, sizeof(int16_t));
    }

    //ustalam wartosci consumed 
    for (i = 0; i < n; i++) {
        inputs[i].consumed = min (strlen(inputs[i].data), target_size); 
    }

    printf("output_buf w mixerze: %s\n",output_buf);

    *output_size  = strlen(output_buf); 

    */

    /* ATRAPA: jako output wypluwa input 1. klienta, nic nie miksuje */
    //printf("size od output_buf: %zu\n", sizeof(output_buf));
    //memset(output_buf, 0, sizeof(output_buf));
    memcpy(&output_buf[0], inputs[0].data, strlen(inputs[0].data));
    *output_size = strlen(output_buf);

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