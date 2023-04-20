#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>
#include <errno.h> // exceptions

#include "common.h"
#include "packet.h"


/*
 * You are required to change the implementation to support
 * window size greater than one.
 * In the current implementation the window size is one, hence we have
 * only one send and receive packet
 */
tcp_packet *recvpkt;
tcp_packet *sndpkt;

// make a new packet_list to store buffered packets
packet_list * head = NULL;
packet_list * tail = NULL;

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    /* 
     * check command line arguments 
     */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp  = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    // setting socket timeout option to a 10-second timeout for receiving data
    struct timeval recv_timeout;
    recv_timeout.tv_sec = 10;
    // recv_timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, 
        &recv_timeout, sizeof(recv_timeout));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
                sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");
    

    clientlen = sizeof(clientaddr);


    int curr_ackno = 0;    
   

    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                VLOG(INFO, "Timeout occurred, closing receiver");
                fclose(fp);
                close(sockfd);
                exit(0);
            } else {
                error("ERROR in recvfrom");
            }
        }
        
        recvpkt = (tcp_packet *) buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        /* 
         * sendto: ACK back to the client 
         */
        gettimeofday(&tp, NULL);
        VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

        if (recvpkt->hdr.data_size == 0) {
            VLOG(INFO, "End Of File has been reached");
            fclose(fp);
            break;
        }

        // cases:
        // 1. seqno is what we expect - send ack and write to file and increment curr_ackno with data_size
        // 2. seqno is not what we expect - send repeated ack of last received packet in order i.e. curr_ackno
        //    store the packet in window buffer if it is not already there, and if there is space in the buffer
       
        if (recvpkt->hdr.seqno == curr_ackno) {
            //VLOG(DEBUG, "written");
            //write to file at appropriate location
            fseek(fp,0, SEEK_SET);
            fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
            //printf("%s \n\n",recvpkt->data);
            //printf("%d \n",recvpkt->hdr.seqno);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
            curr_ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;

            packet_list * curr = head;
            int found = 0;
            while (curr != NULL) {
                //if (curr->val->hdr.seqno == curr_ackno) {
                    //write to file at appropriate location
//                    fseek(fp,0, SEEK_SET);
//                    fseek(fp, curr->val->hdr.seqno, SEEK_SET);
//                    fwrite(curr->val->data, 1, curr->val->hdr.data_size, fp);
//                    printf("curr->val->hdr.seqno: %d\n", curr->val->hdr.seqno );
//                    printf("curr->val->hdr.data_size: %d\n", curr->val->hdr.data_size);
//                    printf("curracko: %d\n", curr_ackno);
//                    curr_ackno = curr->val->hdr.seqno + curr->val->hdr.data_size;
                if (curr->val->hdr.seqno == curr_ackno)
                {
                    fseek(fp,0, SEEK_SET);
                    fseek(fp, curr->val->hdr.seqno, SEEK_SET);
                    fwrite(curr->val->data, 1, curr->val->hdr.data_size, fp);
                    curr_ackno = curr->val->hdr.seqno + curr->val->hdr.data_size;
                   curr = head;
                    //found = 1;
                }
                
                else
                    curr = head->next;
              
                // pop(&head);
               
            }

            // send cumulative ack
            sndpkt = make_packet(0);
            sndpkt->hdr.ackno = curr_ackno;
            sndpkt->hdr.ctr_flags = ACK;
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                    (struct sockaddr *) &clientaddr, clientlen) < 0) {
                error("ERROR in sendto");
            }
            printf("Ack sent: %d\n", sndpkt->hdr.ackno);
        }

        else {
            //VLOG(DEBUG, "huh");
            // send repeated ack
            sndpkt = make_packet(0);
            sndpkt->hdr.ackno = curr_ackno;
            sndpkt->hdr.ctr_flags = ACK;
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                    (struct sockaddr *) &clientaddr, clientlen) < 0) {
                error("ERROR in sendto");
            }
            printf("Ack sent: %d\n", sndpkt->hdr.ackno);

            // store packet in buffer
            if (recvpkt->hdr.seqno > curr_ackno) { // ignore packets that are already received
                push(&head, &tail, recvpkt);
                printf("buffered: %d\n", recvpkt->hdr.seqno);
            }

        }

    }

    return 0;
}
