else if (sscanf(datagram, "UPLOAD %d", &nr) >= 1) {
                        if (DEBUG) printf("Zmatchowano do UPLOAD, nr = %d\n",nr);

                        if (DEBUG) printf("WG MEMCHR i odejmowania wskaznikow\n");
                        char * ptr = memchr(datagram, '\n', len);
                        int header_len = ptr - datagram + 1;
                        int data_len = len - header_len; 
                        if (DEBUG) printf("header_size = %d\n", header_len);
                        
                        //DEBUG: czy dziala przesylanie danych
                        //write(1, datagram+header_len, data_len);// tak samo jak w kliencie kiedy ma wlaczona komunikacje z serwrem
                        //write(1, data, data_len); 

                        if (client_info[clientid].buf_count == fifo_queue_size) {
                            fprintf(stderr, "client_info[clientid].buf_count %d\n",client_info[clientid].buf_count);
                            fprintf(stderr, "size, ktory przewazyl: %d\n",data_len );
                            syserr("przepelnienie bufora");
                        } 
                        client_info[clientid].buf_count += data_len;
                        memcpy(buf_FIFO[clientid] + client_info[clientid].buf_count - data_len, datagram+header_len, data_len);
                        

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
                    } 




// TAKA OPCJA TEZ DZIALA
/*
void mix_and_send(evutil_socket_t descriptor, short ev, void *arg) {
    if (activated[0]) {

        char output_buf[BUF_SIZE];
        size_t output_size = BUF_SIZE;

        char * input;

        output_size = min(output_size, 880);

        input = buf_FIFO[0];

        int16_t * int_input;
        int_input = (int16_t *) ((void*) input);

        memset(output_buf, 0, BUF_SIZE);
        int16_t * int_output = (int16_t *) output_buf;

        int16_t num;
        int16_t sum;
        int j;

        for (j = 0; j < output_size; j++) {
            if (client_info[0].buf_count > j) 
                num = int_input[j];         
            if (num != 0) { //TODO: otwierdzic, ze dziala
                if (num >= 0) 
                    int_output[j] = min(int_output[j] + num, MAX_SHORT_INT);                
                else 
                    int_output[j] = max(int_output[j] + num, MIN_SHORT_INT);                        
            }
        }        
    

        int ile = min(880, client_info[0].buf_count);
        send_DATA_datagram(int_output, last_nr, client_info[0].ack, fifo_queue_size - client_info[0].buf_count, 0, ile);
        last_nr++;

        //update bufora
        memmove(buf_FIFO[0], &buf_FIFO[0][ile], client_info[0].buf_count - ile);            
        client_info[0].buf_count -= ile;
        //if (DEBUG) printf("client_info[%d].buf_count: %li\n", i,client_info[i].buf_count);
        update_min_max(0, client_info[0].buf_count);
    }    
} 
*/











void match_and_execute(char *datagram, int clientid, int len) {
    if (DEBUG) printf("match_and_execute: %s\n",datagram);

    int nr;    
    char data[BUF_SIZE+1] = { 0 };
    if (sscanf(datagram, "UPLOAD %d %[^\n]", &nr, data) >= 1) {
        if (DEBUG) printf("Zmatchowano do UPLOAD, nr = %d, dane = %s\n",nr, data);

        int num = nr;
        if (!nr) num = 1; //na wypadek gdyby nr = 0 -> log10(0) -> blad szyny
        int nr_size = (int) ((floor(log10(num))+1)*sizeof(char));
        int header_size = strlen("UPLOAD") + nr_size + 2;// upload + spacja + nr + \n
        int size = len - header_size;

        //DEBUG: sprawdzam, czy dobrze dochodza dane do serwera od klienta
        //fprintf(stdout, "%*s\n",size,data );
        write(1,data,size); //DZIALA Z LEKKIMI TRZASKAMI, 100 - gra szybciej, 200 wolniej
        //write(2,data, 150); //wypisuje robaczki jak powinno
        fprintf(stderr, "len - header_size: %d\n",len - header_size); //

        
        if (client_info[clientid].buf_count == fifo_queue_size) {
            fprintf(stderr, "client_info[clientid].buf_count %d\n",client_info[clientid].buf_count);
            fprintf(stderr, "size, ktory przewazyl: %d\n",size );
            syserr("przepelnienie bufora");
        } 
        client_info[clientid].buf_count += size;
        memcpy(buf_FIFO[clientid] + client_info[clientid].buf_count - size, data, size);
        

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
    }
    else if (sscanf(datagram, "RETRANSMIT %d", &nr) == 1) {
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
    else if (strcmp(datagram, "KEEPALIVE\n") == 0) {
        if (DEBUG) printf("Zmatchowano do KEEPALIVE\n");
        //TODO : udpate czasu ostatniej wiadomosci od klienta
    }
    else {
        //syserr("Niewlasciwy format datagramu"); //ew. TODO: wypisuj inaczej
        if (DEBUG) printf("niewlasciwy format, datagram: %s\n",datagram);
    }
} 