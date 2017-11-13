#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/vector.cc>
#include <click/args.hh>
#include "TCPhost.hh"
#include "TCPpacket.hh"
#define WINDOW_SIZE 100
#define RECV_BUF_SIZE 100
//#define TIME_OUT 3000
CLICK_DECLS
TCPconnection::TCPconnection(uint32_t dst, uint16_t port, TCPhost* host): dstip(dst),tcp_port(port),_seq(1),synseq(1),
synackseq(1), window_unacked(0, NULL),
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
    Args(conf, this, errh).read_mp("MY_IP", _my_address).read_mp("DST_IP", _dstip).read_mp("TIME_OUT", TIME_OUT).complete();
    return 0;
}
Packet* TCPhost::write_packet(Packet* p, uint32_t dstip=-1, uint32_t srcip=-1, uint32_t seqnum=-1, uint32_t acknum=-1,
                              bool synflag=false, bool ackflag=false, bool finflag=false){
    WritablePacket* packet = p->clone()->uniqueify();
    struct TCPheader* header = (struct TCPheader*) packet->data();
    if(dstip != -1) header->dstip = dstip;
    if(srcip != -1) header->srcip = srcip;
    if(seqnum != -1) header->seqnum = seqnum;
    if(acknum != -1) header->acknum = acknum;
    return packet;
}

Packet* TCPhost::make_packet(uint32_t dstip, uint32_t srcip, uint32_t seqnum, uint32_t acknum,
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
void TCPhost::run_timer(Timer* timer){
//    click_chatter("Timer. Num of connections = %u.", connections.size());
    for(Vector<TCPconnection*>::const_iterator i = connections.begin();i!=connections.end();i++){
        TCPconnection* conn = *i;
//        click_chatter("Timer finding.");
        if(timer == &(conn->timer)){
//            click_chatter("Find right timer.");
            if(conn->state == SYN_SENT){
                //Packet* syn = write_packet(conn->window_unacked[0]);//Don't!!!
                Packet *syn = make_packet(conn->dstip, _my_address, conn->_seq, 0, true);
                struct TCPheader* syn_header = (struct TCPheader*) syn->data();
                click_chatter("Retransmit: Sending SYN(SEQ = %u) to %u.", syn_header->seqnum, syn_header->dstip);
                output(1).push(syn);
            }else if(conn->state == FIN_WAIT1){
                Packet* fin = make_packet(conn->dstip, _my_address, conn->_seq, 0, false, false, true);
                click_chatter("Retransmit: Sending FIN(SEQ = %u).", conn->_seq);
                output(1).push(fin);
            }
            else if (conn->state == SYN_RCVD && conn->window_unacked.size()>0){
                Packet* syn = write_packet(conn->window_unacked[0]);
                struct TCPheader* syn_header = (struct TCPheader*) syn->data();
                click_chatter("Retransmit: Sending SYN ACK(SEQ = %u, ACK = %u) to %u.", syn_header->seqnum, syn_header->acknum, syn_header->dstip);
                output(1).push(syn);
            }
            else if(conn->state == ESTABLISHED && conn->window_unacked.size()>0){
                Packet* pkt = write_packet(conn->window_unacked[0]);
                struct TCPheader* header = (struct TCPheader*) pkt->data();
                click_chatter("Retransmit: Sending DATA to %u, seq = %u.", header->dstip, header->seqnum);
                output(1).push(pkt);
            }
            else if(conn->state == LAST_ACK){
                Packet* fin = make_packet(conn->dstip, _my_address, conn->_seq, 0, false, false, true);
                click_chatter("Retransmit: Sending FIN(SEQ = %u, ACK = %u).", conn->_seq, conn->finacknum);
                output(1).push(fin);
            }
        }
    }
    timer->schedule_after_msec(TIME_OUT);
//    click_chatter("Timer end.");
}
//input port 0: from ip
//output port 0: to ip layer
void TCPhost::push(int port, Packet* packet)
{
    assert(packet);
    if(port == 0){//data from upper layer
        //tmp: set dest ip
        packet = write_packet(packet, _dstip);
        struct TCPheader* header = (struct TCPheader*) packet->data();
        TCPconnection* conn;
        conn = find_connection(_dstip);
        if(conn==NULL){//not exist --> send syn
            //connection establish
            conn = new TCPconnection(_dstip, connections.size(), this);
            click_chatter("Note: new connection to ip %u.", _dstip);
            connections.push_back(conn);
            Packet* syn = make_packet(header->dstip, _my_address, conn->_seq, 0, true);
//            Packet* syn = make_packet(conn->dstip, _my_address, conn->_seq, 0, true);
            struct TCPheader* syn_header = (struct TCPheader*) syn->data();
            //conn->_seq++;
            conn->state = SYN_SENT;
            //conn->window_unacked.push_back(syn->clone()); // Don't!!!
            conn->window_waiting.push_back(packet->clone());
            click_chatter("Connection unestablished, Packet Waiting, push into queue.");
            click_chatter("Sending SYN(SEQ = %u) to %u.", syn_header->seqnum, syn_header->dstip);
            conn->synseq = syn_header->seqnum;
            output(1).push(syn);
            conn->timer.initialize(this);
            conn->timer.schedule_after_msec(TIME_OUT);
            //timer
        }
        else if(header->FIN){
            Packet* fin = make_packet(header->dstip, _my_address, conn->_seq, 0, false, false, true);
            click_chatter("Sending FIN(SEQ = %u).", conn->_seq);
            output(1).push(fin);
            conn->finseq = conn->_seq;
            conn->state = FIN_WAIT1;
        }
        else if(conn->state != ESTABLISHED){
            click_chatter("Connection unestablished, Packet Waiting, push into queue.");
            conn->window_waiting.push_back(packet->clone());
        }
        else {
            if(conn->window_unacked.size()<WINDOW_SIZE){//window not full
                Packet* new_packet = write_packet(packet, -1, _my_address, conn->_seq, -1);
                TCPheader* new_packet_header = (struct TCPheader*) new_packet->data();
                conn->window_unacked.push_back(new_packet->clone());
                click_chatter("Sending DATA to %u, seq = %u.", new_packet_header->dstip, new_packet_header->seqnum);
                click_chatter("Push into window_unacked, seq = %u, position 1.", new_packet_header->seqnum);
                conn->_seq++;
                click_chatter("Now seq = %u.", conn->_seq);
                output(1).push(new_packet); //to ip layer
//                conn->timer.schedule_after_msec(TIME_OUT);
                conn->lfs++;
            }
            else{//window full
                click_chatter("Window full, Packet Waiting, push into queue.");
                conn->window_waiting.push_back(packet->clone());
            }
        }

    }
    else if(port == 1){//data from ip layer
        struct TCPheader* header = (struct TCPheader*) packet->data();
        TCPconnection* conn;
        conn = find_connection(header->srcip);
        //click_chatter("Received packet: dstip %d, srcip %d, myip %d",header->dstip,header->srcip,_my_address);
        click_chatter("Received packet %u from %u %u", header->seqnum, header->srcip, header->acknum);
        if(conn == NULL){
            //click_chatter("Error: connection to ip %u not found.", header->srcip);
            if(header->SYN_TCP && !header->ACK_TCP){
                //connection establish
                conn = new TCPconnection(header->srcip, connections.size(), this);
                connections.push_back(conn);
                Packet *syn = make_packet(header->srcip, _my_address, conn->_seq, header->seqnum+1, true, true);
                struct TCPheader* syn_header = (struct TCPheader*) syn->data();
                //syn_heaser->dstport

                click_chatter("Sending SYN ACK(SEQ = %u, ACK = %u) to %u.", syn_header->seqnum, syn_header->acknum, syn_header->dstip);
                conn->synackseq = syn_header->seqnum;
                //conn->las++;
                click_chatter("Push into window_unacked, seq = %u, position 2.", syn_header->seqnum);
                conn->window_unacked.push_back(syn->clone());
                output(1).push(syn);
                conn->timer.initialize(this);
                conn->timer.schedule_after_msec(TIME_OUT);
                conn->state = SYN_RCVD;
            }
        }
        else if(conn->state == SYN_SENT){
            if(header->SYN_TCP && !header->ACK_TCP){
                Packet *syn = make_packet(header->srcip, _my_address, conn->_seq, header->seqnum+1, true, true);
                struct TCPheader* syn_header = (struct TCPheader*) syn->data();
                //syn_heaser->dstport

                click_chatter("Sending SYN ACK(SEQ = %u, ACK = %u) to %u.", syn_header->seqnum, syn_header->acknum, syn_header->dstip);
                conn->synackseq = syn_header->seqnum;
                //conn->las++;
                click_chatter("Push into window_unacked, seq = %u, position 2-1.", syn_header->seqnum);
                conn->window_unacked.push_back(syn->clone());
                output(1).push(syn);
                conn->timer.initialize(this);
                conn->timer.schedule_after_msec(TIME_OUT);
                conn->state = SYN_RCVD;
            }
            if(header->SYN_TCP && header->ACK_TCP){
                click_chatter("Branch 2: receive SYN ACK, packet->seq = %u.", header->seqnum);
                //received syn ack
                if(header->acknum == conn->synseq + 1){
                    click_chatter("Receive SYN ACK, Connection to %u Established.", header->srcip);
                    conn->state = ESTABLISHED;
                    //send ack back
                    Packet *ack = make_packet(header->srcip, _my_address, header->acknum, header->seqnum+1, false, true);
                    struct TCPheader* ack_header = (struct TCPheader*) ack->data();
                    click_chatter("Sending ACK_TCP(SEQ = %u, ACK = %u) to %u.", ack_header->seqnum, ack_header->acknum, ack_header->dstip);
                    output(1).push(ack);
                    //conn->window_unacked.pop_front(); //Don't!!!
//                conn->timer.unschedule();
                    //begin sending
                    click_chatter("Number of waiting packets: %u.", conn->window_waiting.size());
                    while(conn->window_waiting.size()>0){
                        click_chatter("Pop from queue - window_waiting.");
                        Packet* poppacket = write_packet(conn->window_waiting[0], -1, -1, conn->_seq);
                        conn->window_waiting.pop_front();
                        //send it
                        struct TCPheader* header = (struct TCPheader*) poppacket->data();
                        conn->window_unacked.push_back(poppacket->clone());
                        conn->_seq++;
                        click_chatter("Sending DATA to %u, seq = %u.", header->dstip, header->seqnum);
                        click_chatter("Push into window_unacked, seq = %u, position 3.", header->seqnum);
                        click_chatter("Now seq = %u.", conn->_seq);
                        output(1).push(poppacket); //to ip layer
//                    conn->timer.schedule_after_msec(TIME_OUT);
                        conn->lfs++;

                    }
                }

            }
        }
        else if(conn->state == SYN_RCVD){
            if(header->ACK_TCP){
                if(header->acknum == conn->synackseq+1){
                    //received ack for connection establish
                    click_chatter("Receive ACK_TCP, Connection to %u Established.", header->srcip);
                    conn->state = ESTABLISHED;
                    conn->window_unacked.pop_front();
//                        conn->timer.unschedule();
                }
            }
            else if(header->SYN_TCP && header->ACK_TCP){
                //received syn ack
                if(header->acknum == conn->synseq + 1){
                    click_chatter("Receive SYN ACK, Connection to %u Established.", header->srcip);
                    conn->state = ESTABLISHED;
                    //send ack back
                    Packet *ack = make_packet(header->srcip, _my_address, header->acknum, header->seqnum+1, false, true);
                    struct TCPheader* ack_header = (struct TCPheader*) ack->data();
                    click_chatter("Sending ACK_TCP(SEQ = %u, ACK = %u) to %u.", ack_header->seqnum, ack_header->acknum, ack_header->dstip);
                    output(1).push(ack);
                    //conn->window_unacked.pop_front(); //Don't!!!
//                conn->timer.unschedule();
                    //begin sending
                    click_chatter("Number of waiting packets: %u.", conn->window_waiting.size());
                    while(conn->window_waiting.size()>0){
                        click_chatter("Pop from queue - window_waiting.");
                        Packet* poppacket = write_packet(conn->window_waiting[0], -1, -1, conn->_seq);
                        conn->window_waiting.pop_front();
                        //send it
                        struct TCPheader* header = (struct TCPheader*) poppacket->data();
                        conn->window_unacked.push_back(poppacket->clone());
                        conn->_seq++;
                        click_chatter("Sending DATA to %u, seq = %u.", header->dstip, header->seqnum);
                        click_chatter("Push into window_unacked, seq = %u, position 3.", header->seqnum);
                        click_chatter("Now seq = %u.", conn->_seq);
                        output(1).push(poppacket); //to ip layer
//                    conn->timer.schedule_after_msec(TIME_OUT);
                        conn->lfs++;

                    }
                }

            }
        }
        else if(conn->state == ESTABLISHED) {
            if (header->ACK_TCP) {
                if (conn->lar + 1 == header->seqnum) {
                    click_chatter("Receive ACK_TCP for seq = %u, LAR = %u.", header->seqnum, conn->lar);
                    conn->lar++;
                    //update sliding window
                    conn->window_unacked.pop_front();
//                    conn->timer.unschedule();
                    click_chatter("Number of waiting packets: %u.", conn->window_waiting.size());
                    while (conn->window_waiting.size() > 0) {
                        click_chatter("Pop from queue - window_waiting.");
                        Packet *poppacket = write_packet(conn->window_waiting[0], -1, -1, conn->_seq);
                        conn->window_waiting.pop_front();
                        //send it
                        struct TCPheader *header = (struct TCPheader *) poppacket->data();
                        conn->window_unacked.push_back(poppacket->clone());
                        conn->_seq++;
                        click_chatter("Sending DATA to %u, seq = %u.", header->dstip, header->seqnum);
                        click_chatter("Push into window_unacked, seq = %u, position 4.", header->seqnum);
                        click_chatter("Now seq = %u.", conn->_seq);
                        output(1).push(poppacket); //to ip layer
//                        conn->timer.schedule_after_msec(TIME_OUT);
                        conn->lfs++;
                    }
                } else {
                    click_chatter("Receive extra ACK for seq = %u.", header->seqnum);
                }
            }
            else if(!header->SYN_TCP && !header->ACK_TCP && !header->FIN){//receive data -> send ack （to ip layer)
                //buffer las+1~las+W
                click_chatter("Receiving DATA, seq = %u.", header->seqnum);
                if (header->seqnum > conn->las + RECV_BUF_SIZE) {
                    click_chatter("Buffer overflow, discard packet.");
                } else if (header->seqnum <= conn->las) {
                    click_chatter("Retransmit ACK_TCP for seq = %u.", header->seqnum);
                    Packet *ack = make_packet(header->srcip, _my_address, header->seqnum, 0, false, true);
                    output(1).push(ack);
                } else {
                    conn->receiver_buf[header->seqnum - conn->las - 1] = packet->clone();
                    click_chatter("Buffer packet at position %u, seq = %u.", header->seqnum - conn->las - 1,
                                  header->seqnum);
                    while (conn->receiver_buf[0] != NULL) {
                        //send ack for las
                        struct TCPheader *header = (struct TCPheader *) conn->receiver_buf[0]->data();
                        Packet *ack = make_packet(header->srcip, _my_address, header->seqnum, 0, false, true);
                        struct TCPheader *ack_header = (struct TCPheader *) ack->data();
                        click_chatter("Sending ACK_TCP for seq = %u to %u.", ack_header->seqnum, ack_header->dstip);
                        output(1).push(ack);
                        conn->las++;
                        conn->receiver_buf.pop_front();
                        conn->receiver_buf.push_back(NULL);
                    }
                }

            }
            else if(header->FIN){
                click_chatter("Receive FIN, seq = %u, current state: ESTABLISED.", header->seqnum);
                Packet *fin_ack = make_packet(header->srcip, _my_address, conn->_seq,header->seqnum+1,false,true,false);
                click_chatter("Sending ACK for FIN, (SEQ = %u, ACK = %u).", conn->_seq, header->seqnum+1);
                conn->finacknum = header->seqnum+1;
                output(1).push(fin_ack);
                conn->state = CLOSE_WAIT;
                click_chatter("Sending FIN (SEQ = %u, ACK = %u).", conn->_seq, header->seqnum+1);
                conn->finseq = conn->_seq;
                Packet *fin = make_packet(header->srcip, _my_address, conn->_seq,header->seqnum+1,false,true,true);
                output(1).push(fin);
                conn->state = LAST_ACK;
                click_chatter("New state: LAST_ACK.");
            }
        }
        else if(conn->state == FIN_WAIT1){
            if(header->ACK_TCP){
                click_chatter("Receive ACK for FIN, seq = %u, ack = %u, current state: FIN_WAIT1.", header->seqnum, header->acknum);
                if(header->acknum==conn->finseq + 1){
                    conn->state = FIN_WAIT2;
                    click_chatter("New state: FIN_WAIT2.");
                }
            }
            else if(header->FIN){
                click_chatter("Receive FIN, seq = %u, ack = %u, current state: FIN_WAIT1.", header->seqnum, header->acknum);
                Packet *fin_ack = make_packet(header->srcip, _my_address, conn->_seq,header->seqnum+1,false,true,false);
                click_chatter("Sending ACK for FIN, (SEQ = %u, ACK = %u).", conn->_seq, header->seqnum+1);
                conn->finacknum = header->seqnum+1;
                output(1).push(fin_ack);
                conn->state = CLOSING;
                click_chatter("New state: CLOSING.");
            }
        }
        else if(conn->state == FIN_WAIT2){
            if(header->FIN){
                click_chatter("Receive FIN, seq = %u, ack = %u, current state: FIN_WAIT2.", header->seqnum, header->acknum);
                Packet *fin_ack = make_packet(header->srcip, _my_address, conn->_seq,header->seqnum+1,false,true,false);
                click_chatter("Sending ACK for FIN, (SEQ = %u, ACK = %u).", conn->_seq, header->seqnum+1);
                conn->finacknum = header->seqnum+1;
                output(1).push(fin_ack);
                conn->state = IDLE;
                click_chatter("New state: IDLE.");
            }
        }
        else if(conn->state == CLOSING){
            if(header->ACK_TCP){
                click_chatter("Receive ACK for FIN, seq = %u, ack = %u, current state: CLOSING.", header->seqnum, header->acknum);
                if(header->acknum==conn->finseq + 1) {
                    conn->state = IDLE;
                    click_chatter("New state: IDLE.");
                }
            }

        }
        else if(conn->state == CLOSE_WAIT){
            conn->state = LAST_ACK;
            click_chatter("New state: LAST_ACK.");
        }
        else if(conn->state == LAST_ACK){
            if(header->ACK_TCP){
                click_chatter("Receive ACK for FIN, seq = %u, ack = %u, current state: LAST_ACK.", header->seqnum, header->acknum);
                if(header->seqnum==conn->finseq + 1){
                    conn->state = IDLE;
                    click_chatter("New state: IDLE.");
                }
            }
        }
        else if(conn->state == TIME_OUT){
            conn->state = IDLE;
            click_chatter("New state: IDLE.");

        }
    }
    packet->kill();
}

CLICK_ENDDECLS
EXPORT_ELEMENT(TCPhost)