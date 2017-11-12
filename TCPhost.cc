#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/vector.cc>
#include <click/args.hh>
#include "TCPhost.hh"
#include "TCPpacket.hh"
#define WINDOW_SIZE 100
#define RECV_BUF_SIZE 100
#define TIME_OUT 1000
CLICK_DECLS
TCPconnection::TCPconnection(uint32_t dst, uint16_t port, TCPhost* host): dstip(dst),tcp_port(port),_seq(1),window_unacked(0, NULL),
window_waiting(0, NULL),receiver_buf(RECV_BUF_SIZE,NULL),state(IDLE),lfs(0),las(0),lar(0), timer(host){}
TCPhost::TCPhost():connections(0, NULL) {}
TCPhost::~TCPhost() {}
TCPconnection* TCPhost::find_connection(uint32_t destip){
    for(Vector<TCPconnection*>::const_iterator i = connections.begin();i!=connections.end();i++){
        if((*i)->dstip==destip){
            return *i;
        }
    }
    return NULL;//connection doesn't exist
}
int TCPhost::configure(Vector<String> &conf, ErrorHandler *errh){
////
    Args(conf, this, errh).read_mp("MY_IP", _my_address).read_mp("DST_IP", _dstip).complete();
    return 0;
}
void TCPhost::run_timer(Timer* timer){
    click_chatter("Timer.");
    for(Vector<TCPconnection*>::const_iterator i = connections.begin();i!=connections.end();i++){
        TCPconnection* conn = *i;
        if(timer == &(conn->timer)){
            if(conn->state == ACTIVE_PENDING){
                Packet *syn = conn->window_unacked[0];
                struct TCPheader* syn_header = (struct TCPheader*) syn->data();
                click_chatter("Sending SYN(SEQ = %u) to %u.", syn_header->seqnum, syn_header->dstip);
                output(1).push(syn);
                timer->schedule_after_msec(TIME_OUT);
            }
            else if (conn->state == PASSIVE_PENDING){
                Packet *syn = conn->window_unacked[0];
                struct TCPheader* syn_header = (struct TCPheader*) syn->data();
                click_chatter("Sending SYN ACK(SEQ = %u, ACK = %u) to %u.", syn_header->seqnum, syn_header->acknum, syn_header->dstip);
                output(1).push(syn);
                timer->schedule_after_msec(TIME_OUT);
            }
            else{
                Packet *pkt = conn->window_unacked[0];
                struct TCPheader* header = (struct TCPheader*) pkt->data();
                click_chatter("Sending DATA to %u, seq = %u.", header->dstip, header->seqnum);
                output(1).push(pkt);
                timer->schedule_after_msec(TIME_OUT);
            }
        }
    }
}
//input port 0: from ip
//output port 0: to ip layer
void TCPhost::push(int port, Packet* packet)
{
    assert(packet);
    if(port == 0){//data from upper layer
        struct TCPheader* header = (struct TCPheader*) packet->data();
        TCPconnection* conn;
        conn = find_connection(header->dstip);
        if(conn==NULL){//not exist --> send syn
            //connection establish
            conn = new TCPconnection(_dstip, connections.size(), this);
            click_chatter("Note: new connection to ip %u.", header->dstip);
            connections.push_back(conn);
            WritablePacket *syn = Packet::make(0,0,sizeof(struct TCPheader), 0);
            memset(syn->data(),0,syn->length());
            struct TCPheader* syn_header = (struct TCPheader*) syn->data();
            syn_header->SYN_TCP = true;
            syn_header->ACK_TCP = syn_header->FIN = false;
            syn_header->seqnum = conn->_seq;
            conn->_seq++;
            syn_header->acknum = 0;
            syn_header->srcip = _my_address;
            syn_header->srcport = conn->tcp_port;
            syn_header->dstip = _dstip;
            //syn_heaser->dstport

            click_chatter("Sending SYN(SEQ = %u) to %u.", syn_header->seqnum, syn_header->dstip);
            output(1).push(syn);
            conn->timer.initialize(this);
            conn->timer.schedule_after_msec(TIME_OUT);
            conn->state = ACTIVE_PENDING;
            conn->window_unacked.push_back(syn);
            //conn->window_waiting.push_back(packet);
            //timer
        }
        else if(conn->state != ESTABLISHED){
            click_chatter("Connection unestablished, Packet Waiting.");
            conn->window_waiting.push_back(packet);
        }
        else{
            if(conn->window_unacked.size()<WINDOW_SIZE){//window not full
                WritablePacket *new_packet = Packet::make(0,0,sizeof(struct TCPheader), 0);
                memcpy(new_packet->data(),packet->data(),new_packet->length());
                TCPheader* new_packet_header = (struct TCPheader*) new_packet->data();
                conn->window_unacked.push_back(packet->clone());
                new_packet_header->SYN_TCP = new_packet_header->ACK_TCP = new_packet_header->FIN = false;
                new_packet_header->seqnum = conn->_seq;
                new_packet_header->srcip = _my_address;
                conn->_seq++;
                click_chatter("Sending DATA to %u, seq = %u.", new_packet_header->dstip, new_packet_header->seqnum);
                output(1).push(new_packet); //to ip layer
                conn->lfs++;
            }
            else{//window full
                click_chatter("Window full, Packet Waiting.");
                conn->window_waiting.push_back(packet);
            }
        }        

    }
    else if(port == 1){//data from ip layer
        struct TCPheader* header = (struct TCPheader*) packet->data();
        TCPconnection* conn;
        conn = find_connection(header->srcip);
        click_chatter("Received packet: dstip %d, srcip %d, myip %d",header->dstip,header->srcip,_my_address);
        click_chatter("Received packet %u from %u %u", header->seqnum, header->srcip, header->acknum);
        if(conn == NULL){
            click_chatter("Error: connection to ip %u not found.", header->srcip);
        }
        if(header->SYN_TCP && !header->ACK_TCP){
            //received syn --> send syn ack
            if(conn==NULL){//not exist
                //connection establish
                conn = new TCPconnection(header->srcip, connections.size(), this);
                connections.push_back(conn);
                WritablePacket *syn = Packet::make(0,0,sizeof(struct TCPheader), 0);
                memset(syn->data(),0,syn->length());
                struct TCPheader* syn_header = (struct TCPheader*) syn->data();
                syn_header->SYN_TCP = syn_header->ACK_TCP = true;
                syn_header->FIN = false;
                syn_header->seqnum = conn->_seq;
                syn_header->acknum = header->seqnum+1;
                syn_header->srcip = _my_address;
                syn_header->srcport = conn->tcp_port;
                syn_header->dstip = header->srcip;
                //syn_heaser->dstport

                click_chatter("Sending SYN ACK(SEQ = %u, ACK = %u) to %u.", syn_header->seqnum, syn_header->acknum, syn_header->dstip);
                output(1).push(syn);
                conn->timer.initialize(this);
                conn->timer.schedule_after_msec(TIME_OUT);
                conn->state = PASSIVE_PENDING;
                //timer
            }
        }
        else if(header->SYN_TCP && header->ACK_TCP){
            click_chatter("Branch 1: receive SYN ACK, packet->seq = %u, conn->seq = %u.", header->seqnum, conn->_seq);
            //received syn ack
            if(header->acknum == conn->_seq){
                click_chatter("Receive SYN ACK, Connection to %u Established.", header->srcip);
                conn->state = ESTABLISHED;
                //send ack back
                WritablePacket *ack = Packet::make(0,0,sizeof(struct TCPheader), 0);
                memset(ack->data(),0,ack->length());
                struct TCPheader* ack_header = (struct TCPheader*) ack->data();
                ack_header->ACK_TCP = true;
                ack_header->SYN_TCP = ack_header->FIN = false;
                ack_header->seqnum = header->acknum;
                ack_header->acknum = header->seqnum+1;
                ack_header->srcip = _my_address;
                //ack_header->srcport = 0; //problem
                ack_header->dstip = header->srcip;
                ack_header->dstport = header->srcport;
                click_chatter("Sending ACK(SEQ = %u, ACK = %u) to %u.", ack_header->seqnum, ack_header->acknum, ack_header->dstip);
                output(1).push(ack);
                conn->window_unacked.pop_front();
            }

        }
        else if(header->ACK_TCP){
            //received ack
            if(conn==NULL){
                //not exist

            }
            else{
                if(conn->state == PASSIVE_PENDING){
                    if(header->acknum == conn->_seq+1){
                        //received ack for connection establish
                        click_chatter("Receive ACK_TCP, Connection to %u Established.", header->srcip);
                        conn->state = ESTABLISHED;
                    }
                }
                else if(conn->state == ESTABLISHED){
                    click_chatter("Receive ACK_TCP, LAR = %u.", conn->lar);
                    conn->lar++;
                    //update sliding window
                    conn->window_unacked.pop_front();
                    click_chatter("Number of waiting packets: %u.", conn->window_waiting.size());
                    if(conn->window_waiting.size()>0){
                        click_chatter("Pop from queue.");
                        WritablePacket* poppacket = conn->window_waiting[0]->clone()->uniqueify();
                        conn->window_waiting.pop_front();
                        //send it
                        struct TCPheader* header = (struct TCPheader*) poppacket->data();
                        conn->window_unacked.push_back(packet);
                        header->SYN_TCP = header->ACK_TCP = header->FIN = false;
                        header->seqnum = conn->_seq;
                        header->srcip = _my_address;
                        conn->_seq++;
                        //header->srcport = 0; //problem
                        //header->dstip = header->srcip;
                        //header->dstport = header->srcport;
                        click_chatter("Sending DATA to %u, seq = %u.", header->dstip, header->seqnum);
                        output(1).push(poppacket); //to ip layer
                        conn->lfs++;

                    }
                }
            }
            

        }
        else{//receive data -> send ack （to ip layer)
            //buffer las+1~las+W
            if(conn!=NULL){
                click_chatter("Receiving DATA, seq = %u.", header->seqnum);
                if(header->seqnum > conn->las + RECV_BUF_SIZE || header->seqnum <= conn->las){
                    click_chatter("Buffer overflow, discard packet.");
                }
                else{
                    conn->receiver_buf[header->seqnum - conn->las - 1] = packet->clone();
                    click_chatter("Buffer packet at position %u, seq = %u.", header->seqnum - conn->las - 1, header->seqnum);
                    while(conn->receiver_buf[0] != NULL){
                        //send ack for las
                        struct TCPheader* header = (struct TCPheader*) conn->receiver_buf[0]->data();
                        WritablePacket *ack = Packet::make(0,0,sizeof(struct TCPheader), 0);
                        memset(ack->data(),0,ack->length());
                        struct TCPheader* ack_header = (struct TCPheader*) ack->data();
                        ack_header->ACK_TCP = true;
                        ack_header->SYN_TCP = ack_header->FIN = false;
                        ack_header->seqnum = header->seqnum;
                        ack_header->srcip = _my_address;
                        //ack_header->srcport = 0; //problem
                        ack_header->dstip = header->srcip;
                        ack_header->dstport = header->srcport;
                        click_chatter("Sending ACK_TCP for seq = %u to %u.", ack_header->seqnum, ack_header->dstip);
                        output(1).push(ack);
                        conn->las++;
                        conn->receiver_buf.pop_front();
                        conn->receiver_buf.push_back(NULL);
                    }
                }
            }
        }
        
        
    }
    packet->kill();
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPhost)