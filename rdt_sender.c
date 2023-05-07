        #include <stdio.h>
        #include <unistd.h>
        #include <stdlib.h>
        #include <string.h>
        #include <sys/types.h>
        #include <sys/socket.h>
        #include <netinet/in.h>
        #include <arpa/inet.h>
        #include <signal.h>
        #include <sys/time.h>
        #include <time.h>
        #include <assert.h>
        #include <math.h>
        // #include <stdbool.h>
        // for SIG

        #include"packet.h"
        #include"common.h"

        #define STDIN_FD    0
        #define RETRY  120 //millisecond


        packet_list * head = NULL;
        packet_list * tail = NULL;

        int next_seqno=0;
        int send_base=0;
        float cwnd = 1;
        int ssthresh = 64;
        int list_size = 0;

        int sockfd, serverlen;
        struct sockaddr_in serveraddr;
        struct itimerval timer;
        tcp_packet *sndpkt;
        tcp_packet *recvpkt;
        sigset_t sigmask;

        int duplicateACK = 0; // counter to check duplicate ACKS


        // for RTO calculation
        int retransmitted = 0;
        struct timeval start, end;
        int rto = 3000; // rto = estrtt + 4 * devrtt
        int maxRTO = 5000;
        double rtt = 0; // rtt = end - start
        double estrtt = 0; // estrtt = (1 - alpha) * estrtt + alpha * rtt
        double devrtt = 0; // devrtt = (1 - beta) * devrtt + beta * |rtt - estrtt|
        double alpha = 0.125;
        double beta = 0.25;

        FILE *csv;

        void exp_backoff();

        struct timeval time_init;
        float timedifference_msec(struct timeval t0, struct timeval t1)
        {
            return fabs((t1.tv_sec - t0.tv_sec) * 1000.0f + (t1.tv_usec - t0.tv_usec) / 1000.0f);
        }

        void writeCSV(){
            csv = fopen("CWND.csv", "a");
            if (csv == NULL){
                printf("Error opening csv\n");  
            }
            
            struct timeval t1;
            gettimeofday(&t1, 0);
            fprintf(csv, "%f,%f,%d\n", timedifference_msec(time_init, t1), cwnd, ssthresh);
        }

        void resend_packets(int sig)
        {
            if (sig == SIGALRM)
            {
                //Resend the smallest packet
                sndpkt = head->val;

                retransmitted = 1;

                VLOG(INFO, "Timout happend");
                if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
                exp_backoff();
                
                //if we are in slow start phase
                if(cwnd < ssthresh)
                {
                    ssthresh = ( (int)cwnd/2 > 2 ? (int)cwnd/2 : 2);
                    //writing to csv
                    writeCSV();
                }
            }
        }


        void start_timer()
        {
            sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
            setitimer(ITIMER_REAL, &timer, NULL);
        }


        void stop_timer()
        {
            sigprocmask(SIG_BLOCK, &sigmask, NULL);
        }


        /*
         * init_timer: Initialize timer
         * delay: delay in milliseconds
         * sig_handler: signal handler function for re-sending unACKed packets
         */
        void init_timer(int delay, void (*sig_handler)(int))
        {
            signal(SIGALRM, sig_handler);
            timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
            timer.it_interval.tv_usec = (delay % 1000) * 1000;
            timer.it_value.tv_sec = delay / 1000;       // sets an initial value
            timer.it_value.tv_usec = (delay % 1000) * 1000;

            sigemptyset(&sigmask);
            sigaddset(&sigmask, SIGALRM);
        }

        //calculate rto
        void calc_rto()
        {
            rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
            estrtt = (1 - alpha) * estrtt + alpha * rtt;
            devrtt = (1 - beta) * devrtt + beta * abs(rtt - estrtt);
            // rto = floor(estrtt + 4 * devrtt);
            rto = (int) (estrtt + 4 * devrtt);

            // rto should be at least 1 second - in case floor ret 0
            if (rto < 3000)
                rto = 3000;
            if (rto >= maxRTO)
                rto = maxRTO;

            // VLOG(INFO, "rtt: %f, estrtt: %f, devrtt: %f, rto: %d", rtt, estrtt, devrtt, rto);
            init_timer(rto, resend_packets);
        }

        //backoff
        void exp_backoff()
        {
            rto *= 2;
            if (rto >= maxRTO)
                rto = maxRTO;
            init_timer(rto, resend_packets);
        }
        

        int main (int argc, char **argv)
        {
            //opening it to refresh it
            csv = fopen("CWND.csv", "w");
            fclose(csv);
            
            gettimeofday(&time_init, 0); // noting starting time
            
            int portno, len;
            int next_seqno = 0;
            char *hostname;
            char buffer[DATA_SIZE];
            FILE *fp;

            /* check command line arguments */
            if (argc != 4) {
                fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
                exit(0);
            }
            hostname = argv[1];
            portno = atoi(argv[2]);
            fp = fopen(argv[3], "r");
            if (fp == NULL) {
                error(argv[3]);
            }


            // size of file to determine num of packets
            fseek(fp, 0, SEEK_END);
            int size = ftell(fp);

            // reset to beg
            fseek(fp, 0, SEEK_SET);

            // making packets in array
            int num_packs = size / DATA_SIZE;

            // for ceiling after div
            if (num_packs * DATA_SIZE < size) {
                num_packs++;
            }


            //array to store all packets
            tcp_packet **packArr = malloc(num_packs * sizeof(tcp_packet *));
            len = fread(buffer, 1, DATA_SIZE, fp); // read outside - first step
            int count = 0;
            while (len > 0)
            {
                send_base = next_seqno;
                next_seqno = send_base + len;
                
                tcp_packet *pack = make_packet(len);
                memcpy(pack->data, buffer, len);
                pack->hdr.seqno = send_base;
                packArr[count] = pack;
                count++;
                len = fread(buffer, 1, DATA_SIZE, fp);
                
            }
            
            /* socket: create the socket */
            sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0)
                error("ERROR opening socket");

            /* initialize server server details */
            bzero((char *) &serveraddr, sizeof(serveraddr));
            serverlen = sizeof(serveraddr);

            /* covert host into network byte order */
            if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
                fprintf(stderr,"ERROR, invalid host %s\n", hostname);
                exit(0);
            }

            /* build the server's Internet address */
            serveraddr.sin_family = AF_INET;
            serveraddr.sin_port = htons(portno);
            assert(MSS_SIZE - TCP_HDR_SIZE > 0);

            //Stop and wait protocol
            init_timer(rto, resend_packets);
            next_seqno = 0;

            // reset to beg - FOR NOW
            fseek(fp, 0, SEEK_SET);
            int counter = 0; //keeping index of pack array
            
            //sending first 1 packet
            int i = (int)cwnd;
            int set_timer = 0;

            // incase there are less tahn 10 in total
            while(i > 0)
            {
                sndpkt = packArr[counter];
                push(&head, &tail, sndpkt); //pushing to the list
                list_size++;
                counter++;

                send_base = sndpkt->hdr.seqno;
                VLOG(DEBUG, "Sending packet %d to %s",
                        send_base, inet_ntoa(serveraddr.sin_addr));
                
                if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0){
                    error("sendto");}
                
                //keeping timer for the lowest packet
                if(!set_timer){
                    // init_timer(rto, resend_packets); // initialize with rto
                    start_timer();
                    gettimeofday(&start, NULL);
                    set_timer = 1;
                    // record time for rtt
                }
                i--;
            }
            
            //file is empty
            if(head == NULL){
                return 0;
            }
            
            send_base = 0; //nothing has been recieved
            next_seqno += head->val->hdr.data_size; //the first ack will be

            while (1)
            {
                
                    //receiving acks
                    if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                                (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                    {
                        error("recvfrom");
                    }
                    recvpkt = (tcp_packet *)buffer;
                    assert(get_data_size(recvpkt) <= DATA_SIZE);


                    //end of file empty packet
                    if (recvpkt->hdr.ackno == size) {
                        printf("Done\n");
                        sndpkt = make_packet(0);
                        sndpkt->hdr.seqno = size;
                        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                        {
                            error("sendto");
                        }
                        break;
                    }
                
                    //congestion control
                
                    if (cwnd < ssthresh)
                        /* Slow Start*/
                        cwnd = cwnd + 1;
                    else
                        /* Congestion Avoidance */
                        cwnd = cwnd + 1/cwnd;
                
                    //writing to csv
                    writeCSV();
          
                    // stop timer if the acked packet includes the lowest
                    if (recvpkt->hdr.ackno > head->val->hdr.seqno) {

                        if (!retransmitted) {
                            stop_timer();
                            gettimeofday(&end, NULL);
                            calc_rto();
                            start_timer(); //starting timer for the new lowest
                            gettimeofday(&start, NULL);
                            
                            duplicateACK = 0;
                        }
                        else {
                            retransmitted = 0;
                            stop_timer();
                            gettimeofday(&end, NULL);
                            start_timer(); //starting timer for the new lowest
                            gettimeofday(&start, NULL);
                            
                            duplicateACK = 0;
                        }
                        
                    }
                
                    else{
                        duplicateACK++;
                        retransmitted = 1;
                        
                        //if we are in slow start phase and a packet is lost
                        if(cwnd < ssthresh)
                        {
                            ssthresh = ( (int)cwnd/2 > 2 ? (int)cwnd/2 : 2);
                            //writing to csv
                            writeCSV();
                        }
                    }
                
                    //fast recovery
                    // 3 duplicate acks
                   if(duplicateACK == 3)
                   {
                        sndpkt = head->val;

                        VLOG(INFO, "Timeout happend");
                        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                        {
                            error("sendto");
                        }
                       
                        duplicateACK = 0;
                       
                       //fast recovery
                       ssthresh = ( (int)cwnd/2 > 2 ? (int)cwnd/2 : 2);
                       //writing to csv
                       writeCSV();
                       
                       //deleting list
                       while(list_size != 0)
                       {
                           pop(&head, &tail);
                           list_size--;
                       }
                       
                       //list size is 1 now
                       list_size = 1;
                       push(&head, &tail, sndpkt); //pushing to the list
                       cwnd = 1;
                       //writing to csv
                       writeCSV();
                    
                   }
              
                   
                    int new_packets_no = 0;
               
                    //popping the acked packets from the pack list
                    while(recvpkt->hdr.ackno > head->val->hdr.seqno)
                    {
                        pop(&head, &tail);
                        list_size--;
                        if (head == NULL) {
                            break;
                        }
                    }
               
                    new_packets_no = (int)cwnd - list_size;
                
                    //filling the packet list with new packets(same number of acked packets) and sending them
                    while(new_packets_no > 0)
                    {
                        if(counter < num_packs)
                        {
                        
                            sndpkt = packArr[counter];
                            push(&head, &tail, sndpkt); //pushing to the list
                            list_size++;
                            counter++;
                                                
                            send_base = sndpkt->hdr.seqno;
                            
                            VLOG(DEBUG, "Sending packet %d to %s",
                                    send_base, inet_ntoa(serveraddr.sin_addr));
                            
                            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                            {
                                error("sendto");
                            }
                            
                            new_packets_no--;
                        }
                            
                        else
                            break;
                    }

            }

            return 0;

        }
