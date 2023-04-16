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


typedef struct node {
    tcp_packet * val;
    struct node * next;
} packet_list;

packet_list * head = NULL;
//head = (packet_list *) malloc(sizeof(packet_list));
//head->next = NULL;

//need to change push
void push(packet_list * head, tcp_packet * val) {
    packet_list * current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    /* now we can add a new variable */
    current->next = (packet_list *) malloc(sizeof(packet_list));
    current->next->val = val;
    current->next->next = NULL;
}

tcp_packet* pop(packet_list ** head) {
    tcp_packet* retval = NULL;
    packet_list * next_node = NULL;

    if (*head == NULL) {
        return -1;
    }

    next_node = (*head)->next;
    retval = (*head)->val;
    free(*head);
    *head = next_node;

    return retval;
}


int next_seqno=0;
int send_base=0;
int window_size = 10;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;


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
    int next_seqno;
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

    tcp_packet *packArr = malloc(num_packs * sizeof(tcp_packet));

    len = fread(buffer, 1, DATA_SIZE, fp); // read outside - first step

    int count = 0;

    printf("num_packs: %d\n", num_packs);
    printf("size: %d\n", size);

    next_seqno = 0;
    
    while (len > 0)
    {
        send_base = next_seqno;
        next_seqno = send_base + len;
        
        tcp_packet *pack = make_packet(len);
        memcpy(pack->data, buffer, len);
        pack->hdr.seqno = send_base;
        packArr[count] = *pack; // should it be pack?
        count++;
        len = fread(buffer, 1, DATA_SIZE, fp);
        
        if (len <= 0) {
            pack = make_packet(0); // to signal end of file
            packArr[count] = *pack;
            count++;
        }
        // printf("counts: %d \n", count);
    }

    // CHANGES

    // CHANGES 2

    // deal with len(0) packet in while
    // incorporate following?

    /*send_base = next_seqno;
    next_seqno = send_base + len;
    sndpkt = make_packet(len);
    memcpy(sndpkt->data, buffer, len);
    sndpkt->hdr.seqno = send_base;*/

    // CHANGES 2


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
    while(i > 0)
    {
        sndpkt = packArr[counter]; //idk
        push(head, sndpkt); //pushing to the list
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
        if(i == 10)
            start_timer();
        
        i--;
    }
    
    send_base = 0; //nothing has been recieved
    next_seqno = send_base + DATA_SIZE; //the first ack will be

    while (1)
    {
        // // window size of 10 packets from list here
        // sndpkt = make_packet(len);
        // memcpy(sndpkt->data, buffer, len);
        // sndpkt->hdr.seqno = send_base;
        //Wait for ACK
        
        //do {

            //VLOG(DEBUG, "Sending packet %d to %s",
            //        send_base, inet_ntoa(serveraddr.sin_addr));
            /*
             * If the sendto is called for the first time, the system will
             * will assign a random port number so that server can send its
             * response to the src port.
             */
            /*
            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }

            start_timer();
            //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
            //struct sockaddr *src_addr, socklen_t *addrlen);
            */

            do
            {
                if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom");
                }

                recvpkt = (tcp_packet *)buffer;
                printf("%d \n", get_data_size(recvpkt));
                assert(get_data_size(recvpkt) <= DATA_SIZE);
                
            }while(recvpkt->hdr.ackno < next_seqno);    //ignore duplicate ACKs
            stop_timer();
        
        
            //popping the acked packets from the pack list
            
            int new_packets_no = 0;
            while( head->val->hdr.seqno < recvpkt->hdr.ackno)
            {
                pop(&head);
                new_packets_no++;
            }
            //filling the packet list with new packets and sending them
            
        //if() to start new timer for the lowest pack
                
            while(new_packets_no > 0)
            {
                sndpkt = packArr[counter]; //idk
                push(head, sndpkt); //pushing to the list
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
        
            next_seqno = recvpkt->hdr.ackno + DATA_SIZE; //next expected min ack
            
            
            /*resend pack if don't recv ACK */
        //} while(recvpkt->hdr.ackno != next_seqno);

        free(sndpkt);
    }

    return 0;

}
