#ifndef CLICK_NULLS_HH
#define CLICK_NULLS_HH
#include <click/element.hh>
CLICK_DECLS

/*
=c

Null1() ... Null8()

=s basictransfer

copy of Null

=d

The elements Null1 through Null8 are reimplementations of Null. However, each
has independent code, so the i-cache cost of using all eight elements (Null1
through Null8) is eight times the cost of eight Null elements.

=a Null */

class Null1 : public Element {

 public:

  Null1()				{ }

  const char *class_name() const override	{ return "Null1"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  Packet *simple_action(Packet *p)	{ return p; }

};

class Null2 : public Element {

 public:

  Null2()				{ }

  const char *class_name() const override	{ return "Null2"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  Packet *simple_action(Packet *p)	{ return p; }

};

class Null3 : public Element {

 public:

  Null3()				{ }

  const char *class_name() const override	{ return "Null3"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  Packet *simple_action(Packet *p)	{ return p; }

};

class Null4 : public Element {

 public:

  Null4()				{ }

  const char *class_name() const override	{ return "Null4"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  Packet *simple_action(Packet *p)	{ return p; }

};

class Null5 : public Element {

 public:

  Null5()				{ }

  const char *class_name() const override	{ return "Null5"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  Packet *simple_action(Packet *p)	{ return p; }

};

class Null6 : public Element {

 public:

  Null6()				{ }

  const char *class_name() const override	{ return "Null6"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  Packet *simple_action(Packet *p)	{ return p; }

};

class Null7 : public Element {

 public:

  Null7()				{ }

  const char *class_name() const override	{ return "Null7"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  Packet *simple_action(Packet *p)	{ return p; }

};

class Null8 : public Element {

 public:

  Null8()				{ }

  const char *class_name() const override	{ return "Null8"; }
  const char *port_count() const override	{ return PORTS_1_1; }

  Packet *simple_action(Packet *p)	{ return p; }

};

CLICK_ENDDECLS
#endif
