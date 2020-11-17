// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_Search_HH
#define CLICK_Search_HH
#include <click/element.hh>
CLICK_DECLS

/*
 * =c
 * Search()
 * =s basicmod
 * Strip the TCP header from front of packets
 * =d
 * Removes all bytes from the beginning of the packet up to the end of the TCP header.
 * It will also increase an annotation (by default Paint2) by the number of bytes poped
 * to remember how long was the header and allow to revert the operation later. This is
 * intended to be used with UnstripAnno
 * =e
 * Use this to get rid of all headers up to the end of the TCP layer, print the HTTP request payload, and
 * go back to the previous pointer:
 *
 *   s :: Search("\n\r\n\r") //Strips to the end of the pattern
 *   -> Print("HTTP REQUEST PAYLOAD") //So Print will show the content
 *   -> UntripAnno(); //Go back to where we were
 *
 *   s[1] -> Print("Malformed HTTP");
 *
 *
 * =a UnstripAnno
 */

class Search : public Element { public:

    Search() CLICK_COLD;

    const char *class_name() const override		{ return "Search"; }
    const char *port_count() const override		{ return PORTS_1_1X2; }
    const char *processing() const override		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *) override;

  private:

    int _anno;
    String _pattern;
    bool _set_anno;
    bool _strip_after;

};

CLICK_ENDDECLS
#endif
