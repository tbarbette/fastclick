#include <click/config.h>
#include "chattermessage.hh"
#include <click/glue.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/args.hh>
#include <elements/json/json.hh>
CLICK_DECLS

ChatterMessage::ChatterMessage() :
	_active(false), _channel("default") {
}

ChatterMessage::~ChatterMessage() {
}

int
ChatterMessage::configure(Vector<String> &conf, ErrorHandler *errh) {
	bool active=true;
	String channel("default");
	String prefix("");

	if (Args(conf, this, errh)
	.read_mp("TYPE", _type)
	.read_mp("MSG", StringArg(), _message)
	.read("ACTIVE", active)
	.read("CHANNEL", channel)
	.complete() < 0) {
		return -1;
	}

	_active = active;
	_channel = channel;
	_ehandler = router()->chatter_channel(_channel);
	format_message();

	return 0;
}

Packet*
ChatterMessage::simple_action(Packet *p) {
	emit();
	return p;
}

void
ChatterMessage::format_message() {
	Json message = Json();
	message.set("type", _type);
	message.set("content", _message);
	_formated_message = message.unparse();
}

void
ChatterMessage::emit() const {
	_ehandler->message(_formated_message.c_str());
}

CLICK_ENDDECLS
EXPORT_ELEMENT(ChatterMessage)
ELEMENT_REQUIRES(Json)
ELEMENT_MT_SAFE(ChatterMessage)
