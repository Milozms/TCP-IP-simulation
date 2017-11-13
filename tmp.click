require(library /home/comnetsii/elements/lossyrouterport.click);

rp :: LossyRouterPort(DEV $dev, IN_MAC $in_mac , OUT_MAC $out_mac, LOSS 0.9, DELAY 0.2);

cl :: IPClient(MY_IP 1);
tcp :: TCPhost(MY_IP 1, DST_IP 2, TIME_OUT 1000);
//PacketGenerator()->rp->Discard();

DataClient(MY_IP 1, DST_IP 2, RATE 1000, DELAY 0,LIMIT 20) -> [0]tcp[0] -> Discard
//RatedSource(DATA "hello", RATE 1, LIMIT 10) -> [0]tcp[0] -> Discard
rp->[1]cl[0]->[1]tcp[1]->[0]cl[1]->rp;