#ifndef DataClient_HH
#define DataClient_HH

#include <click/element.hh>
#include <click/timer.hh>

CLICK_DECLS
class DataClient : public Element
{
	public:
		DataClient();
		~DataClient();
        int configure(Vector<String> &conf, ErrorHandler *errh);
		const char *class_name() const {return "DataClient";}
		const char *port_count() const {return "0/1";}
		const char *processing() const {return PUSH;}
		void run_timer(Timer*);

		Packet* make_packet(uint32_t dstip, uint32_t srcip, uint32_t seqnum, uint32_t acknum,
								 bool synflag, bool ackflag, bool finflag);
    private:
        int initialize(ErrorHandler *);
        uint32_t _my_address, _dstip, _rate,_delay;
        Timer _timer;
};

CLICK_ENDDECLS

#endif