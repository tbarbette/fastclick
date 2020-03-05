#ifndef MIDDLEBOX_PATHMERGER_HH
#define MIDDLEBOX_PATHMERGER_HH
#include <click/element.hh>
#include "stackelement.hh"
#include "tcphelper.hh"

CLICK_DECLS

/*
=c

PathMerger()

=s middlebox

used to merge two paths into one in the middlebox stack

=d

This element is used to merge two paths into one in the middlebox. It must be used instead of
connecting the output of two components to the input of another one. This is required as when
an element calls a method of the stack, it propagates the call to the element connected to its
input. If two outputs are connected to one input of an element, it cannot know which one of the
two outputs was used to send the packet.
PathMerger associates a packet to the input it came from and thus it is able to propagate
the method call to the right element by selecting the right input when a method of the stack
is called on a packet.
To merge two paths into one, connect each path to a different input of a PathMerger and the unique
output will correspond to the merged path.

*/

/*
 * Structure used by the PathMerger element
 */
struct fcb_pathmerger
{
    HashTable<tcp_seq_t, int> portMap;

    fcb_pathmerger() : portMap(0)
    {
    }
};

class PathMerger : public TCPHelper, public StackElement
{
public:
    /** @brief Construct an PathMerger element
     */
    PathMerger() CLICK_COLD;

    /** @brief Destruct a PathMerger element
     */
    ~PathMerger() CLICK_COLD;

    const char *class_name() const        { return "PathMerger"; }
    const char *port_count() const        { return "2/1"; }
    const char *processing() const        { return PUSH; }

    int configure(Vector<String> &, ErrorHandler *) CLICK_COLD;

    void push_packet(int port, Packet *packet);

    #if HAVE_BATCH
    void push_batch(int port, PacketBatch *batch);
    #endif

    virtual void addStackElementInList(StackElement* element, int port);

protected:
    virtual void removeBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position,
         uint32_t length);
    virtual WritablePacket* insertBytes(struct fcb *fcb, WritablePacket* packet, uint32_t position,
         uint32_t length) CLICK_WARN_UNUSED_RESULT;
    virtual void requestMorePackets(struct fcb *fcb, Packet *packet, bool force = false);
    virtual void packetSent(struct fcb *fcb, Packet* packet);
    virtual void closeConnection(struct fcb* fcb, WritablePacket *packet, bool graceful,
        bool bothSides);
    virtual bool isLastUsefulPacket(struct fcb* fcb, Packet *packet);
    virtual unsigned int determineFlowDirection();

private:
    StackElement* previousStackElements[2]; // Previous elements for the two inputs

    /** @brief Return the input number for a given packet
     * @param fcb Pointer to the FCB of the flow
     * @param packet The packet
     * @return Input number from which the given packet came from
     */
    int getPortForPacket(struct fcb *fcb, Packet *packet);

    /** @brief Set the input number for a given packet
     * @param fcb Pointer to the FCB of the flow
     * @param packet The packet
     * @param port Input number from which the given packet came from
     */
    void setPortForPacket(struct fcb *fcb, Packet *packet, int port);

    /** @brief Return the element from which a given packet came from
     * @param fcb Pointer to the FCB of the flow
     * @param packet The packet
     * @return Element from which a given packet came from
     */
    StackElement* getElementForPacket(struct fcb *fcb, Packet* packet);

    /** @brief Remove the entry associating a packet to an input number
     * @param fcb Pointer to the FCB of the flow
     * @param packet The packet
     */
    void removeEntry(struct fcb *fcb, Packet* packet);

    /** @brief Add an entry associating a packet to an input number
     * @param fcb Pointer to the FCB of the flow
     * @param packet The packet
     * @param port The input number
     */
    void addEntry(struct fcb *fcb, Packet* packet, int port);
};
*/
CLICK_ENDDECLS

#endif
