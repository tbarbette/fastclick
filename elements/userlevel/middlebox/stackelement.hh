#ifndef MIDDLEBOX_STACKELEMENT_HH
#define MIDDLEBOX_STACKELEMENT_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/router.hh>
#include <click/routervisitor.hh>
#include <click/stackvisitor.hh>

CLICK_DECLS

class StackElement : public Element
{
public:
    StackElement() CLICK_COLD;
    ~StackElement();

    // Click related methods
    const char *class_name() const        { return "StackElement"; }
    const char *port_count() const        { return PORTS_1_1; }
    const char *processing() const        { return PROCESSING_A_AH; }
    void* cast(const char*);
    int initialize(ErrorHandler*)   CLICK_COLD;

    void push(int, Packet *);
    Packet *pull(int);

    // Custom methods
    virtual bool isOutElement()          { return false; }
    virtual void packetModified(Packet*);
    void addStackElementInList(StackElement*);
    static bool isStackElement(Element*);

protected:
    virtual Packet* processPacket(Packet*);
    void setAnnotationModification(Packet*, bool);
    bool getAnnotationModification(Packet*);
    void setAnnotationAcked(Packet* p, bool value);
    bool getAnnotationAcked(Packet* p);
    uint16_t getContentOffset(Packet*);
    void setContentOffset(Packet*, uint16_t);
    void modifyPacket(Packet* packet);
    void buildFunctionStack();
    const unsigned char* getPacketContentConst(Packet*);
    unsigned char* getPacketContent(WritablePacket*);
    bool isPacketContentEmpty(Packet*);
    void removeBytes(WritablePacket*, uint32_t, uint32_t);

private:
    struct stackElementListNode *stackElementList;
    void setAnnotationBit(Packet*, int, bool);
    bool getAnnotationBit(Packet*, int);

    // Constants
    const int offsetAnnotationBools = 12;
    const int offsetContentOffset = 13; // Currently set to 2 bytes (-> to check)

    // Up to 8 booleans can be stored in the corresponding annotation
    const int offsetAnnotationModified = 0;
    const int offsetAnnotationAcked = 1;

};

struct stackElementListNode
{
    StackElement *node;
    struct stackElementListNode *next;
};

CLICK_ENDDECLS

#endif
