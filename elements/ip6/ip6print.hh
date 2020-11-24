#ifndef CLICK_IP6PRINT_HH
#define CLICK_IP6PRINT_HH
#include <click/batchelement.hh>
#include <click/string.hh>
CLICK_DECLS

/*
=c

IP6Print([LABEL, NBYTES, CONTENTS, ACTIVE])

=s ip6

pretty-prints IP6 packets

=d

dumps simple information about ip6 packet.
LABEL specifies the label at the head of each
line. NBYTES specify how many bytes to print
and CONTENTS specify if the content should
be printed, in hex. NBYTES and CONTENTS
are keywords.

Keyword arguments are:

=over 4

=item LABEL

String. A label to print before each packet. Default is an empty label.

=item NBYTES

Integer. Determines how many bytes to print. Default is 1500 bytes.

=item CONTENTS

Boolean. Determines whether the packet data is printed (in hex). Default is false.

=item ACTIVE

Boolean. If false, then don't print messages. Default is true.

=back

=h active read-write

Sets/Gets the active flag of this element.

=a CheckIP6Header, IPPrint */

class IP6Print : public SimpleElement<IP6Print> {
    public:
        IP6Print();
        ~IP6Print();

        const char *class_name() const override { return "IP6Print"; }
        const char *port_count() const override { return PORTS_1_1; }

        int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
        void add_handlers() CLICK_COLD;

        Packet *simple_action(Packet *);

    private:
        String _label;
        unsigned _bytes;
        bool _contents;
        bool _active;
};

CLICK_ENDDECLS
#endif
