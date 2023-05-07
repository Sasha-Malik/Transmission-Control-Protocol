enum packet_type {
    DATA,
    ACK,
};

typedef struct {
    int seqno;
    int ackno;
    int ctr_flags;
    int data_size;
}tcp_header;

#define MSS_SIZE    1500
#define UDP_HDR_SIZE    8
#define IP_HDR_SIZE    20
#define TCP_HDR_SIZE    sizeof(tcp_header)
#define DATA_SIZE   (MSS_SIZE - TCP_HDR_SIZE - UDP_HDR_SIZE - IP_HDR_SIZE)
typedef struct {
    tcp_header  hdr;
    char    data[0];
}tcp_packet;

tcp_packet* make_packet(int seq);
int get_data_size(tcp_packet *pkt);

typedef struct node {
    tcp_packet * val;
    struct node * next;
} packet_list;

void push(packet_list ** head, packet_list ** tail, tcp_packet * val);

void pop(packet_list ** head, packet_list ** tail);

packet_list* popCurrent(packet_list ** head, packet_list ** tail, packet_list ** current);
