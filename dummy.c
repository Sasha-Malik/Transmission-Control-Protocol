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
int window_size = 10;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

int duplicateACK = 0; // counter to check duplicate ACKS


// for RTO calculation
int rtt_running = 0;
struct timeval start, end;
int rto = 3; // rto = estrtt + 4 * devrtt
double rtt = 0; // rtt = end - start
double estrtt = 0; // estrtt = (1 - alpha) * estrtt + alpha * rtt
double devrtt = 0; // devrtt = (1 - beta) * devrtt + beta * |rtt - estrtt|
double alpha = 0.125;
double beta = 0.25;

void exp_backoff();

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend the smallest packet
        sndpkt = head->val;

        VLOG(INFO, "Timout happend");
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }

        exp_backoff();
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


void calc_rto()
{
    rtt = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
    estrtt = (1 - alpha) * estrtt + alpha * rtt;
    devrtt = (1 - beta) * devrtt + beta * abs(rtt - estrtt);
    // rto = floor(estrtt + 4 * devrtt); 
    rto = (int) (estrtt + 4 * devrtt);

    // rto should be at least 1ms - in case floor ret 0
    if (rto < 1)
        rto = 1;

    VLOG(INFO, "rtt: %f, estrtt: %f, devrtt: %f, rto: %d", rtt, estrtt, devrtt, rto);
    init_timer(rto, resend_packets);
}

void exp_backoff()
{
    if (rto < 240)
        rto *= 2;
    else
        rto = 240;
    init_timer(rto, resend_packets);
}


int main (int argc, char **argv)
{
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

    //printf("num_packs: %d\n", num_packs);
    //printf("size: %d\n", size);
    
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
    
    //sending first 10 packets
    int i = 10;

    if (num_packs < 10) {
        i = num_packs;
    }

    // int set_timer = 0;

    // incase there are less tahn 10 in total
    while(i > 0)
    {
        sndpkt = packArr[counter];
        push(&head, &tail, sndpkt); //pushing to the list
        counter++;

        send_base = sndpkt->hdr.seqno;
        VLOG(DEBUG, "Sending packet %d to %s",
                send_base, inet_ntoa(serveraddr.sin_addr));
        
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
        
        //keeping timer for the lowest packet
        if(!rtt_running)
        {
            init_timer(rto, resend_packets); // initialize with rto
            start_timer();
            // set_timer = 1;

            rtt_running = 1;
            // record time for rtt
            gettimeofday(&start, NULL);

        }
        
        i--;
    }
    
    //file is empty
    if(head == NULL){
        return 0;
    }
    
//   packet_list* cur = head;
//   while(cur !=NULL)
//   {
//       printf("%d ",cur->val->hdr.seqno);
//       cur = cur->next;
//   }
//   printf("\n");
    
    send_base = 0; //nothing has been recieved
    next_seqno += head->val->hdr.data_size; //the first ack will be

    while (1)
    {
        
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
                VLOG(DEBUG, "Sending packet %d to %s", 0, inet_ntoa(serveraddr.sin_addr));
                break;
            }
        
  

            // stop timer if the acked packet includes the lowest
            if (recvpkt->hdr.ackno > head->val->hdr.seqno) {
                stop_timer();
                gettimeofday(&end, NULL);                
                calc_rto();
                start_timer(); //starting timer for the new lowest
                gettimeofday(&start, NULL);
                
                duplicateACK = 0;
            }
        
            else{
                duplicateACK++;
            }
        
        
            // 3 duplicate acks
           if(duplicateACK == 3)
           {
                // printf("DUPLICATE\n");
                stop_timer();
                gettimeofday(&end, NULL);

                sndpkt = head->val;

                VLOG(INFO, "Timeout happend");
                if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }

                start_timer();
                gettimeofday(&start, NULL);

                duplicateACK = 0;
           }
      
           
            int new_packets_no = 0;
       
            //popping the acked packets from the pack list
            while(recvpkt->hdr.ackno > head->val->hdr.seqno)
            {
                pop(&head, &tail);
                new_packets_no++;
                if (head == NULL) {
                    break;
                }
            }
       
        
            //filling the packet list with new packets(same number of acked packets) and sending them

            while(new_packets_no > 0)
            {
                if(counter < num_packs)
                {
                //printf("counter234: %d\n", counter);
                sndpkt = packArr[counter];
                push(&head, &tail, sndpkt); //pushing to the list
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
