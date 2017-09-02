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
}

int InsultRemover::configure(Vector<String> &conf, ErrorHandler *errh)
{
    //TODO : use a proper automaton for insults

    if(Args(conf, this, errh)
            .read_all("WORD", insults)
            .read_p("REPLACE", _replace)
            .read_p("CLOSECONNECTION", closeAfterInsults)
    .complete() < 0)
        return -1;

    if (insults.size() == 0) {
        return errh->error("No words given");
    }

    return 0;
}


void InsultRemover::push_batch(int port, fcb_insultremover* insultremover, PacketBatch* flow)
{
    insultremover->flowBuffer.enqueueAll(flow);

    for(int i = 0; i < insults.size(); ++i)
    {
        const char* insult = insults[i].c_str();
        if (_replace) {
            auto iter = insultremover->flowBuffer.contentBegin();
            auto end = insultremover->flowBuffer.contentEnd();

            while (iter != end) {
                //click_chatter("Letter %c",*iter);
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
                            } else {
                                goto finished;
                            }
                            goto needMore;
                        }
                        //click_chatter("Letter %c",*iter);
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
                            break;
                            pos = 0;
                        }
                    } while (*iter == insult[pos]);
                }
                ++iter;
            }
        } else {
            assert(false);
            //TODO : Probably not working anymore
            /*result = insultremover->flowBuffer.removeInFlow(insult);

            // While we keep finding complete insults in the packet
            while(result == 1)
            {
                insultremover->counterRemoved += 1;
                result = insultremover->flowBuffer.removeInFlow(insult);
            }*/
        }
    }
    finished:
    //Finished without being in the middle of an insult. If closeconn was set and there was an insult, we already jumped further.
    output_push_batch(0, insultremover->flowBuffer.dequeueAll());
    return;

    closeconn:

    closeConnection(flow, true, true);
    insultremover->flowBuffer.dequeueAll()->kill();
    /*
    removeBytes(packet, 0, getPacketContentSize(packet));
    const char *message = "<font color='red'>The web page contains insults and has been "
        "blocked</font><br />";
    packet = insertBytes(fcb, packet, 0, strlen(message) + 1);
    strcpy((char*)getPacketContent(packet), message);*/
    return;

    needMore:


    if(flow != NULL)
        output_push_batch(0, flow);
    return;
}


CLICK_ENDDECLS
EXPORT_ELEMENT(InsultRemover)
ELEMENT_MT_SAFE(InsultRemover)
