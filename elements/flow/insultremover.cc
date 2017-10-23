/*
 * insultremover.{cc,hh} -- remove insults in web pages
 * Romain Gaillard
 * Tom Barbette
 */

#include <click/config.h>
#include <click/router.hh>
#include <click/args.hh>
#include <click/error.hh>
#include "insultremover.hh"
#include "tcpelement.hh"

CLICK_DECLS

InsultRemover::InsultRemover() : insults()
{
    // Initialize the memory pool of each thread
    for(unsigned int i = 0; i < poolBufferEntries.weight(); ++i)
        poolBufferEntries.get_value(i).initialize(POOL_BUFFER_ENTRIES_SIZE);

    closeAfterInsults = false;
    _replace = true;
    _insert = false;
    _full = false;
}

int InsultRemover::configure(Vector<String> &conf, ErrorHandler *errh)
{
    //TODO : use a proper automaton for insults
    _insert_msg = "<font color='red'>Blocked content !</font><br />";
    String mode = "MASK";
    if(Args(conf, this, errh)
            .read_all("WORD", insults)
            .read_p("MODE", mode)
            .read_p("MSG", _insert_msg)
            .read_p("CLOSECONNECTION", closeAfterInsults)
    .complete() < 0)
        return -1;

    if (mode == "MASK") {
        _replace = true;
        _insert = false;
    } else if (mode == "REMOVE") {
        _replace = false;
        _insert = false;
    } else if (mode == "REPLACE") {
        _replace = false;
        _insert = true;
    } else if (mode == "FULL") {
        _replace = false;
        _insert = false;
        _full = true;
    } else {
        return errh->error("Mode must be MASK, REMOVE, REPLACE or FULL");
    }


    if (insults.size() == 0) {
        return errh->error("No words given");
    }

    return 0;
}

int
InsultRemover::maxModificationLevel() {
    int mod = StackSpaceElement<fcb_insultremover>::maxModificationLevel() | MODIFICATION_WRITABLE;
    if (!_replace) {
        mod |= MODIFICATION_RESIZE | MODIFICATION_STALL;
    } else {
        mod |= MODIFICATION_REPLACE;
    }
    return mod;
}


void InsultRemover::push_batch(int port, fcb_insultremover* insultremover, PacketBatch* flow)
{
    insultremover->flowBuffer.enqueueAll(flow);

    /**
     * This is mostly an example element, so we have two modes :
     * - Replacement, done inline using the iterator directly in this element
     * - Removing, done calling iterator.remove
     */
    for(int i = 0; i < insults.size(); ++i)
    {
        const char* insult = insults[i].c_str();
        if (_replace) {
            auto iter = insultremover->flowBuffer.contentBegin();
            auto end = insultremover->flowBuffer.contentEnd();

            while (iter != end) {
                if (*iter ==  insult[0]) {
                    int pos = 0;
                    typeof(iter) start_pos = iter;
                    do {
                        ++pos;
                        ++iter;
                        if (iter == end) {
                            //Finished in the middle of a potential match, ask for more packets
                            flow = start_pos.flush();
                            if(!isLastUsefulPacket(start_pos.current())) {
                                requestMorePackets(start_pos.current(), false);
                                goto needMore;
                            } else {
                                goto finished;
                            }
                        }
                        if (insult[pos] == '\0') {
                            insultremover->counterRemoved += 1;
                            if (closeAfterInsults)
                                goto closeconn;
                            pos = 0;
                            while (insult[pos] != '\0') {
                                *start_pos = '*';
                                ++start_pos;
                                ++pos;
                            }
                            pos = 0;
                        }
                    } while (*iter == insult[pos]);
                }
                ++iter;
            }
        } else {
            int result;
            do {
                if (!_insert) { //If not insert, just remove
                    result = insultremover->flowBuffer.removeInFlow(insult, this);
                } else if (!_full){ //Insert but not full, replace pattern per message
                    result = insultremover->flowBuffer.replaceInFlow(insult, _insert_msg.c_str(), this);
                } else { //Full, repalce the whole flow per message
                    result = insultremover->flowBuffer.searchInFlow(insult);
                }
                if (result == 1) {
                    if (closeAfterInsults)
                        goto closeconn;
                    if (_full) {
                        //TODO
                        //Remove all bytes
                        //Add msg
                    }
                    insultremover->counterRemoved += 1;
                }
            } while (result == 1);

            // While we keep finding complete insults in the packet
            if (result == 0) { //Finished in the middle of a potential match
                if(!isLastUsefulPacket(flow->tail())) {
                    requestMorePackets(flow->tail(), false);
                    flow = 0; // We will re-match the whole buffer, not that much efficient. See FlowIDSMatcher for better implementation
                    goto needMore;
                } else {
                    goto finished;
                }
            }
        }
    }
    finished:
    //Finished without being in the middle of an insult. If closeconn was set and there was an insult, we already jumped further.
    output_push_batch(0, insultremover->flowBuffer.dequeueAll());
    return;

    closeconn:

    closeConnection(flow, true);
    insultremover->flowBuffer.dequeueAll()->fast_kill();

    return;

    needMore:


    if(flow != NULL)
        output_push_batch(0, flow);
    return;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(InsultRemover)
ELEMENT_MT_SAFE(InsultRemover)
