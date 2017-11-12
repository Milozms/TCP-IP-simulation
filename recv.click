require(library /home/comnetsii/elements/routerport.click);

rp :: RouterPort(DEV $dev, IN_MAC $in_mac , OUT_MAC $out_mac);
host :: TCPhost;
Idle() -> [0]host[0] -> Discard()
rp -> [1]host[1] -> rp