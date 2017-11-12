#ifndef CLICK_IPCLIENT_HH
#define CLICK_IPCLIENT_HH
#define MAXNODE 100
#include <click/element.hh>
#include <click/timer.hh>
#include <vector>
#include "IPPacket.hh"
CLICK_DECLS

class IPClient : public Element {
    public:
        IPClient();
        ~IPClient();
        const char *class_name() const { return "IPClient";}
        const char *port_count() const { return "1-/1-";}
        const char *processing() const { return PUSH; }

        int configure(Vector<String> &conf, ErrorHandler *errh);
        void push(int, Packet*);
        void broadcast(Packet *);
        void sendout(Packet *);
        void run_timer(Timer*);
        void calPath();
        WritablePacket* makeIP(int type,int src,int dst,int seq,int size,int data[100],TCPheader);
        int initialize(ErrorHandler*);

    private:
        int G[MAXNODE][MAXNODE];
        std::vector<Link> LinkList;
        int d[MAXNODE],pre[MAXNODE],inq[MAXNODE],route[MAXNODE];
        int ipToPort[MAXNODE];
        Timer _timer;
        int _myIP;
        int seq;
};

CLICK_ENDDECLS
#endif
