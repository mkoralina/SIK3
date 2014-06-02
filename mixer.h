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

    fprintf(stderr, "MIXER\n");


    // FAKTYCZNA TRESC MIKSERA : 

    int16_t num;
    int16_t sum;

    int16_t * int_input[n];

    
    int i;
    long long int j;


    int target_size = 176 * tx_interval_ms; 
    if (target_size > *output_size) {
        perror("Bufor do miksera jest za maly");
        target_size = *output_size;
    }

    *output_size  = target_size;

    //DEBUG
    //fprintf(stderr, "inputs[0].data: %s\n", (char *) inputs[0].data); //tutaj sie nic nie wyswietla
    //fprintf(stderr, "inputs[0].len  = %d\n", inputs[0].len); //10560 glownie 
    //write(1,inputs[0].data,inputs[0].len); //dziala, ale mega w zwolnionym tempie i naklada sie, bo to cala dlugosc odtwarzam, a nie tylko target size 
    //target size nie dziala


    //ATRAPA 2
    //w taki sposób (nie) działa...
    /*
    char a[inputs[0].len];
    memset(a, 0 , inputs[0].len);
    size_t ile = 880;
    memcpy(a, inputs[0].data, inputs[0].len);
    fprintf(stderr, "inputs[0].len = %d\n", inputs[0].len);
    int w = write(1, &a[0], ile);
    fprintf(stderr, "WYPISANO w = %d\n",w );
    if (w < 0) {
        syserr("blad we write");
        exit(EXIT_SUCCESS);
    }
*/

    // wciaz ATRAPA - przesylam tylko dane zerowego bufora (=klienta)
    /*
    int ile_moge = min(176 * tx_interval_ms, *output_size);
    ile_moge = min(ile_moge, inputs[0].len);
    char atrapa[ile_moge+1];
    memset(atrapa, 0, ile_moge);
    memcpy(atrapa, inputs[0].data, ile_moge);
    atrapa[ile_moge] = 0;
    fprintf(stderr, "zaraz sprobuje wypisac atrape o dlugosci ile moge = %d\n",ile_moge);
    //write(1,atrapa,ile_moge+1);
    */

    //inicjalizacja na zera?
    //for (i = 0; i < n; i++) {
    //    int_input[i] = {0};
    //}


    for (i = 0; i < n; i++) {
        //tutaj musze alokowac pamiec? jesli to jest tylko castowanie? to ma wskaxnik chyba tylko, nie?
        int_input[i] = (int16_t *) inputs[i].data;
        inputs[i].consumed = min (inputs[i].len, target_size); 
    }

    int16_t * int_output_buf = (int16_t *) output_buf;

    for (j = 0; j < target_size/2; j++) {
        sum = 0;
        for (i = 0; i < n; i++) { 
            num = int_input[i][j];           
            if (num != 0) { //TODO: nie wiem, cz to dziala
                if (num >= 0) 
                    sum = min(sum + num, MAX_SHORT_INT);                
                else 
                    sum = max(sum + num, MIN_SHORT_INT);                        
            }
        }
        int_output_buf[j] = sum;
    }

    //DEBUG
    //write(1,inputs[0].data,*output_size); //tu też gra, powoli + pik. tak jak na górze
    

    //write(1, int_output_buf, *output_size);
    
    output_buf = (void*) int_output_buf;

    //write(1, output_buf, *output_size); //to tez dziala, podobnie z jakoscia jak u gory



}


void mixer1(struct mixer_input* inputs, size_t n, void* output_buf,                      
    size_t* output_size, unsigned long tx_interval_ms) {
    printf("MIXER\n");
    *output_size = 176*tx_interval_ms;
    int i;
    for (i = 0; i < n; i++) {
        inputs[i].consumed = min (inputs[i].len, 176*tx_interval_ms); 
    }
}    