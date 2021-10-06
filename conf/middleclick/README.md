Example configurations
======================

 * middlebox-empty-ip.click A typical middlebox with two "bump in the wire" configuration. It's not transportant. It has the two CTXManager ready for IPv4 configuration, with ARP management.
 * middlebox-tcp-ids.click An extended version of the configuration above that is implementing a simple TCP/HTTP IDS that can match and mask/replace/remove content of HTTP streams. It is the one used in the mininet topology (see the mininet folder).
 * firewall.click A simple firewall configuration that supports ARP, it's therefore not transparent
