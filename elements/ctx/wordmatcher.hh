#ifndef MIDDLEBOX_WORDMATCHER_HH
#define MIDDLEBOX_WORDMATCHER_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include <click/flow/ctxelement.hh>
#include <click/flowbuffer.hh>

CLICK_DECLS

/**
 * Structure used by the WordMatcher element
 */
struct fcb_WordMatcher
{
    FlowBuffer flowBuffer;
    uint32_t counterRemoved;

    fcb_WordMatcher()
    {
        /*counterRemoved = 0;*/
    }
};

enum DPIMode {ALERT, CLOSE, MASK, REPLACE, REMOVE, FULL};

/*
=c

WordMatcher([CLOSECONNECTION])

=s middlebox

removes insults in payload. This element is not intended for performance
as it loops through a list of insults. Use CTXIDSMatcher for an efficient
version.

=d

This element removes or replace insults in web pages

=item CLOSECONNECTION

Boolean that can be set to true if the connection must be closed when an insult is found.
In this case, the content of the page is replaced by an error message telling the user
that the web page has been blocked because it contains insults then the TCP connection is closed.
Default value: false.

=item REPLACE
Boolean if true, replace the insult instead of removing the bytes. Default to true.

=a HTTPIn, HTTPOut */

#define POOL_BUFFER_ENTRIES_SIZE 300

class WordMatcher : public CTXSpaceElement<fcb_WordMatcher>
{
public:
    /** @brief Construct an WordMatcher element
     */
    WordMatcher() CLICK_COLD;

    const char *class_name() const        { return "WordMatcher"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    void push_flow(int port, fcb_WordMatcher* fcb, PacketBatch*) override;

    static String read_handler(Element *e, void *thunk);
    void add_handlers() override;

protected:

    virtual int maxModificationLevel(Element* stop) override;

    Vector<StringRef> _words; // Vector containing the words to remove from the web pages

    enum DPIMode _mode;
    bool _all;
    bool _quiet;
    String _insert_msg;
    atomic_uint32_t found;

};

CLICK_ENDDECLS
#endif
