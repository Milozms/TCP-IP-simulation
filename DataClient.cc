#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/vector.cc>
#include <click/args.hh>
#include "DataClient.hh"
#include "TCPpacket.hh"
CLICK_DECLS

DataClient::DataClient():_timer(this) {}
DataClient::~DataClient() {}
int DataClient::configure(Vector<String> &conf, ErrorHandler *errh){
    Args(conf, this, errh).
            read_mp("MY_IP", _my_address).
            read_mp("DST_IP", _dstip).
            read_mp("RATE", _rate).
            read_mp("DELAY",_delay).
            read_mp("LIMIT",_limit).complete();
    _timer.initialize(this);
    _timer.schedule_after_msec(_delay);
    return 0;
}
int DataClient::initialize(ErrorHandler*){
    _timer.initialize(this);
    return 0;
}
Packet* DataClient::make_packet(uint32_t dstip, uint32_t srcip, uint32_t seqnum, uint32_t acknum,
                                      bool synflag=false, bool ackflag=false, bool finflag=false){
    WritablePacket *packet = Packet::make(0,0,sizeof(struct TCPheader), 0);
    memset(packet->data(),0,packet->length());
    struct TCPheader* header = (struct TCPheader*) packet->data();
    header->dstip = dstip;
    header->srcip = srcip;
    header->seqnum = seqnum;
    header->acknum = acknum;
    header->SYN_TCP = synflag;
    header->ACK_TCP = ackflag;
    header->FIN = finflag;
    return packet;
}
void DataClient::run_timer(Timer* timer){
    //click_chatter("Timer.");
    if(timer == &_timer)
    {
        if(_limit >=0 )
        {
            Packet* syn = make_packet(_dstip, _my_address,0,0);
            output(0).push(syn);
            _timer.reschedule_after_msec(_rate);
            _limit--;
        }
        else
        {
            Packet* syn = make_packet(_dstip, _my_address,0,0, false,false, true);
            output(0).push(syn);
        }
    }
    //click_chatter("Timer end.");
}

CLICK_ENDDECLS
EXPORT_ELEMENT(DataClient)