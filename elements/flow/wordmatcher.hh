#ifndef MIDDLEBOX_INSULTREM_HH
#define MIDDLEBOX_INSULTREM_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include "stackelement.hh"
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

/*
=c

WordMatcher([CLOSECONNECTION])

=s middlebox

removes insults in payload. This element is not intended for performance
as it loops through a list of insults. Use FlowIDSMatcher for an efficient
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

class WordMatcher : public StackSpaceElement<fcb_WordMatcher>
{
public:
    /** @brief Construct an WordMatcher element
     */
    WordMatcher() CLICK_COLD;

    const char *class_name() const        { return "WordMatcher"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) override CLICK_COLD;

    void push_batch(int port, fcb_WordMatcher* fcb, PacketBatch*) override;

protected:

    virtual int maxModificationLevel() override;

    /** @brief Remove an insult in the web page stored in the buffer
     * @param fcb Pointer to the FCB of the flow
     * @param insult The insult to remove
     * @return The result of the deletion (1: insult found and removed, -1 insult not found, 0
     * insult not found but may start at the end of the last packet in the buffer)
     */
    int removeInsult(struct fcb_WordMatcher* fcb, const char *insult);

    per_thread<MemoryPool<struct flowBufferEntry>> poolBufferEntries;

    Vector<String> insults; // Vector containing the words to remove from the web pages

    bool closeAfterInsults;
    bool _replace;
    bool _insert;
    bool _full;
    String _insert_msg;
};

CLICK_ENDDECLS
#endif
