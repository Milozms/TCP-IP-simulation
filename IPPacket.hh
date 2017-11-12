#define HELLO 1
#define LINK 2
#define ACK 3
#define DATA 4
#define BYE 5
#include "TCPpacket.hh"
struct Link{
  int u,v;
};

struct IPPacket{
  int type;
  int src,dst;
  int seq,size;
  int data[100];
  TCPheader tcpdata;
};
