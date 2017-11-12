#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/timer.hh>
#include <click/args.hh>
#include <click/packet.hh>
#include <queue>
#include "IPClient.hh"

CLICK_DECLS

IPClient::IPClient() : _timer(this) {
	seq = 0;
	memset(G,0,sizeof(G));
	memset(pre,0,sizeof(pre));
	memset(ipToPort,0,sizeof(ipToPort));
}

IPClient::~IPClient(){}

int IPClient::initialize(ErrorHandler *errh){
    _timer.initialize(this);
    _timer.schedule_after_msec(500);
    return 0;
}

int IPClient::configure(Vector<String> &conf, ErrorHandler *errh) {
    Args(conf, this, errh).read_mp("MY_IP", _myIP).complete();
    G[_myIP][_myIP] = 0;
    return 0;
}

void IPClient::broadcast(Packet* p){
    int n = noutputs();
    int sent = 0;
    for (int i = 1; i < n-1 ; i++)
    {
      if(Packet *pp = p->clone())
          output(i).push(pp);
      sent++;
    }
    output(n-1).push(p);
    sent++;
    assert(sent == n - 1);
}

void IPClient::sendout(Packet* packet){
    IPPacket* data = (IPPacket*)packet->data();
    if(data->dst == 0)
         broadcast(packet);
    else if(ipToPort[data->dst] != 0)
         output(ipToPort[data->dst]).push(packet);
//    else
//         output(ipToPort[route[data->dst]]).push(packet);packet
}

void IPClient::calPath()
{
    std::queue<int> q;
    for(int v = 0;v < 4;v++) d[v] = 1000;
    memset(inq,0,sizeof(inq));
    q.push(_myIP);d[_myIP] = 0;inq[_myIP] = 1;
    while(!q.empty())
    {
        int u = q.front();q.pop();inq[u] = 0;
        for(int v = 0;v < 4;v++)
        {
            if(G[u][v]!=0)
            {
                if(d[v]>d[u] + 1)
                {
                    d[v] = d[u] + 1;pre[v] = u;
                    if(!inq[v]) {q.push(v);inq[v] = 1;}
                }
            }
        }
    }

    for(int v = 0;v < 4;v++)
    {
        if(v == _myIP || pre[v] == 0) continue;
        int pred = v;
        while(pre[pred] != _myIP){
             pred = pre[pred];
        }
        route[v] = pred;
    }

    click_chatter("Route table of %d:",_myIP);
    for(int i = 1;i <= 3;i++)
    {
    if(i == _myIP) continue;
        click_chatter("%d: %d",i,route[i]);
    }

}


WritablePacket* IPClient::makeIP(int type,int src,int dst,int seq,int size,int data_in[100],TCPheader tcpdata = TCPheader())
{
    int packetsize = sizeof(IPPacket);
    WritablePacket *packet = Packet::make(0, 0, packetsize, 0);
    if(packet == 0) click_chatter("ERROR: can not make packet!");
    IPPacket* data = (IPPacket*)packet->data();
    data->type = type;
    data->src = src;
    data->dst = dst;
    data->size = size;
    data->seq = seq;
    if(data_in != NULL) memcpy(data->data,data_in,sizeof(data));
    data->tcpdata = tcpdata;
    return packet;
}

void IPClient::push(int port,Packet* packet){

    if(port == 0)
    {
        TCPheader* header = (struct TCPheader*) packet->data();
        int dstip = header->dstip, srcip = _myIP;
        click_chatter("Send packet: dstip %d, srcip %d, myip %d",dstip,srcip,_myIP);
        WritablePacket *packet = makeIP(DATA,_myIP,dstip,1,1,NULL,*header);
        sendout(packet);
    }
    else
    {
        int packetsize = sizeof(IPPacket);
        IPPacket* rec = (IPPacket*)packet->data();
        click_chatter("%d %d %d",rec->type,rec->src,rec->dst);
        //debug info
        if(rec->type == DATA && rec->dst == 0){
            exit(0);
        }
        switch (rec->type)
        {
            case HELLO:
            {
                int u=rec->src,v=_myIP;
                ipToPort[u] = port;
                if(G[u][v] == 0 || rec->size != LinkList.size())
                {
                    if(!G[u][v]) LinkList.push_back((Link){u,v});
                    G[u][v] = G[v][u] = 1;
                    click_chatter("%d",LinkList.size());
                    int data_in[100];data_in[0] = u,data_in[1] = v;
                    WritablePacket *packet = makeIP(LINK,_myIP,0,1,1,data_in);
                    calPath();
                    broadcast(packet);

                    for(int i = 0;i < LinkList.size();i++)
                    {
                        data_in[0] = LinkList[i].u,data_in[1] = LinkList[i].v;
                        packet = makeIP(LINK,_myIP,u,i,LinkList.size(),data_in);
                        output(port).push(packet);
                    }
                }
                break;
            }
            case LINK:
            {
                int u=rec->data[0],v=rec->data[1];
                click_chatter("Link %d -> %d received",u,v);
                if(G[u][v] == 0)
                {
                    G[u][v] = G[v][u] = 1;
                    LinkList.push_back((Link){u,v});
                    if(rec->dst!= _myIP){
                        broadcast(packet);
                    }
                    int data_in[100];data_in[0] = u,data_in[1] = v;
                    WritablePacket *packet = makeIP(ACK,_myIP,rec->src,rec->seq,rec->size,data_in);
                    output(port).push(packet);
                    calPath();
                }
                break;
            }
            case ACK:
            {
                if(rec->seq + 1 > rec->size) return;
                int data_in[100];data_in[0] = LinkList[rec->seq].u;data_in[1] = LinkList[rec->seq].v;
                WritablePacket *packet = makeIP(LINK,_myIP,rec->src,rec->seq+1,LinkList.size(),data_in);
                output(port).push(packet);
                break;
            }
            case DATA:
            {
                if(rec->dst == _myIP)
                {
                    TCPheader* header = &(rec->tcpdata);
                    int dstip = header->dstip, srcip = header->srcip;
                    click_chatter("Received packet: dstip %d, srcip %d, myip %d",dstip,srcip,_myIP);
                    WritablePacket *packet = Packet::make(0,0,sizeof(TCPheader),0); //TODO: TCP packet
                    TCPheader* new_header = (struct TCPheader*) packet->data();
                    memcpy(new_header,header,sizeof(TCPheader));
                    output(0).push(packet);
                }
                else
                sendout(packet);
                break;
            }
            case BYE:
            {
                int u = rec->dst,v = _myIP;
                if(G[u][v] != 0)
                {
                    std::vector<Link>::iterator i = LinkList.begin();
                    while(i != LinkList.end()){
                        if(i->u == u || i->v == u) {G[u][v] = 0;i = LinkList.erase(i);}
                        else i++;
                    }
                    broadcast(packet);
                }
            }
        }
    }
}

void IPClient::run_timer(Timer *timer) {
    assert(timer == &_timer);
    WritablePacket *packet = makeIP(HELLO,_myIP,0,0,LinkList.size(),NULL);
    broadcast(packet);
    _timer.reschedule_after_sec(1);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(IPClient)
