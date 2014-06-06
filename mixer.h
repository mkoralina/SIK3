/* 
  Monika Konopka, 334666
  Data: 13.05.2014r. 
*/

#ifndef MIXER
#define MIXER

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

//miksuje dane od klientow z aktywnymi kolejkami
void mixer(struct mixer_input* inputs, size_t n, void* output_buf,                      
    size_t* output_size, unsigned long tx_interval_ms) {
    
    memset(output_buf, 0, *output_size);

    int target = 176 * tx_interval_ms;

    *output_size = min(*output_size, target);

    int16_t * int_input[n];

    int i;
    for(i = 0; i < n; i++) {
        inputs[i].consumed = min(inputs[i].len, target);
        int_input[i] = (int16_t *) ((void*) inputs[i].data);
    }   
    
    int16_t * int_output = (int16_t *) output_buf;

    int16_t num;
    int16_t sum;
    int j;
    
    for (j = 0; j < *output_size; j++) {
        int_output[j] = 0;
        for(i = 0; i < n; i++) {            
            if (inputs[i].len > j) {//na sztywno bardzo TODO
                num = int_input[i][j];  
                if (num >= 0) 
                    int_output[j] = min(int_output[j] + num, MAX_SHORT_INT);                
                else 
                    int_output[j] = max(int_output[j] + num, MIN_SHORT_INT);                        
                
            }
        }                
    }        

}

#endif 
   