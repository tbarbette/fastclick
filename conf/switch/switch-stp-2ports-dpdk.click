//Configuration of a two-port switch with STP support intended for userlevel + DPDK
define( $dev1 0,
        $dev2 1)

FromDPDKDevice($dev1) -> f0 :: Classifier(14/4242, 0/00C095E2169C, -);
FromDPDKDevice($dev2) -> f1 :: Classifier(14/4242, 0/00C095E2169D, -);

w0 :: Queue -> ToDPDKDevice($dev1);
w1 :: Queue -> ToDPDKDevice($dev2);

s :: EtherSwitch;
in :: Suppressor;
out :: Suppressor;

f0[2] -> [0]in[0] -> [0]s[0] -> [0]out[0] -> w0;
f1[2] -> [1]in[1] -> [1]s[1] -> [1]out[1] -> w1;

st :: EtherSpanTree(00:02:03:04:05:06, in, out, s);
f0[0] -> [0]st[0] -> w0;
f1[0] -> [1]st[1] -> w1;

tohost :: ToHost;
f0[1] -> tohost;
f1[1] -> tohost;

