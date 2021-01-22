// -*- mode: c++; c-basic-offset: 4 -*-
#ifndef CLICK_SEARCH_HH
#define CLICK_SEARCH_HH
#include <click/batchelement.hh>
CLICK_DECLS

/*
 * =c
 * Search()
 * =s basicmod
 * Strip the head of the packet up to pattern to be found in the packet content.
 * =d
 *
 * =item PATTERN
 *
 * The string to search. The packet "data" pointer will be placed after
 *
 * =item STRIP_AFTER
 *
 * Go after the pattern instead of before
 *
 * =item ANNO
 *
 * An annotation where to place the number of skipped bytes
 *
 * =item SET_ANNO
 *
 * Set the above annotation or not.
 *
 * =e
 * Use this to get rid of all headers up to some pattern, like a HTTP \n\r:
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

class Search : public BatchElement { public:

    Search() CLICK_COLD;

    const char *class_name() const override		{ return "Search"; }
    const char *port_count() const override		{ return PORTS_1_1X2; }
    const char *processing() const override		{ return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push(int, Packet *) override;
#if HAVE_BATCH
    void push_batch(int, PacketBatch *) override;
#endif

    int action(Packet* p);

  private:

    int _anno;
    String _pattern;
    bool _set_anno;
    bool _strip_after;

};

CLICK_ENDDECLS
#endif
