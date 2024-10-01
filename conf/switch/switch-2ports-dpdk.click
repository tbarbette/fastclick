/*
 * Configuration for a simple 2-ports switch without STP support
 * it is intended for userlevel Click using DPDK
 * Eg launch with:
 *     click --dpdk -- switch-2ports-dpdk.click dev1=00:11:00.0 dev2=00:11:00.1
 * or omit dev1 and dev2 parameters and change them below
 */


//Define the name of the ports
define( $dev1  0,
        $dev2  1,
        $print false)

s :: EtherSwitch;

w0 :: ToDPDKDevice($dev1);
w1 :: ToDPDKDevice($dev2);

elementclass Input { $label |
    input -> c :: Classifier(12/0800, 12/0806, -);
    c[0] -> Strip(14) ->  CheckIPHeader() -> IPPrint($label, ACTIVE $print) -> Unstrip(14) -> output;
    c[1] -> output;
    c[2] -> Print(NONIP) -> Discard;
}

FromDPDKDevice($dev1) -> Input(FROMH1) -> [0]s[0] -> w0;
FromDPDKDevice($dev2) -> Input(FROMH2) -> [1]s[1] -> w1;

