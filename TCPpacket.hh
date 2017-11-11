#include <click/element.hh>

struct TCPheader{
    uint16_t srcport, dstport;
    uint32_t srcip, dstip, seqnum, acknum;
    bool SYN,ACK,FIN;
    //int data[10];
};
