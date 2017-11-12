#include <click/element.hh>

struct TCPheader{
    //uint16_t srcport, dstport;
    uint32_t dstip, srcip, seqnum, acknum;
    bool SYN_TCP,ACK_TCP,FIN;
    //int data[10];
};
