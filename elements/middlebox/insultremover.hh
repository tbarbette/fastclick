#ifndef MIDDLEBOX_INSULTREM_HH
#define MIDDLEBOX_INSULTREM_HH
#include <click/element.hh>
#include <click/vector.hh>
#include <click/multithread.hh>
#include "stackelement.hh"
#include "flowbuffer.hh"
#include "flowbufferentry.hh"

CLICK_DECLS

/*
=c

InsultRemover([CLOSECONNECTION])

=s middlebox

removes insults in web pages

=d

This element removes insults in web pages

=item CLOSECONNECTION

Boolean that can be set to true if the connection must be closed when an insult is found.
In this case, the content of the page is replaced by an error message telling the user
that the web page has been blocked because it contains insults then the TCP connection is closed.
Default value: false.

=a HTTPIn, HTTPOut */

#define POOL_BUFFER_ENTRIES_SIZE 300

class InsultRemover : public StackElement
{
public:
    /** @brief Construct an InsultRemover element
     */
    InsultRemover() CLICK_COLD;

    const char *class_name() const        { return "InsultRemover"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

protected:
    Packet* processPacket(struct fcb*, Packet*);

    /** @brief Remove an insult in the web page stored in the buffer
     * @param fcb Pointer to the FCB of the flow
     * @param insult The insult to remove
     * @return The result of the deletion (1: insult found and removed, -1 insult not found, 0
     * insult not found but may start at the end of the last packet in the buffer)
     */
    int removeInsult(struct fcb* fcb, const char *insult);

    per_thread<MemoryPool<struct flowBufferEntry>> poolBufferEntries;

    Vector<const char*> insults; // Vector containing the words to remove from the web pages

    bool closeAfterInsults;
    bool closed;
};

CLICK_ENDDECLS
#endif
