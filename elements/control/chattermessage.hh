#ifndef CLICK_CHATTERMESSAGE_HH
#define CLICK_CHATTERMESSAGE_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c
ChatterMessage(TYPE, MSG, [KEYWORDS])

=s control
Prints a chatter message.

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

=back
=e


For example,

  ...->ChatterMessage(LOG, "This is a log message")->...

=h type rw
Returns or sets the message TYPE

=h message rw
Returns or sets the message text

*/
class ChatterMessage : public Element {
	public:

		ChatterMessage() CLICK_COLD;
		~ChatterMessage() CLICK_COLD;

		const char *class_name() const		{ return "ChatterMessage"; }
		const char *port_count() const    { return PORTS_1_1; }

		bool can_live_reconfigure() const   { return true; }

		int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;
		Packet *simple_action(Packet *);
		// void add_handlers() CLICK_COLD;

	private:
		void emit() const;
		void format_message();
		String _type;
		String _message;
		bool _active;
		String _channel;
		String _formated_message;
		ErrorHandler *_ehandler;
};

CLICK_ENDDECLS
#endif
