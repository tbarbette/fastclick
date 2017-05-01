#include <click/config.h>

#include "metron.hh"

#include <click/args.hh>
#include <click/error.hh>

#include "../json/json.hh"


CLICK_DECLS


Metron::Metron() {

}

Metron::~Metron() {

}


int Metron::configure(Vector<String> &conf, ErrorHandler *errh) {
    if (Args(conf, this, errh)
       // .read_p("PORT", _port) //TODO AUTH
        .complete() < 0)
        return -1;

    return 0;
}


int Metron::initialize(ErrorHandler *errh) {

	return 0;
}

void Metron::cleanup(CleanupStage stage) {

}

String Metron::read_handler(Element *e, void *user_data) {
	Metron *m = static_cast<Metron *>(e);
    intptr_t what = reinterpret_cast<intptr_t>(user_data);

    switch (what) {
    	case h_ressources:
    		Json j = Json::make_object();
    		j.set("cpu",Json(click_max_cpu_ids()));
    		return j.unparse();
		break;
    }
}

void Metron::add_handlers() {
	add_read_handler("ressources", read_handler, h_ressources);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel)

EXPORT_ELEMENT(Metron)
ELEMENT_MT_SAFE(Metron)
