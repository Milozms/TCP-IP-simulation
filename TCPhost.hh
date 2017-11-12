#ifndef IPPACKETCLASSIFIER_HH
#define IPPACKETCLASSIFIER_HH

#include <click/element.hh>
#include <click/timer.hh>

CLICK_DECLS
enum connection_state {
    IDLE,
    ACTIVE_PENDING,
    PASSIVE_PENDING,
    ESTABLISHED,
    ACTIVE_DIS_PENDING,
    PASSIVE_DIS_PENDING
};
class TCPhost;
class TCPconnection{
    public:
        uint16_t tcp_port;
        uint32_t dstip;
        uint32_t _seq;
        Vector<Packet*> window_unacked, window_waiting, receiver_buf;
        int lfs, lar; //sender, last frame sent, last ack received
        int las;// receiver， last ack sent
        connection_state state;
        Timer timer;
        TCPconnection(uint32_t dst, uint16_t port, TCPhost* host);
        ~TCPconnection();
};
class TCPhost : public Element
{
	public:
		TCPhost();
		~TCPhost();
        int configure(Vector<String> &conf, ErrorHandler *errh);
		const char *class_name() const {return "TCPhost";}
		const char *port_count() const {return "2/2";}
		const char *processing() const {return PUSH;}
		void run_timer(Timer*);

        void push(int port, Packet *p);
    private:
        uint32_t _my_address, _dstip;
        Vector<TCPconnection*> connections;
        TCPconnection* find_connection(uint32_t destip);
};

CLICK_ENDDECLS

#endif