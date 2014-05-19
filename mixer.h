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

    char * output = (char *) output_buf;
    int target_size = 176 * tx_interval_ms; 

    //jakos oszacowac ile bede musiala wziac z kolejek w takim razie, zeby po sumie wyszlo tyle 
    int per_input; //uzaleznic od n i target_size

    int i;
    //przejdz po wszytskich dostepnych inputach i sprawdz najdluzszy
    for (i = 0; i < per_input; i++) {
        //TODO:
        int j;
        int value = 0;
        for (j = 0; j < n; j++) {
            //jesli jest ta liczba, to
            //value += inputs[j][i]; //przy czym to ma byc BINARNIE ! wiec moze jakies binarne dodawanie
            //jesli nie ma, to jest 0, czyli nie wplywa na sume
        }
        output[i] = value;
    }
    *output_size  = strlen(output); 
    output_buf = (void *) output;
     //ALE JESZCZE SA PRZENIESIENIA, NO GENERALNIE TO JEST XLE NAPISANE
}