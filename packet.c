#include <stdlib.h>
#include"packet.h"

static tcp_packet zero_packet = {.hdr={0}};
/*
 * create TCP packet with header and space for data of size len
 */
tcp_packet* make_packet(int len)
{
    tcp_packet *pkt;
    pkt = malloc(TCP_HDR_SIZE + len);

    *pkt = zero_packet;
    pkt->hdr.data_size = len;
    return pkt;
}

int get_data_size(tcp_packet *pkt)
{
    return pkt->hdr.data_size;
}


void push(packet_list ** head, packet_list ** tail, tcp_packet * val) {
    
    packet_list * new_node = (packet_list *) malloc(sizeof(packet_list));
    new_node->val = val;
    new_node->next = NULL;

    if (*tail == NULL) {
        *head = new_node;
        *tail = new_node;
        return;
    }
    else {
        (*tail)->next = new_node;
        *tail = new_node;
    }  
}


void pop(packet_list ** head) {
    *head = (*head)->next;
}

packet_list* popCurrent(packet_list ** head, packet_list ** tail, packet_list ** current) {
    if(*head == NULL)
        return NULL;

    packet_list *next_elem = NULL;
    
    if (*current == *head) {
        next_elem = (*head)->next;
        pop(head);
    }
    else {
        packet_list * temp = *head;
        while (temp->next != *current) {
            temp = temp->next;
        }
        next_elem = (*current)->next;
        temp->next = (*current)->next;
    }

    return next_elem;
}
