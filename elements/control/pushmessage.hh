#ifndef CLICK_PUSHMESSAGE_HH
#define CLICK_PUSHMESSAGE_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c
PushMessage(TYPE, MSG, [KEYWORDS])

=s control
Pushes a message via the chatter channel

=d

Prints a chatter MSG with a set TYPE.
The chatter format is a simple JSON:
{"type":"TYPE", "content":MSG}


Keyword arguments are:
=over 8

=item ACTIVE

Boolean. Will send chatter messages only if ACTIVE is true.

=item CHANNEL

Text word. Send messages to this chatter CHANNEL. Default
is the default channel, which corresponds to C<click_chatter()>.

=item ATTACH_PACKET
Boolean. If true it will assume the MSG is formatted with an '%s'
and will put there the packet's data. Default is false.

=item PACKET_SIZE
Integer. The number of bytes from the packet to attach to MSG it ATTACH_PACKET is true.
Default is 64.

=back
=e


For example,

  ...->PushMessage(LOG, "This is a log message: %s", ATTACH_PACKET true, PACKET_SIZE 20)->...

*/
class PushMessage : public Element {
	public:

		PushMessage() CLICK_COLD;
		~PushMessage() CLICK_COLD;

		const char *class_name() const		{ return "PushMessage"; }
		const char *port_count() const    { return PORTS_1_1; }

		bool can_live_reconfigure() const   { return true; }

		int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		Packet *simple_action(Packet *);
		// void add_handlers() CLICK_COLD;

	private:
		void emit(Packet *p) const;
		void format_message();
		String _type;
		String _message;
		int _severity;
		bool _active;
		String _channel;
		String _formated_message;
		bool _attach_packet;
		int _packet_size;
		char * _packet_data;
		ErrorHandler *_ehandler;
};

CLICK_ENDDECLS
#endif
