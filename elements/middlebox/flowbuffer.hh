#ifndef MIDDLEBOX_FLOWBUFFER_HH
#define MIDDLEBOX_FLOWBUFFER_HH

#include <clicknet/tcp.h>
#include "memorypool.hh"
#include "flowbufferentry.hh"
#include "stackelement.hh"
#include "fcb.hh"

CLICK_DECLS

class FlowBufferContentIter;
class FlowBufferIter;

class FlowBuffer
{
public:
    friend class FlowBufferIter;

    FlowBuffer();
    ~FlowBuffer();

    void initialize(StackElement *owner, MemoryPool<struct flowBufferEntry> *poolEntries);
    bool isInitialized();
    StackElement* getOwner();
    MemoryPool<struct flowBufferEntry>* getMemoryPool();

    void enqueue(WritablePacket *packet);
    WritablePacket* dequeue();

    FlowBufferIter begin();
    FlowBufferIter end();

    int searchInFlow(const char *pattern);
    bool removeInFlow(struct fcb* fcb, const char* pattern);
    bool replaceInFlow(struct fcb *fcb, const char* pattern, const char *replacement);

private:
    FlowBufferContentIter contentBegin();
    FlowBufferContentIter contentEnd();
    FlowBufferContentIter search(FlowBufferContentIter start, const char* pattern, int *feedback);
    void remove(struct fcb *fcb, FlowBufferContentIter start, uint32_t length);

    MemoryPool<struct flowBufferEntry> *poolEntries;
    struct flowBufferEntry *head;
    struct flowBufferEntry *tail;
    StackElement *owner;
    bool initialized;
};

class FlowBufferIter
{
public:
    FlowBufferIter(FlowBuffer *_flowBuffer, flowBufferEntry* _entry);

    bool operator==(const FlowBufferIter& other) const;
    bool operator!=(const FlowBufferIter& other) const;
    WritablePacket*& operator*();
    FlowBufferIter& operator++();

private:
    FlowBuffer *flowBuffer;
    struct flowBufferEntry *entry;
};

class FlowBufferContentIter
{
public:
    friend class FlowBuffer;

    FlowBufferContentIter(FlowBuffer *_flowBuffer, flowBufferEntry* _entry);

    bool operator==(const FlowBufferContentIter& other) const;
    bool operator!=(const FlowBufferContentIter& other) const;
    unsigned char& operator*();
    FlowBufferContentIter& operator++();

private:
    FlowBuffer *flowBuffer;
    struct flowBufferEntry *entry;
    uint32_t offsetInPacket;
};

CLICK_ENDDECLS

#endif
