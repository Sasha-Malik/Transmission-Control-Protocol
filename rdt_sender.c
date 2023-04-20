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

#include <stdbool.h>

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

    // CHANGES

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


    tcp_packet **packArr = malloc(num_packs * sizeof(tcp_packet *));

    len = fread(buffer, 1, DATA_SIZE, fp); // read outside - first step

    int count = 0;

    printf("num_packs: %d\n", num_packs);
    printf("size: %d\n", size);
    
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
    init_timer(RETRY, resend_packets);
    next_seqno = 0;

    // reset to beg - FOR NOW
    fseek(fp, 0, SEEK_SET);
    
    int counter = 0; //keeping index of pack array
    
    //sending first 10 packets
    int i = 10;

    if (num_packs < 10) {
        i = num_packs;
    }

    int set_timer = 0;

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
        if(set_timer == 0)
        {
            start_timer();
            set_timer = 1;
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

            if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                        (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;
            assert(get_data_size(recvpkt) <= DATA_SIZE);
              
            //}while(recvpkt->hdr.ackno < next_seqno);    //ignore duplicate ACKs
        
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
                start_timer(); //starting timer for the new lowest
                duplicateACK = 0;
            }
        
            else{
                duplicateACK++;
            }
        
        
            // 3 duplicate acks
//            if(duplicateACK == 3)
//            {
//                stop_timer();
//                start_timer();
//
//                sndpkt = head->val;
//
//                VLOG(INFO, "Timout happend");
//                if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
//                            ( const struct sockaddr *)&serveraddr, serverlen) < 0)
//                {
//                    error("sendto");
//                }
//            }
        
           
            int new_packets_no = 0;
            //printf("seq_no : %d \n",head->val->hdr.seqno);
            //printf("ack_no : %d \n",recvpkt->hdr.ackno);
       
            //popping the acked packets from the pack list
            while(recvpkt->hdr.ackno > head->val->hdr.seqno)
            {
                pop(&head);
                new_packets_no++;
                if (head == NULL) {
                    break;
                }
            }

            //filling the packet list with new packets and sending them
            if(count < num_packs)
            {
                while(new_packets_no > 0)
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
            }
        
            //next_seqno = recvpkt->hdr.ackno + DATA_SIZE; //next expected min ack

        //free(sndpkt);
    }

    return 0;

}