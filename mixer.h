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



void mixer(struct mixer_input* inputs, size_t n, void* output_buf,                      
    size_t* output_size, unsigned long tx_interval_ms) {
    


    memset(output_buf, 0, *output_size);

    char * input;

    int target = 176 * tx_interval_ms;

    *output_size = min(*output_size, target);

    input = inputs[0].data;

    int i;
    for(i = 0; i < n; i++) {
        inputs[i].consumed = min(inputs[i].len, target);
    }     


    int16_t * int_input;
    int_input = (int16_t *) ((void*) input);

    
    int16_t * int_output = (int16_t *) output_buf;

    int16_t num;
    int16_t sum;
    int j;

    for (j = 0; j < *output_size; j++) {
        int_output[j] = 0;
        if (inputs[0].len > j) {//na sztywno bardzo TODO
            num = int_input[j];         
            if (num != 0) { //TODO: otwierdzic, ze dziala
                if (num >= 0) 
                    int_output[j] = min(int_output[j] + num, MAX_SHORT_INT);                
                else 
                    int_output[j] = max(int_output[j] + num, MIN_SHORT_INT);                        
            }
        }
            
    }        

}



//TODO
void mixer1(struct mixer_input* inputs, size_t n, void* output_buf,                      
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
        fprintf(stderr,"Bufor do miksera jest za maly %d",*output_size);
        target_size = *output_size;
    }

    *output_size  = target_size;

    //int ile = min(inputs[0].len, 880);
    //send_data(inputs[0].data,ile);
    //return;

    //inicjalizacja na zera?
    //for (i = 0; i < n; i++) {
    //    int_input[i] = {0};
    //}


    for (i = 0; i < n; i++) {
        //tutaj musze alokowac pamiec? jesli to jest tylko castowanie? tTAK
        int_input[i] = (int16_t *) inputs[i].data;
        //fprintf(stderr, "inputs[i].data %p\n",inputs[i].data ); //ten sam adres
        //fprintf(stderr, "int_input[i] %p\n",int_input[i] ); //ten sam adres
        inputs[i].consumed = min (inputs[i].len, target_size); 
    }

    int16_t * int_output_buf = (int16_t *) output_buf;

    for (j = 0; j < target_size/2; j++) {
        sum = 0;
        for (i = 0; i < n; i++) { 
            num = int_input[i][j];  
            printf("num = %d\n",num);         
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


   