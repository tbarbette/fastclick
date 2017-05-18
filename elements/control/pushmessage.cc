#include <click/config.h>
#include "pushmessage.hh"
#include <click/glue.hh>
#include <click/error.hh>
#include <click/router.hh>
#include <click/args.hh>
#include <elements/json/json.hh>
CLICK_DECLS

PushMessage::PushMessage() :
_active(false), _channel("default"), _attach_packet(false), _packet_size(64), _packet_data(0) {
}

PushMessage::~PushMessage() {
	if (_packet_data) {
		CLICK_LFREE(_packet_data, _packet_size);
	}
}

int
PushMessage::configure(Vector<String> &conf, ErrorHandler *errh) {
	bool active=true;
	String channel("default");
	int packet_size = 64;
	int severity = 1;
	bool attach_packet = false;
	String prefix("");
	if (Args(conf, this, errh)
	.read_mp("TYPE", _type)
	.read_mp("MSG", StringArg(), _message)
	.read("SEVERITY", severity)
	.read("ACTIVE", active)
	.read("CHANNEL", channel)
	.read("ATTACH_PACKET", attach_packet)
	.read("PACKET_SIZE", packet_size)
	.complete() < 0) {
		return -1;
	}
	_active = active;
	_channel = channel;
	_severity = severity;
	_attach_packet = attach_packet;
	if (!attach_packet) {
		_packet_size = 0;
	} else {
		_packet_size = packet_size;
	}
	_packet_data = (char *) CLICK_LALLOC(_packet_size * 3 + 1);
	_ehandler = router()->chatter_channel(_channel);
	format_message();
	return 0;
}

Packet *
PushMessage::simple_action(Packet *p) {
	emit(p);
	return p;
}

void
PushMessage::format_message() {
	Json message = Json();
	message.set("type", _type);
	message.set("content", _message);
	_formated_message = message.unparse();
}

void
PushMessage::emit(Packet *p) const {
	if (_attach_packet) {
		int size = (int) p->length() < _packet_size ? p->length() : _packet_size;
		const unsigned char *data = p->data();
		char *buf = _packet_data;
		for (int i=0; i<size; i++, data++) {
			sprintf(buf, "%02x ", *data & 0xff);
			buf += 3;
		}
		if (size) {
			*(buf-1) = '\0';
		} else {
			*buf = '\0';
		}
		_ehandler->message(_formated_message.c_str(), _packet_data);
	} else {
		_ehandler->message(_formated_message.c_str(), "");
	}
}

CLICK_ENDDECLS
EXPORT_ELEMENT(PushMessage)
ELEMENT_REQUIRES(Json)
ELEMENT_MT_SAFE(PushMessage)
